// =============================================================
// tcp_server.cpp — bounded, independent per-radio TCP sessions.
// =============================================================
#include "tcp_server.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "protocol.h"
#include "net_filter.h"
#include "bounded_protocol_service.h"
#include <WiFi.h>
#include <new>
#include <lwip/sockets.h>
#include <errno.h>

// Legacy primary-radio adapter until main installs a routed handler.
extern void processHostCommand(uint8_t cmd, const uint8_t* payload,
                               uint16_t len, TransportSource src);
extern void noteTransportFrameError(uint8_t err_code);

namespace TCPServer {

struct OutputFrame {
    uint8_t data[MAX_FRAME_SIZE];
    uint16_t length = 0;
    uint16_t offset = 0;
    uint32_t queuedAt = 0;
};
struct Session {
    WiFiServer* server = nullptr;
    WiFiClient client;
    uint32_t clientLocalIP = 0; // capture before getsockname loses an old address
    bool authenticated = false;
    FrameParser parser;
    // Never reset the generation in end()/beginRadios(): deferred work may
    // outlive either a disconnected socket or a restarted listener bank.
    uint32_t generation = 0;
    uint32_t frameCount = 0;
    uint32_t parsedFrameCount = 0;
    uint8_t radio = 0;
    OutputFrame output[OUTPUT_QUEUE_FRAMES];
    size_t outputHead = 0, outputCount = 0;
    bool closing = false;
    bool servicing = false, outputAttempted = false;
};
static Session sessions[MAX_RADIOS];
static size_t listenerCount = 0;
static size_t nextStart = 0;
static String requiredToken;
static RadioCommandHandler commandHandler = nullptr;
static RadioFrameErrorHandler frameErrorHandler = nullptr;
void setRadioFrameErrorHandler(RadioFrameErrorHandler handler) { frameErrorHandler = handler; }
static constexpr size_t MAX_BYTES_PER_LOOP = 256;
static constexpr uint8_t MAX_FRAMES_PER_LOOP = 1;

static bool requiresAuth() { return requiredToken.length() > 0; }

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
    const uint16_t crc = crc16_ccitt(buf + 1, 3 + len);
    buf[i++] = crc & 0xFF;
    buf[i++] = (crc >> 8) & 0xFF;
    outLen = i;
}

static void disconnectClient(Session& session);
static bool outputExpired(const Session& session) {
    // Absolute age, not an inactivity timeout: trickling partial writes cannot
    // keep a session congested forever. Each tail retains its own enqueue time.
    return session.outputCount &&
        static_cast<uint32_t>(millis() - session.output[session.outputHead].queuedAt) >=
            OUTPUT_CONGESTION_TIMEOUT_MS;
}

// One syscall, never NetworkClient::write(): the latter retries one-second
// select() waits (and restarts its retry count on partial progress). Retain a
// frame's unsent suffix; a later frame must never overtake it.
static void serviceOutput(Session& session) {
    if (outputExpired(session)) { disconnectClient(session); return; }
    if (!session.outputCount || (session.servicing && session.outputAttempted)) return;
    const int descriptor = session.client.fd();
    if (descriptor < 0) { disconnectClient(session); return; }
    auto& frame = session.output[session.outputHead];
    session.outputAttempted = true;
    const int sent = ::send(descriptor, frame.data + frame.offset,
                            frame.length - frame.offset, MSG_DONTWAIT);
    if (sent > 0) {
        frame.offset += static_cast<uint16_t>(sent);
        if (frame.offset == frame.length) {
            session.outputHead = (session.outputHead + 1) % OUTPUT_QUEUE_FRAMES;
            --session.outputCount;
            if (!session.outputCount && session.closing) disconnectClient(session);
        }
    } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
               errno != EINTR && errno != ENOBUFS && errno != ENOMEM) {
        disconnectClient(session);
    }
}
static bool outputHasRoom(const Session& session) {
    return !session.closing && !outputExpired(session) && session.outputCount < OUTPUT_QUEUE_FRAMES;
}
static void queueOutput(Session& session, const uint8_t* data, size_t len) {
    if (!session.client || !session.client.connected() || !len) return;
    // The void writers cannot report an overflow to a caller. Close explicitly,
    // rather than silently lose a command completion or continue a torn frame.
    if (!data || len > MAX_FRAME_SIZE || outputExpired(session) ||
        session.outputCount == OUTPUT_QUEUE_FRAMES) {
        disconnectClient(session); return;
    }
    auto& frame = session.output[(session.outputHead + session.outputCount) % OUTPUT_QUEUE_FRAMES];
    memcpy(frame.data, data, len);
    frame.length = static_cast<uint16_t>(len); frame.offset = 0; frame.queuedAt = millis();
    ++session.outputCount;
    if (session.outputCount == 1) serviceOutput(session);
}

