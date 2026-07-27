#include "w5100s_http_server.h"

#include <cstring>

namespace W5100sHttpServer {

RouteAction classifyRoute(HttpRequest::Method method, const char* route) {
    if (!route) return RouteAction::NOT_FOUND;
    if (method == HttpRequest::Method::GET) {
        if (std::strcmp(route, "/") == 0) return RouteAction::ROOT_HTML;
        if (std::strcmp(route, "/stats") == 0) return RouteAction::STATS_HTML;
        if (std::strcmp(route, "/api/temp") == 0) return RouteAction::TEMP_JSON;
        if (std::strcmp(route, "/api/system") == 0) return RouteAction::SYSTEM_JSON;
        if (std::strcmp(route, "/api/radio") == 0) return RouteAction::RADIO_JSON;
        if (std::strcmp(route, "/api/network") == 0) return RouteAction::NETWORK_JSON;
        if (std::strcmp(route, "/api/stats") == 0) return RouteAction::STATS_JSON;
        if (std::strcmp(route, "/api/config") == 0) return RouteAction::CONFIG_JSON;
        if (std::strcmp(route, "/api/gps") == 0) return RouteAction::GPS_JSON;
        return RouteAction::NOT_FOUND;
    }
    if (method == HttpRequest::Method::POST) {
        // OTA is intentionally absent until a compatible, verified bootloader
        // and staged-image handoff exist. Task 12 owns all mutating forms/APIs.
        if (std::strcmp(route, "/update") == 0) return RouteAction::NOT_FOUND;
        static const char* const managementRoutes[] = {
            "/api/config", "/api/reboot", "/reboot", "/hostname",
            "/network", "/token", "/auth", "/gps",
        };
        for (const char* management : managementRoutes) {
            if (std::strcmp(route, management) == 0) return RouteAction::MANAGEMENT_POST;
        }
    }
    return RouteAction::NOT_FOUND;
}

}  // namespace W5100sHttpServer

#if defined(PYMC_ETHERNET_W5100S)

#include "board_config.h"
#include "bootloader_manager.h"
#include "gps_manager.h"
#include "net_filter.h"
#include "rak4631_config.h"
#include "rak4631_web_handlers.h"
#include "runtime_stats.h"
#include "w5100s_ethernet_transport.h"
#include "webui_shared.h"

#include <Arduino.h>
#include <RAK13800_W5100S.h>
#include <w5100.h>
#include <algorithm>
#include <cstdio>
#include <string>

