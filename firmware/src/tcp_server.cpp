// =============================================================
// tcp_server.cpp — single-client TCP protocol server with optional
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

static WiFiServer* server         = nullptr;
static WiFiClient  client;
static String      requiredToken  = "";
static bool        authenticated  = false;
static FrameParser parser;
static uint32_t    acceptedCount  = 0;
static uint32_t    frameCount     = 0;
static uint32_t    parsedFrameCount = 0;

// Keep the single cooperative firmware loop fair. A command callback may do
// synchronous radio work, so draining a busy TCP socket without a budget can
// indefinitely postpone HTTP/OTA servicing later in loop().
static constexpr size_t MAX_BYTES_PER_LOOP = 256;
static constexpr uint8_t MAX_FRAMES_PER_LOOP = 1;

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

static void sendToClient(uint8_t cmd, const uint8_t* payload, uint16_t len) {
    if (!client || !client.connected()) return;
    uint8_t buf[MAX_FRAME_SIZE];
    uint16_t flen = 0;
    buildFrame(buf, flen, cmd, payload, len);
    client.write(buf, flen);
}

static void sendErrorToClient(uint8_t err) {
    sendToClient(CMD_ERROR, &err, 1);
}

static void disconnectClient() {
    if (client) {
        Serial.printf("[TCP] disconnect client %s auth=%u frames=%lu\n",
                      client.remoteIP().toString().c_str(),
                      authenticated ? 1U : 0U,
                      (unsigned long)frameCount);
        client.stop();
    }
    authenticated = false;
    frameCount = 0;
    parser.reset();
}

static void onFrameOk(uint8_t cmd, const uint8_t* payload, uint16_t len, TransportSource src) {
    (void)src;  // always TCP here
    parsedFrameCount++;
    frameCount++;
    Serial.printf("[TCP] frame cmd=0x%02X len=%u auth=%u\n",
                  cmd, (unsigned)len, authenticated ? 1U : 0U);

    if (requiresAuth() && !authenticated) {
        if (cmd == CMD_AUTH) {
            if (len == requiredToken.length() &&
                memcmp(payload, requiredToken.c_str(), len) == 0) {
                authenticated = true;
                Serial.println("[TCP] auth OK");
                sendToClient(CMD_AUTH_OK, nullptr, 0);
            } else {
                Serial.println("[TCP] auth rejected");
                sendErrorToClient(ERR_UNAUTHORIZED);
                delay(5);
                disconnectClient();
            }
        } else {
            sendErrorToClient(ERR_UNAUTHORIZED);
            delay(5);
            disconnectClient();
        }
        return;
    }

    // Authenticated (or no auth required)
    if (cmd == CMD_AUTH) {
        // Idempotent ack — client may always send AUTH.
        sendToClient(CMD_AUTH_OK, nullptr, 0);
        return;
    }

    processHostCommand(cmd, payload, len, TransportSource::TCP);
}

static void onFrameErr(uint8_t err_code, TransportSource src) {
    (void)src;
    Serial.printf("[TCP] frame parse error 0x%02X\n", err_code);
    noteTransportFrameError(err_code);
    sendErrorToClient(err_code);
}

void begin(uint16_t port, const String& token) {
    end();
    requiredToken = token;
    authenticated = false;
    parser.reset();
    server = new WiFiServer(port);
    server->begin();
    server->setNoDelay(true);
}

void end() {
    disconnectClient();
    if (server) {
        server->end();
        delete server;
        server = nullptr;
    }
}

void loop() {
    if (!server) return;

    // Accept incoming connection if no current client
    if (!client || !client.connected()) {
        if (client) disconnectClient();
        WiFiClient incoming = server->available();
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
                return;
            }
            client = incoming;
            client.setNoDelay(true);
            parser.reset();
            authenticated = false;
            frameCount = 0;
            acceptedCount++;
            Serial.printf("[TCP] accepted client %s (#%lu, auth=%s)\n",
                          client.remoteIP().toString().c_str(),
                          (unsigned long)acceptedCount,
                          requiresAuth() ? "required" : "open");
        }
    }

    // Read available bytes through the parser, but always yield back to the
    // main loop after a bounded amount of work. FrameParser keeps partial
    // state, so frames larger than the byte budget continue on the next pass.
    if (client && client.connected()) {
        size_t bytesProcessed = 0;
        uint8_t framesProcessed = 0;
        uint32_t previousFrameCount = parsedFrameCount;

        while (client.connected() && client.available() &&
               bytesProcessed < MAX_BYTES_PER_LOOP &&
               framesProcessed < MAX_FRAMES_PER_LOOP) {
            uint8_t b = (uint8_t)client.read();
            bytesProcessed++;
            frameparser_feed(parser, b, TransportSource::TCP, onFrameOk, onFrameErr);

            if (parsedFrameCount != previousFrameCount) {
                framesProcessed++;
                previousFrameCount = parsedFrameCount;
            }
        }
    }
}

bool isClientReady() {
    if (!client || !client.connected()) return false;
    return !requiresAuth() || authenticated;
}

String getClientIP() {
    if (!client || !client.connected()) return String();
    return client.remoteIP().toString();
}

void write(const uint8_t* data, size_t len) {
    if (!client || !client.connected()) return;
    client.write(data, len);
}

} // namespace TCPServer
