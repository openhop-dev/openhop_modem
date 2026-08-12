// =============================================================
// tcp_server.cpp — multi-listener TCP protocol server with optional
// shared-token authentication.
// =============================================================
#include "tcp_server.h"
#include "protocol.h"
#include "frame_parser.h"
#include "net_filter.h"

#include <WiFi.h>

// Forward decl — implemented in main.cpp; invoked for authorized frames.
extern void processHostCommand(uint8_t cmd, const uint8_t* payload,
                               uint16_t len, TransportSource src);
extern void noteTransportFrameError(uint8_t err_code);

namespace TCPServer {

static constexpr uint8_t INVALID_SLOT = 0xFF;

struct ClientSlot {
    WiFiClient client;
    FrameParser parser;
    bool        occupied      = false;
    bool        authenticated = false;
    uint32_t    acceptedCount = 0;
    uint32_t    frameCount    = 0;
    uint32_t    generation    = 0;
};

static WiFiServer* listeners[MAX_TCP_CLIENTS] = {};
static ClientSlot  slots[MAX_TCP_CLIENTS];
static String      requiredToken = "";
static uint8_t     commandSlot   = INVALID_SLOT;

static bool requiresAuth() { return requiredToken.length() > 0; }

// isLanAddress() lives in net_filter.h — shared with the OTA server.

static void buildFrame(uint8_t* buf, uint16_t& outLen,
                       uint8_t cmd, const uint8_t* payload, uint16_t len) {
    uint16_t i = 0;
    buf[i++] = PROTO_SYNC;
    buf[i++] = cmd;
    buf[i++] = len & 0xFF;
    buf[i++] = (len >> 8) & 0xFF;
    if (len > 0 && payload) {
        memcpy(buf + i, payload, len);
        i += len;
    }
    uint16_t crc = crc16_ccitt(buf + 1, 3 + len);  // over CMD + LEN + PAYLOAD
    buf[i++] = crc & 0xFF;
    buf[i++] = (crc >> 8) & 0xFF;
    outLen = i;
}

static void sendToClient(uint8_t slot, uint8_t cmd,
                         const uint8_t* payload, uint16_t len) {
    if (slot >= MAX_TCP_CLIENTS) return;
    ClientSlot& state = slots[slot];
    if (!state.occupied || !state.client.connected()) return;
    uint8_t buf[MAX_FRAME_SIZE];
    uint16_t flen = 0;
    buildFrame(buf, flen, cmd, payload, len);
    state.client.write(buf, flen);
}

static void sendErrorToClient(uint8_t slot, uint8_t err) {
    sendToClient(slot, CMD_ERROR, &err, 1);
}

static void disconnectClient(uint8_t slot) {
    if (slot >= MAX_TCP_CLIENTS) return;
    ClientSlot& state = slots[slot];
    if (state.occupied) {
        IPAddress addr = state.client.remoteIP();
        Serial.printf("[TCP] disconnect slot=%u client %u.%u.%u.%u auth=%u frames=%lu\n",
                      (unsigned)slot, addr[0], addr[1], addr[2], addr[3],
                      state.authenticated ? 1U : 0U,
                      (unsigned long)state.frameCount);
        state.client.stop();
    }
    state.occupied = false;
    state.authenticated = false;
    state.frameCount = 0;
    state.generation++;
    state.parser.reset();
}

static void onFrameOk(uint8_t cmd, const uint8_t* payload, uint16_t len, TransportSource src) {
    (void)src;  // always TCP here
    if (commandSlot >= MAX_TCP_CLIENTS) return;
    ClientSlot& state = slots[commandSlot];
    state.frameCount++;
    Serial.printf("[TCP] slot=%u frame cmd=0x%02X len=%u auth=%u\n",
                  (unsigned)commandSlot, cmd, (unsigned)len,
                  state.authenticated ? 1U : 0U);

    if (requiresAuth() && !state.authenticated) {
        if (cmd == CMD_AUTH) {
            if (len == requiredToken.length() &&
                memcmp(payload, requiredToken.c_str(), len) == 0) {
                state.authenticated = true;
                Serial.printf("[TCP] slot=%u auth OK\n", (unsigned)commandSlot);
                sendToClient(commandSlot, CMD_AUTH_OK, nullptr, 0);
            } else {
                Serial.printf("[TCP] slot=%u auth rejected\n",
                              (unsigned)commandSlot);
                sendErrorToClient(commandSlot, ERR_UNAUTHORIZED);
                delay(5);
                disconnectClient(commandSlot);
            }
        } else {
            sendErrorToClient(commandSlot, ERR_UNAUTHORIZED);
            delay(5);
            disconnectClient(commandSlot);
        }
        return;
    }

    // Authenticated (or no auth required)
    if (cmd == CMD_AUTH) {
        // Idempotent ack — client may always send AUTH.
        sendToClient(commandSlot, CMD_AUTH_OK, nullptr, 0);
        return;
    }

    processHostCommand(cmd, payload, len, TransportSource::TCP);
}

static void onFrameErr(uint8_t err_code, TransportSource src) {
    (void)src;
    if (commandSlot >= MAX_TCP_CLIENTS) return;
    Serial.printf("[TCP] slot=%u frame parse error 0x%02X\n",
                  (unsigned)commandSlot, err_code);
    noteTransportFrameError(err_code);
    sendErrorToClient(commandSlot, err_code);
}

bool begin(uint16_t port, const String& token) {
    end();
    requiredToken = token;
    commandSlot = INVALID_SLOT;
    const uint32_t basePort = port ? port : 5055u;
    const uint32_t lastPort = basePort + MAX_TCP_CLIENTS - 1u;
    if (lastPort > 0xFFFFu) {
        Serial.printf("[TCP] invalid base port %lu: slots require %lu-%lu\n",
                      (unsigned long)basePort,
                      (unsigned long)basePort,
                      (unsigned long)lastPort);
        return false;
    }
    for (uint8_t slot = 0; slot < MAX_TCP_CLIENTS; slot++) {
        slots[slot].parser.reset();
        listeners[slot] = new WiFiServer((uint16_t)(basePort + slot));
        listeners[slot]->begin();
        listeners[slot]->setNoDelay(true);
    }
    Serial.printf("[TCP] listening on %lu-%lu auth=%s\n",
                  (unsigned long)basePort, (unsigned long)lastPort,
                  requiresAuth() ? "required" : "open");
    return true;
}

void end() {
    commandSlot = INVALID_SLOT;
    for (uint8_t slot = 0; slot < MAX_TCP_CLIENTS; slot++) {
        disconnectClient(slot);
        if (listeners[slot]) {
            listeners[slot]->end();
            delete listeners[slot];
            listeners[slot] = nullptr;
        }
    }
}

void loop() {
    for (uint8_t slot = 0; slot < MAX_TCP_CLIENTS; slot++) {
        if (!listeners[slot]) continue;
        ClientSlot& state = slots[slot];

        // A connected socket remains untouched while RF activity changes.
        // Only an actual disconnected socket is reset and replaced.
        if (state.occupied && !state.client.connected()) {
            disconnectClient(slot);
        }

        if (!state.occupied) {
            WiFiClient incoming = listeners[slot]->available();
            if (incoming) {
                // Hard LAN-only policy: drop public-IP clients before
                // touching the parser. No log spam beyond the rejection
                // notice; this is a normal firewall behaviour.
                IPAddress addr = incoming.remoteIP();
                if (!isLanAddress(addr)) {
                    Serial.printf(
                        "[TCP] rejecting non-LAN client %u.%u.%u.%u "
                        "(firmware accepts only RFC1918 / link-local / loopback)\n",
                        addr[0], addr[1], addr[2], addr[3]);
                    incoming.stop();
                } else {
                    state.client = incoming;
                    state.occupied = true;
                    state.client.setNoDelay(true);
                    state.parser.reset();
                    state.authenticated = false;
                    state.frameCount = 0;
                    state.generation++;
                    state.acceptedCount++;
                    Serial.printf("[TCP] accepted slot=%u client %u.%u.%u.%u (#%lu, auth=%s)\n",
                                  (unsigned)slot, addr[0], addr[1], addr[2], addr[3],
                                  (unsigned long)state.acceptedCount,
                                  requiresAuth() ? "required" : "open");
                }
            }
        }

        // Read available bytes through this slot's parser.
        if (state.occupied && state.client.connected()) {
            uint8_t previousSlot = commandSlot;
            commandSlot = slot;
            while (state.client.available()) {
                uint8_t b = (uint8_t)state.client.read();
                frameparser_feed(state.parser, b, TransportSource::TCP,
                                 onFrameOk, onFrameErr);
            }
            commandSlot = previousSlot;
        }
    }
}

bool isClientReady() {
    for (uint8_t slot = 0; slot < MAX_TCP_CLIENTS; slot++) {
        if (isSlotReady(slot)) return true;
    }
    return false;
}

String getClientIP() {
    uint8_t slot = activeSlot();
    return slot < MAX_TCP_CLIENTS ? getSlotIP(slot) : String();
}

void write(const uint8_t* data, size_t len) {
    if (commandSlot < MAX_TCP_CLIENTS) {
        writeToSlot(commandSlot, data, len);
    } else {
        writeToReadySlots(data, len);
    }
}

uint8_t activeSlot() {
    if (commandSlot < MAX_TCP_CLIENTS) return commandSlot;
    for (uint8_t slot = 0; slot < MAX_TCP_CLIENTS; slot++) {
        if (isSlotReady(slot)) return slot;
    }
    for (uint8_t slot = 0; slot < MAX_TCP_CLIENTS; slot++) {
        if (slots[slot].occupied && slots[slot].client.connected()) return slot;
    }
    return INVALID_SLOT;
}

bool isSlotReady(uint8_t slot) {
    if (slot >= MAX_TCP_CLIENTS) return false;
    ClientSlot& state = slots[slot];
    return state.occupied && state.client.connected() &&
           (!requiresAuth() || state.authenticated);
}

uint32_t getSlotGeneration(uint8_t slot) {
    if (slot >= MAX_TCP_CLIENTS) return 0;
    return slots[slot].generation;
}

String getSlotIP(uint8_t slot) {
    if (slot >= MAX_TCP_CLIENTS || !slots[slot].occupied ||
        !slots[slot].client.connected()) {
        return String();
    }
    return slots[slot].client.remoteIP().toString();
}

void writeToSlot(uint8_t slot, const uint8_t* data, size_t len) {
    if (slot >= MAX_TCP_CLIENTS || !data || len == 0) return;
    ClientSlot& state = slots[slot];
    if (!state.occupied) return;
    if (!state.client.connected()) {
        disconnectClient(slot);
        return;
    }
    size_t written = state.client.write(data, len);
    if (written != len && !state.client.connected()) {
        disconnectClient(slot);
    }
}

void writeToReadySlots(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    for (uint8_t slot = 0; slot < MAX_TCP_CLIENTS; slot++) {
        if (isSlotReady(slot)) writeToSlot(slot, data, len);
    }
}

uint8_t currentCommandSlot() {
    return commandSlot;
}

} // namespace TCPServer