namespace W5100sHttpServer {
namespace {

constexpr uint16_t HTTP_PORT = 80;
constexpr uint32_t READ_TIMEOUT_MS = 3000;
constexpr uint32_t REQUEST_DEADLINE_MS = 5000;
constexpr uint32_t RESPONSE_DEADLINE_MS = 5000;
constexpr size_t RESPONSE_CHUNK_BYTES = 256;
constexpr size_t MAX_RESPONSE_BODY_BYTES = 16384;
constexpr const char* AUTH_USER = "admin";
constexpr const char* DEFAULT_HTTP_PASSWORD = "password";
constexpr const char* OTA_DISABLED_REASON =
    "Stock RAK4631/Adafruit nRF52 bootloader staged-image installation is "
    "incompatible or unverified; Ethernet OTA remains disabled. Use the "
    "board's USB serial DFU bootloader recovery path.";

enum class ClientState : uint8_t { IDLE, READING, READY, WRITING, CLOSING };
enum class SendState : uint8_t { IDLE, COMMAND_PENDING, ACK_PENDING };

EthernetServer server(HTTP_PORT);
bool serverActive = false;
EthernetClient client;
HttpRequest::Parser parser;
ClientState clientState = ClientState::IDLE;
bool lastIpUsable = false;
char responseHeader[320] = {};
size_t responseHeaderLength = 0;
size_t responseHeaderOffset = 0;
std::string responseBody;
size_t responseBodyOffset = 0;
uint32_t responseStartedMs = 0;
uint32_t requestStartedMs = 0;
SendState sendState = SendState::IDLE;
bool sendHeaderPhase = false;
size_t sendLength = 0;
BootloaderManager::DeferredTransition deferredTransition;
Rak4631Config::Config managementConfig{};

std::string ipString(const IPAddress& ip) {
    if (ip == IPAddress(static_cast<uint32_t>(0))) return {};
    char text[16];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return text;
}

std::string macString(const uint8_t mac[6]) {
    char text[18];
    std::snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return text;
}

const char* linkString(EthernetManager::LinkState state) {
    switch (state) {
        case EthernetManager::LinkState::UP: return "up";
        case EthernetManager::LinkState::DOWN: return "down";
        default: return "unknown";
    }
}

const char* radioState(const RuntimeStats::Snapshot& snap) {
    if (snap.radioStandby) return "Standby";
    if (snap.status.radio_state == 1) return "TX";
    if (snap.status.radio_state == 2) return "Error";
    return "RX/Idle";
}

WebUiShared::Model buildModel() {
    const Rak4631Config::Config& cfg = Rak4631Config::getConfig();
    const EthernetManager::Snapshot ethernet = EthernetManager::getSnapshot();
    const RuntimeStats::Snapshot runtime = RuntimeStats::capture();
    const GPSManager::Snapshot gps = GPSManager::snapshot();

    WebUiShared::Model model;
    model.board = BOARD.name;
    model.firmware = runtime.firmwareVersion.c_str();
    model.hostname = Rak4631Config::getEffectiveHostname();
    model.connectedClientIp = TCPServer::getClientIP().c_str();
    model.uptimeSec = runtime.status.uptime_sec;
    model.dieTemperatureC = runtime.status.temp_c;
    model.capabilities.ethernet = true;
    model.capabilities.battery = BOARD.battery.pin >= 0;
    model.capabilities.gps = GPSManager::hasGpsPins();
    model.capabilities.radio = BOARD.has_lora_radio;
    model.capabilities.updateAvailable = false;
    model.capabilities.httpFirmwareUpload = false;
    model.capabilities.writableManagement = true;
    // The exact installed bootloader must produce a visible BLE DFU target before
    // this recovery control and route may be exposed.
    model.capabilities.bleDfu = false;
    model.updateUnavailableReason = OTA_DISABLED_REASON;

    model.network.interfaceName = "Ethernet";
    model.network.live = ethernet.hasIP;
    model.network.currentIp = ipString(ethernet.localIP);
    model.network.subnet = ipString(ethernet.subnet);
    model.network.gateway = ipString(ethernet.gateway);
    model.network.dns1 = ipString(ethernet.dns1);
    model.network.dns2 = ipString(ethernet.dns2);
    model.network.linkState = linkString(ethernet.linkState);
    model.network.mac = macString(ethernet.mac);
    model.network.tcpStatus = model.connectedClientIp.empty() ? "listening" : "connected";

    model.config.hostname = cfg.hostname;
    model.config.useStaticIp = cfg.useStaticIP;
    model.config.staticIp = ipString(IPAddress(cfg.staticIP.octets[0], cfg.staticIP.octets[1],
                                               cfg.staticIP.octets[2], cfg.staticIP.octets[3]));
    model.config.subnet = ipString(IPAddress(cfg.subnet.octets[0], cfg.subnet.octets[1],
                                             cfg.subnet.octets[2], cfg.subnet.octets[3]));
    model.config.gateway = ipString(IPAddress(cfg.gateway.octets[0], cfg.gateway.octets[1],
                                              cfg.gateway.octets[2], cfg.gateway.octets[3]));
    model.config.dns1 = ipString(IPAddress(cfg.dns1.octets[0], cfg.dns1.octets[1],
                                           cfg.dns1.octets[2], cfg.dns1.octets[3]));
    model.config.dns2 = ipString(IPAddress(cfg.dns2.octets[0], cfg.dns2.octets[1],
                                           cfg.dns2.octets[2], cfg.dns2.octets[3]));
    model.config.tcpPort = cfg.tcpPort;
    model.config.tcpTokenSet = cfg.tcpToken[0] != '\0';
    model.config.gpsEnabled = cfg.gpsEnabled;

    model.battery.available = model.capabilities.battery;
    model.battery.voltageValid = runtime.status.battery_mv != 0xffff;
    model.battery.voltageMv = model.battery.voltageValid ? runtime.status.battery_mv : 0;

    model.gps.available = gps.available;
    model.gps.enabled = gps.enabled;
    model.gps.seen = gps.seen;
    model.gps.fixValid = gps.fixValid;
    model.gps.fixQuality = gps.fixQuality;
    model.gps.satellitesUsed = gps.satellitesUsed;
    model.gps.satellitesInView = gps.satellitesInViewCount;
    model.gps.latitude = gps.latitude;
    model.gps.longitude = gps.longitude;
    model.gps.altitudeValid = gps.hasAltitude;
    model.gps.altitudeM = gps.altitudeM;
    model.gps.speedValid = gps.hasSpeed;
    model.gps.speedKmh = gps.speedKmh;
    model.gps.courseValid = gps.hasCourse;
    model.gps.courseDegrees = gps.courseDegrees;
    model.gps.utcTime = gps.utcTime.c_str();
    model.gps.date = gps.date.c_str();
    model.gps.datetimeUtc = gps.datetimeUtc.c_str();
    model.gps.lastSentenceType = gps.lastSentenceType.c_str();
    model.gps.validSentenceCount = gps.validSentenceCount;
    model.gps.invalidChecksumCount = gps.invalidChecksumCount;
    model.gps.rawByteCount = gps.rawByteCount;
    model.gps.configCommandCount = gps.configCommandCount;
    model.gps.uartRxPin = BOARD.pin_gps_uart_rx;
    model.gps.uartTxPin = BOARD.pin_gps_uart_tx;
    model.gps.uartBaud = BOARD.gps_uart_baud;
    model.gps.enablePin = BOARD.pin_gps_enable;
    model.gps.resetPin = BOARD.pin_gps_reset;
    model.gps.ageValid = gps.lastUpdateMs != 0;
    model.gps.ageMs = model.gps.ageValid ? static_cast<uint32_t>(millis() - gps.lastUpdateMs) : 0;
    for (uint8_t i = 0; i < gps.satellitesInViewStored; ++i) {
        WebUiShared::GpsSatelliteModel sat;
        sat.prn = gps.satellitesInView[i].prn.c_str();
        sat.elevationValid = gps.satellitesInView[i].elevationDegrees >= 0;
        sat.elevationDegrees = gps.satellitesInView[i].elevationDegrees;
        sat.azimuthValid = gps.satellitesInView[i].azimuthDegrees >= 0;
        sat.azimuthDegrees = gps.satellitesInView[i].azimuthDegrees;
        sat.snrValid = gps.satellitesInView[i].hasSnr;
        sat.snrDb = gps.satellitesInView[i].snrDb;
        model.gps.satellites.push_back(sat);
    }

    model.radio.available = BOARD.has_lora_radio;
    model.radio.state = radioState(runtime);
    model.radio.standby = runtime.radioStandby;
    model.radio.autoCadEnabled = runtime.autoCadEnabled;
    model.radio.frequencyHz = runtime.radio.freq_hz;
    model.radio.bandwidthHz = runtime.radio.bandwidth_hz;
    model.radio.spreadingFactor = runtime.radio.sf;
    model.radio.codingRate = runtime.radio.cr;
    model.radio.txPowerDbm = runtime.radio.power_dbm;
    model.radio.syncword = runtime.radio.syncword;
    model.radio.preambleLength = runtime.radio.preamble_len;
    model.counters.rxPackets = runtime.status.rx_count;
    model.counters.txPackets = runtime.status.tx_count;
    model.counters.crcErrors = runtime.status.crc_errors;
    model.counters.lastRssiDbm = runtime.status.last_rssi;
    model.counters.lastSnrDb = runtime.status.last_snr / 10.0f;
    model.counters.noiseFloorDbm = runtime.status.noise_floor_x10 / 10.0f;
    return model;
}

void closeListenerSockets() {
    W5100.getSPI()->beginTransaction(SPI_ETHERNET_SETTINGS);
    for (uint8_t socket = 0; socket < MAX_SOCK_NUM; ++socket) {
        if (EthernetServer::server_port[socket] != HTTP_PORT) continue;
        W5100.writeSnCR(socket, Sock_CLOSE);
        EthernetServer::server_port[socket] = 0;
    }
    W5100.getSPI()->endTransaction();
}

void clearClientState(bool responseCompleted) {
    client = EthernetClient();
    clientState = ClientState::IDLE;
    sendState = SendState::IDLE;
    sendLength = 0;
    responseBody.clear();
    responseHeaderLength = responseHeaderOffset = responseBodyOffset = 0;
    if (responseCompleted) deferredTransition.responseClosed(millis());
    else deferredTransition.responseAborted();
}

void closeClient(bool responseCompleted = false) {
    const uint8_t socket = client.getSocketNumber();
    if (socket < MAX_SOCK_NUM) {
        // EthernetClient::stop() waits for up to its connection timeout and
        // uses blocking socket commands. Issue a bounded local close command
        // and release the C++ handle immediately instead.
        W5100.getSPI()->beginTransaction(SPI_ETHERNET_SETTINGS);
        W5100.writeSnCR(socket, Sock_CLOSE);
        W5100.getSPI()->endTransaction();
    }
    if (responseCompleted && socket < MAX_SOCK_NUM) {
        clientState = ClientState::CLOSING;
        sendState = SendState::IDLE;
        sendLength = 0;
        return;
    }
    clearClientState(false);
}

const char* reasonPhrase(int status) {
    switch (status) {
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "OK";
    }
}

void queueResponse(int status, const char* contentType, std::string body,
                   bool authenticate = false) {
    if (body.size() > MAX_RESPONSE_BODY_BYTES) {
        status = 500;
        contentType = "application/json; charset=utf-8";
        body = "{\"error\":\"response too large\"}";
        authenticate = false;
    }
    responseBody = std::move(body);
    const char* challenge = authenticate
        ? "WWW-Authenticate: Basic realm=\"openHop Modem\"\r\n" : "";
    const int length = std::snprintf(
        responseHeader, sizeof(responseHeader),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
        "Connection: close\r\n%sCache-Control: no-store\r\n\r\n",
        status, reasonPhrase(status), contentType,
        static_cast<unsigned long>(responseBody.size()), challenge);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(responseHeader)) {
        closeClient();
        return;
    }
    responseHeaderLength = static_cast<size_t>(length);
    responseHeaderOffset = responseBodyOffset = 0;
    responseStartedMs = millis();
    clientState = ClientState::WRITING;
}

void queueError(int status, const char* error, bool authenticate = false) {
    std::string body = "{\"error\":\"";
    body += error;
    body += "\"}";
    queueResponse(status, "application/json; charset=utf-8", std::move(body), authenticate);
}

bool saveManagementConfig(const Rak4631Config::Config& config, void*) {
    if (!Rak4631Config::saveConfig(config)) return false;
    managementConfig = config;
    return true;
}

void armTransition(Rak4631WebHandlers::Transition transition) {
    if (transition == Rak4631WebHandlers::Transition::BLE_DFU)
        deferredTransition.arm(BootloaderManager::Mode::BLE_OTA);
    else if (transition == Rak4631WebHandlers::Transition::REBOOT)
        deferredTransition.arm(BootloaderManager::Mode::REBOOT);
}

void dispatch() {
    const char* password = managementConfig.httpPassword;
    if (!password[0]) password = DEFAULT_HTTP_PASSWORD;
    if (!HttpRequest::basicAuthMatches(parser.header("Authorization"), AUTH_USER, password)) {
        queueError(401, "authentication required", true);
        return;
    }

    const RouteAction action = classifyRoute(parser.request().method, parser.request().route);
    if (action == RouteAction::NOT_FOUND) {
        queueError(404, "not found");
        return;
    }
    if (action == RouteAction::MANAGEMENT_POST) {
        Rak4631WebHandlers::Request request;
        request.route = parser.request().route;
        request.contentType = parser.header("Content-Type");
        request.body = parser.request().body;
        request.bodyLength = parser.request().bodyLength;
        request.origin = parser.header("Origin");
        request.host = parser.header("Host");
        request.current = managementConfig;
        request.gpsSupported = GPSManager::hasGpsPins();
        const Rak4631WebHandlers::Response response =
            Rak4631WebHandlers::handlePost(request, saveManagementConfig, nullptr);
        queueResponse(response.status, response.contentType.c_str(), response.body);
        if (clientState == ClientState::WRITING) armTransition(response.transition);
        return;
    }

    const WebUiShared::Model model = buildModel();
    switch (action) {
        case RouteAction::ROOT_HTML:
            queueResponse(200, "text/html; charset=utf-8", WebUiShared::renderRootPage(model)); break;
        case RouteAction::STATS_HTML:
            queueResponse(200, "text/html; charset=utf-8", WebUiShared::renderStatsPage(model)); break;
        case RouteAction::TEMP_JSON:
            queueResponse(200, "application/json; charset=utf-8", WebUiShared::renderTempJson(model)); break;
        case RouteAction::SYSTEM_JSON:
            queueResponse(200, "application/json; charset=utf-8", WebUiShared::renderSystemJson(model)); break;
        case RouteAction::RADIO_JSON:
            queueResponse(200, "application/json; charset=utf-8", WebUiShared::renderRadioJson(model)); break;
        case RouteAction::NETWORK_JSON:
            queueResponse(200, "application/json; charset=utf-8", WebUiShared::renderNetworkJson(model)); break;
        case RouteAction::STATS_JSON:
            queueResponse(200, "application/json; charset=utf-8", WebUiShared::renderStatsJson(model)); break;
        case RouteAction::CONFIG_JSON:
            queueResponse(200, "application/json; charset=utf-8", WebUiShared::renderConfigJson(model)); break;
        case RouteAction::GPS_JSON:
            queueResponse(200, "application/json; charset=utf-8", WebUiShared::renderGpsJson(model)); break;
        default: queueError(404, "not found"); break;
    }
}

void writeOneChunk() {
    if (!client || !client.connected()) {
        closeClient();
        return;
    }
    if (static_cast<uint32_t>(millis() - responseStartedMs) >= RESPONSE_DEADLINE_MS) {
        closeClient();
        return;
    }
    const uint8_t socket = client.getSocketNumber();
    if (socket >= MAX_SOCK_NUM) {
        closeClient();
        return;
    }

    // Never call EthernetClient::write(): the pinned RAK driver waits without
    // a timeout for both TX space and SEND_OK. Drive the W5100S send command
    // as a small state machine instead, polling at most once per loop.
    if (sendState != SendState::IDLE) {
        W5100.getSPI()->beginTransaction(SPI_ETHERNET_SETTINGS);
        const uint8_t status = W5100.readSnSR(socket);
        if (status != SnSR::ESTABLISHED && status != SnSR::CLOSE_WAIT) {
            W5100.getSPI()->endTransaction();
            closeClient();
            return;
        }
        if (sendState == SendState::COMMAND_PENDING) {
            if (W5100.readSnCR(socket) == 0) sendState = SendState::ACK_PENDING;
            W5100.getSPI()->endTransaction();
            return;
        }
        const uint8_t interrupts = W5100.readSnIR(socket);
        if (interrupts & SnIR::TIMEOUT) {
            W5100.writeSnIR(socket, SnIR::TIMEOUT);
            W5100.getSPI()->endTransaction();
            closeClient();
            return;
        }
        if (!(interrupts & SnIR::SEND_OK)) {
            W5100.getSPI()->endTransaction();
            return;
        }
        W5100.writeSnIR(socket, SnIR::SEND_OK);
        W5100.getSPI()->endTransaction();
        if (sendHeaderPhase) responseHeaderOffset += sendLength;
        else responseBodyOffset += sendLength;
        sendState = SendState::IDLE;
        sendLength = 0;
        if (responseHeaderOffset == responseHeaderLength &&
            responseBodyOffset == responseBody.size()) closeClient(true);
        return;
    }

    const uint8_t* data = nullptr;
    size_t remaining = 0;
    sendHeaderPhase = responseHeaderOffset < responseHeaderLength;
    if (sendHeaderPhase) {
        data = reinterpret_cast<const uint8_t*>(responseHeader + responseHeaderOffset);
        remaining = responseHeaderLength - responseHeaderOffset;
    } else {
        data = reinterpret_cast<const uint8_t*>(responseBody.data() + responseBodyOffset);
        remaining = responseBody.size() - responseBodyOffset;
    }
    if (remaining == 0) {
        closeClient();
        return;
    }
    const size_t amount = std::min(remaining, RESPONSE_CHUNK_BYTES);

    W5100.getSPI()->beginTransaction(SPI_ETHERNET_SETTINGS);
    const uint8_t status = W5100.readSnSR(socket);
    const uint16_t freeBytes = W5100.readSnTX_FSR(socket);
    if ((status != SnSR::ESTABLISHED && status != SnSR::CLOSE_WAIT) ||
        freeBytes < amount) {
        W5100.getSPI()->endTransaction();
        if (status != SnSR::ESTABLISHED && status != SnSR::CLOSE_WAIT) closeClient();
        return;
    }
    uint16_t pointer = W5100.readSnTX_WR(socket);
    const uint16_t offset = pointer & W5100.SMASK;
    const uint16_t destination = offset + W5100.SBASE(socket);
    if (W5100.hasOffsetAddressMapping() || offset + amount <= W5100.SSIZE) {
        W5100.write(destination, data, amount);
    } else {
        const uint16_t first = W5100.SSIZE - offset;
        W5100.write(destination, data, first);
        W5100.write(W5100.SBASE(socket), data + first, amount - first);
    }
    pointer += amount;
    W5100.writeSnTX_WR(socket, pointer);
    W5100.writeSnIR(socket, SnIR::SEND_OK | SnIR::TIMEOUT);
    W5100.writeSnCR(socket, Sock_SEND);
    W5100.getSPI()->endTransaction();
    sendLength = amount;
    sendState = SendState::COMMAND_PENDING;
}

void handleParserResult(HttpRequest::Result result) {
    switch (result) {
        case HttpRequest::Result::BAD_REQUEST: queueError(400, "bad request"); break;
        case HttpRequest::Result::METHOD_NOT_ALLOWED: queueError(405, "method not allowed"); break;
        case HttpRequest::Result::PAYLOAD_TOO_LARGE: queueError(413, "payload too large"); break;
        case HttpRequest::Result::TIMED_OUT: queueError(408, "request timeout"); break;
        case HttpRequest::Result::COMPLETE: clientState = ClientState::READY; break;
        default: break;
    }
}

}  // namespace