// Protocol/auth errors must reach the requesting socket even before auth.
// Application replies and events use the separately gated public writers.
static void sendToClient(Session& session, uint8_t cmd, const uint8_t* payload, uint16_t len) {
    if (!session.client || !session.client.connected()) return;
    uint8_t buf[MAX_FRAME_SIZE]; uint16_t size = 0;
    buildFrame(buf, size, cmd, payload, len);
    queueOutput(session, buf, size);
}
static void sendErrorToClient(Session& session, uint8_t error) {
    sendToClient(session, CMD_ERROR, &error, 1);
}
static void disconnectClient(Session& session) {
    if (session.client) session.client.stop();
    session.client = WiFiClient();
    session.authenticated = false;
    session.closing = false;
    session.frameCount = 0;
    session.clientLocalIP = 0;
    session.parser.reset();
    session.outputHead = session.outputCount = 0;
}
static void onFrameOk(uint8_t cmd, const uint8_t* payload, uint16_t len,
                      TransportSource, void* context) {
    auto& session = *static_cast<Session*>(context);
    ++session.parsedFrameCount;
    ++session.frameCount;
    if (requiresAuth() && !session.authenticated) {
        if (cmd == CMD_AUTH && len == requiredToken.length() &&
            memcmp(payload, requiredToken.c_str(), len) == 0) {
            session.authenticated = true;
            sendToClient(session, CMD_AUTH_OK, nullptr, 0);
        } else {
            // Never dispatch more input after failure, even while the error's
            // suffix waits for the socket. Close on drain or the same deadline.
            session.authenticated = false;
            session.closing = true;
            sendErrorToClient(session, ERR_UNAUTHORIZED);
        }
        return;
    }
    if (cmd == CMD_AUTH) {
        sendToClient(session, CMD_AUTH_OK, nullptr, 0);
        return;
    }
    const ResponseRoute route{TransportSource::TCP, session.radio, session.generation};
    if (commandHandler && isRouteCurrent(route)) commandHandler(cmd, payload, len, route);
}
static void onFrameErr(uint8_t error, TransportSource, void* context) {
    auto& session = *static_cast<Session*>(context);
    ++session.parsedFrameCount; // malformed input also consumes the frame quota
    if (frameErrorHandler) frameErrorHandler(session.radio, error);
    else noteTransportFrameError(error);
    sendErrorToClient(session, error);
}