void begin() {
    end();
    managementConfig = Rak4631Config::getConfig();
    server.begin();
    serverActive = true;
    lastIpUsable = EthernetManager::hasIP();
    Serial.println("[HTTP/ETH] port 80 listener initialized; Ethernet OTA disabled");
}

void end() {
    closeClient();
    if (serverActive) {
        closeListenerSockets();
        serverActive = false;
    }
    lastIpUsable = false;
}

void loop() {
    // A transition is eligible only after the complete response was ACKed and
    // its socket was explicitly closed, then after the bounded grace period.
    deferredTransition.poll(millis(), BootloaderManager::execute, nullptr);
    if (deferredTransition.committed()) return;
    if (!serverActive) return;
    const bool ipUsable = EthernetManager::hasIP();
    if (!ipUsable) {
        if (client) closeClient();
        if (lastIpUsable) closeListenerSockets();
        lastIpUsable = false;
        return;
    }
    if (!lastIpUsable) {
        server.begin();
        lastIpUsable = true;
        Serial.println("[HTTP/ETH] IP restored; port 80 listener restarted");
    }

    if (clientState == ClientState::CLOSING) {
        const uint8_t socket = client.getSocketNumber();
        if (socket >= MAX_SOCK_NUM) {
            clearClientState(false);
            return;
        }
        W5100.getSPI()->beginTransaction(SPI_ETHERNET_SETTINGS);
        const uint8_t command = W5100.readSnCR(socket);
        const uint8_t status = W5100.readSnSR(socket);
        W5100.getSPI()->endTransaction();
        if (command == 0 && status == SnSR::CLOSED) {
            clearClientState(true);
        } else if (static_cast<uint32_t>(millis() - responseStartedMs) >= RESPONSE_DEADLINE_MS) {
            closeClient(false);
        }
        return;
    }
    if (clientState == ClientState::WRITING) {
        writeOneChunk();
        return;
    }
    if (clientState == ClientState::READY) {
        if (client.available() > 0) {
            const int value = client.read();
            if (value >= 0) {
                const uint8_t byte = static_cast<uint8_t>(value);
                handleParserResult(parser.feed(&byte, 1, millis()));
            }
            return;
        }
        dispatch();
        return;
    }
    if (clientState == ClientState::READING) {
        if (!client || !client.connected()) {
            parser.finish();
            closeClient();
            return;
        }
        if (static_cast<uint32_t>(millis() - requestStartedMs) >= REQUEST_DEADLINE_MS) {
            queueError(408, "request deadline exceeded");
            return;
        }
        if (client.available() > 0) {
            const int value = client.read();
            if (value >= 0) {
                const uint8_t byte = static_cast<uint8_t>(value);
                handleParserResult(parser.feed(&byte, 1, millis()));
            }
            return;
        }
        handleParserResult(parser.poll(millis()));
        return;
    }

    EthernetClient incoming = server.accept();
    if (!incoming) return;
    client = incoming;
    if (!isLanAddress(client.remoteIP())) {
        // Source policy is evaluated before parsing Authorization.
        queueError(403, "LAN access only");
        return;
    }
    requestStartedMs = millis();
    parser.reset(requestStartedMs, READ_TIMEOUT_MS);
    clientState = ClientState::READING;
}

}  // namespace W5100sHttpServer

#endif  // PYMC_ETHERNET_W5100S