bool beginRadios(const uint16_t* ports, size_t count, const String& token,
                 RadioCommandHandler handler) {
    if (!ports || count == 0 || count > MAX_RADIOS || !handler) return false;
    // Validate the whole bank before allocating or disturbing live sessions.
    for (size_t i = 0; i < count; ++i) {
        if (ports[i] == 0 || ports[i] == 80) return false;
        for (size_t j = 0; j < i; ++j) if (ports[i] == ports[j]) return false;
    }
    WiFiServer* pending[MAX_RADIOS] = {};
    for (size_t i = 0; i < count; ++i) {
        pending[i] = new (std::nothrow) WiFiServer(ports[i]);
        if (!pending[i]) {
            for (size_t j = 0; j < i; ++j) delete pending[j];
            return false;
        }
    }
    end();
    requiredToken = token;
    commandHandler = handler;
    listenerCount = count;
    for (size_t i = 0; i < count; ++i) {
        auto& session = sessions[i];
        session.radio = static_cast<uint8_t>(i);
        session.server = pending[i];
        session.server->begin();
        session.server->setNoDelay(true);
    }
    return true;
}
static void legacyCommand(uint8_t cmd, const uint8_t* payload, uint16_t len,
                          const ResponseRoute&) {
    processHostCommand(cmd, payload, len, TransportSource::TCP);
}
void begin(uint16_t port, const String& token) {
    (void)beginRadios(&port, 1, token, legacyCommand);
}
void end() {
    for (auto& session : sessions) {
        disconnectClient(session);
        if (session.server) {
            session.server->end();
            delete session.server;
            session.server = nullptr;
        }
    }
    listenerCount = 0;
    nextStart = 0;
    commandHandler = nullptr;
}
void invalidateInterface(const IPAddress& address) {
    for (size_t i = 0; i < listenerCount; ++i) {
        auto& session = sessions[i];
        if (session.client && session.clientLocalIP == static_cast<uint32_t>(address))
            disconnectClient(session);
    }
}
static void serviceSession(Session& session) {
    if (!session.server) return;
    if (!session.client || !session.client.connected()) {
        disconnectClient(session);
        WiFiClient incoming = session.server->available();
        if (incoming) {
            if (!isLanAddress(incoming.remoteIP())) {
                incoming.stop();
                return;
            }
            // Fail closed rather than recycle a stale deferred-work identity.
            if (session.generation == UINT32_MAX) {
                incoming.stop();
                return;
            }
            session.client = incoming;
            ++session.generation;
            session.clientLocalIP = static_cast<uint32_t>(session.client.localIP());
            session.client.setNoDelay(true);
        }
    }
    if (session.client && session.client.connected()) {
        // Snapshot admission before draining: an owner's deferred completion
        // must get the newly freed slot before another request is dispatched.
        const bool acceptInput = outputHasRoom(session);
        session.servicing = true; session.outputAttempted = false;
        serviceOutput(session);
        if (acceptInput && session.client && session.client.connected()) {
            serviceBoundedProtocolInput(session.client, MAX_BYTES_PER_LOOP, MAX_FRAMES_PER_LOOP,
                [&session](uint8_t byte) {
                    const uint32_t previous = session.parsedFrameCount;
                    frameparser_feed(session.parser, byte, TransportSource::TCP,
                                     onFrameOk, onFrameErr, &session);
                    return session.parsedFrameCount != previous;
                });
        }
        session.servicing = false;
    }
}
void loop() {
    if (!listenerCount) return;
    const size_t count = listenerCount;
    const size_t start = nextStart;
    nextStart = (nextStart + 1) % count;
    for (size_t i = 0; i < count; ++i) serviceSession(sessions[(start + i) % count]);
}
bool isClientReady(uint8_t radio) {
    if (radio >= listenerCount) return false;
    auto& session = sessions[radio];
    return session.client && session.client.connected() && !session.closing &&
           (!requiresAuth() || session.authenticated);
}
bool isClientReady() { return isClientReady(0); }
uint32_t connectionGeneration() { return isClientReady() ? sessions[0].generation : 0; }
bool isConnectionCurrent(uint32_t generation) {
    return isRouteCurrent({TransportSource::TCP, 0, generation});
}
bool isRouteCurrent(const ResponseRoute& route) {
    return route.transport == TransportSource::TCP && route.generation != 0 &&
           isClientReady(route.radio) && sessions[route.radio].generation == route.generation;
}
bool canQueueResponse() {
    return isClientReady() && outputHasRoom(sessions[0]);
}
bool canQueueResponse(const ResponseRoute& route) {
    return isRouteCurrent(route) && outputHasRoom(sessions[route.radio]);
}
String getClientIP(uint8_t radio) {
    if (radio >= listenerCount) return String();
    auto& session = sessions[radio];
    if (!session.client || !session.client.connected()) return String();
    return session.client.remoteIP().toString();
}
String getClientIP() { return getClientIP(0); }
void write(const uint8_t* data, size_t len) {
    // Preserve the primary-only legacy raw writer; callers gate async writes.
    if (listenerCount && !sessions[0].closing) queueOutput(sessions[0], data, len);
}
void writeRadioEvent(uint8_t radio, const uint8_t* data, size_t len) {
    if (isClientReady(radio)) queueOutput(sessions[radio], data, len);
}
void writeTo(const ResponseRoute& route, const uint8_t* data, size_t len) {
    if (isRouteCurrent(route)) queueOutput(sessions[route.radio], data, len);
}

} // namespace TCPServer
#endif // ARDUINO_ARCH_ESP32
