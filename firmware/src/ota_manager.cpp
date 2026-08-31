// =============================================================
// ota_manager.cpp — OTA via ArduinoOTA + HTTP /update, with
// dual-bank rollback guarded by a sanity watchdog.
// =============================================================
#include "ota_manager.h"
#include "board_config.h"
#include "ethernet_manager.h"
#include "gps_manager.h"
#include "net_filter.h"
#include "rf_frontend.h"
#include "runtime_stats.h"
#include "tcp_server.h"
#include "wifi_manager.h"
#include "webui_shared.h"
#if defined(BOARD_STATION_G3)
#include "station_g3_power.h"
#endif

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <ETH.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

namespace OTAManager {

static constexpr uint32_t SANITY_TIMEOUT_MS = 120000;  // mark firmware valid after 2 min of health
static constexpr uint16_t HTTP_PORT         = 80;
static constexpr const char* NVS_NAMESPACE  = "lora_modem";
static constexpr const char* HTTP_PASS_KEY  = "http_pass";
static constexpr const char* HTTP_AUTH_USER = "admin";
static constexpr const char* DEFAULT_HTTP_PASSWORD = "openhop";
static constexpr uint8_t MAX_HTTP_PASSWORD_LEN = 64;
static constexpr uint8_t MAX_TCP_TOKEN_LEN = 64;

static String      hostname;
static String      token;
static String      httpPassword;
static WebServer*  httpServer       = nullptr;
static bool        started          = false;
static bool        markedValid      = false;
static uint32_t    sanityDeadline   = 0;
static bool        sawValidFrame    = false;

static String modemTitle() {
    return String(BOARD.name) + " openHop Modem";
}

static String currentIPString() {
    if (EthernetManager::hasIP()) return String(EthernetManager::getIPString());
    return WiFi.localIP().toString();
}

static void loadHttpPassword() {
    httpPassword = DEFAULT_HTTP_PASSWORD;
    Preferences p;
    if (p.begin(NVS_NAMESPACE, true)) {
        httpPassword = p.getString(HTTP_PASS_KEY, DEFAULT_HTTP_PASSWORD);
        p.end();
    }
    if (httpPassword.length() == 0) {
        httpPassword = DEFAULT_HTTP_PASSWORD;
    }
}

static bool saveHttpPassword(const String& password) {
    Preferences p;
    if (!p.begin(NVS_NAMESPACE, false)) return false;
    size_t written = p.putString(HTTP_PASS_KEY, password);
    p.end();
    if (written == 0) return false;
    httpPassword = password;
    return true;
}

static void sendSimplePage(const __FlashStringHelper* title,
                           const __FlashStringHelper* heading,
                           const __FlashStringHelper* message) {
    String messageStr(message);
    String body = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<title>");
    body += String(title);
    body += F("</title></head>"
              "<body style='font-family:system-ui,sans-serif;max-width:540px;"
              "margin:2em auto;padding:0 1em;color:#222'>"
              "<h2>");
    body += String(heading);
    body += F("</h2><p>");
    body += messageStr;
    body += F("</p><p><a href='/'>Back to OTA page</a></p></body></html>");
    httpServer->send(200, "text/html; charset=utf-8", body);
}

static void sendSimplePage(const __FlashStringHelper* title,
                           const __FlashStringHelper* heading,
                           const String& message) {
    String body = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<title>");
    body += String(title);
    body += F("</title></head>"
              "<body style='font-family:system-ui,sans-serif;max-width:540px;"
              "margin:2em auto;padding:0 1em;color:#222'>"
              "<h2>");
    body += String(heading);
    body += F("</h2><p>");
    body += message;
    body += F("</p><p><a href='/'>Back to OTA page</a></p></body></html>");
    httpServer->send(200, "text/html; charset=utf-8", body);
}

static IPAddress parseIPArg(const String& s) {
    IPAddress ip;
    if (!ip.fromString(s)) ip = IPAddress((uint32_t)0);
    return ip;
}

struct NetworkSnapshot {
    const char* iface = "Offline";
    bool live = false;
    bool has_wifi_rssi = false;
    int32_t wifi_rssi_dbm = 0;
    IPAddress ip;
    IPAddress subnet;
    IPAddress gateway;
    IPAddress dns1;
    IPAddress dns2;
};

static NetworkSnapshot getNetworkSnapshot() {
    NetworkSnapshot snap;
    if (EthernetManager::hasIP()) {
        snap.iface = "Ethernet";
        snap.live = true;
        snap.ip = ETH.localIP();
        snap.subnet = ETH.subnetMask();
        snap.gateway = ETH.gatewayIP();
        snap.dns1 = ETH.dnsIP(0);
        snap.dns2 = ETH.dnsIP(1);
        return snap;
    }
    if (WifiManager::isSTAConnected()) {
        snap.iface = "Wi-Fi";
        snap.live = true;
        snap.ip = WiFi.localIP();
        snap.subnet = WiFi.subnetMask();
        snap.gateway = WiFi.gatewayIP();
        snap.dns1 = WiFi.dnsIP(0);
        snap.dns2 = WiFi.dnsIP(1);
        snap.has_wifi_rssi = true;
        snap.wifi_rssi_dbm = WiFi.RSSI();
        return snap;
    }
    if (WifiManager::isAPActive()) {
        snap.iface = "Setup AP";
        snap.live = true;
        snap.ip = WiFi.softAPIP();
    }
    return snap;
}

static String ipFieldValue(bool useStatic, const IPAddress& saved, const IPAddress& live) {
    if (useStatic && (uint32_t)saved != 0) return saved.toString();
    if (!useStatic && (uint32_t)live != 0) return live.toString();
    if ((uint32_t)saved != 0) return saved.toString();
    return String();
}

static String formatUptime(uint32_t uptimeSec) {
    uint32_t days = uptimeSec / 86400;
    uint32_t hours = (uptimeSec % 86400) / 3600;
    uint32_t minutes = (uptimeSec % 3600) / 60;
    uint32_t seconds = uptimeSec % 60;
    char buf[32];
    if (days > 0) {
        snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
                 (unsigned long)days, (unsigned long)hours,
                 (unsigned long)minutes, (unsigned long)seconds);
    } else {
        snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                 (unsigned long)hours, (unsigned long)minutes,
                 (unsigned long)seconds);
    }
    return String(buf);
}

static const char* radioStateLabel(const RuntimeStats::Snapshot& snap) {
    if (snap.radioStandby) return "Standby";
    switch (snap.status.radio_state) {
        case 1: return "TX";
        case 2: return "Error";
        default: return "RX/Idle";
    }
}

static WebUiShared::Model buildWebUiModel() {
    const auto& cfg = WifiManager::getConfig();
    const RuntimeStats::Snapshot snap = RuntimeStats::capture();
    const NetworkSnapshot net = getNetworkSnapshot();
    const GPSManager::Snapshot gps = GPSManager::snapshot();

    WebUiShared::Model model;
    model.board = BOARD.name;
    model.firmware = snap.firmwareVersion.c_str();
    model.hostname = hostname.c_str();
    model.connectedClientIp = TCPServer::getClientIP().c_str();
    model.uptimeSec = snap.status.uptime_sec;
    model.dieTemperatureC = snap.status.temp_c;
    model.capabilities.wifi = BOARD.has_wifi;
    model.capabilities.ethernet = BOARD.ethernet.enabled;
    model.capabilities.mdns = true;
    model.capabilities.wifiReset = BOARD.has_wifi;
    model.capabilities.wifiAntennaSelection = WifiManager::hasWifiAntennaSwitch();
    model.capabilities.heltecV43Controls = RFFrontEnd::hasHeltecV43LnaControl();
    model.capabilities.gps = GPSManager::hasGpsPins();
    model.capabilities.battery = BOARD.battery.pin >= 0 || BOARD.battery.fuel_gauge_i2c_addr != 0;
    model.capabilities.radio = true;
    model.capabilities.updateAvailable = true;
    model.capabilities.httpFirmwareUpload = true;
    model.capabilities.writableManagement = true;
    model.capabilities.exposeTcpToken = true;

    model.network.interfaceName = net.iface;
    model.network.live = net.live;
    model.network.currentIp = currentIPString().c_str();
    model.network.subnet = (uint32_t)net.subnet ? net.subnet.toString().c_str() : "";
    model.network.gateway = (uint32_t)net.gateway ? net.gateway.toString().c_str() : "";
    model.network.dns1 = (uint32_t)net.dns1 ? net.dns1.toString().c_str() : "";
    model.network.dns2 = (uint32_t)net.dns2 ? net.dns2.toString().c_str() : "";
    model.network.hasWifiRssi = net.has_wifi_rssi;
    model.network.wifiRssiDbm = net.wifi_rssi_dbm;
    if (model.capabilities.ethernet) {
        model.network.linkState = EthernetManager::isLinkUp() ? "up" : "down";
        model.network.mac = EthernetManager::getMACString();
        model.network.tcpStatus = model.connectedClientIp.empty() ? "listening" : "connected";
    }

    model.config.hostname = cfg.hostname.c_str();
    model.config.useStaticIp = cfg.useStaticIP;
    model.config.staticIp = (uint32_t)cfg.staticIP ? cfg.staticIP.toString().c_str() : "";
    model.config.subnet = (uint32_t)cfg.subnet ? cfg.subnet.toString().c_str() : "";
    model.config.gateway = (uint32_t)cfg.gateway ? cfg.gateway.toString().c_str() : "";
    model.config.dns1 = (uint32_t)cfg.dns1 ? cfg.dns1.toString().c_str() : "";
    model.config.dns2 = (uint32_t)cfg.dns2 ? cfg.dns2.toString().c_str() : "";
    model.config.tcpPort = cfg.tcpPort;
    model.config.tcpTokenSet = cfg.tcpToken.length() > 0;
    model.config.tcpToken = cfg.tcpToken.c_str();
    model.config.wifiExternalAntenna = cfg.wifiExternalAntenna;
    model.config.gpsEnabled = cfg.gpsEnabled;
    model.config.heltecV43ExternalLnaEnabled = RFFrontEnd::isExternalLnaEnabled();
    model.config.heltecV43FemLnaBypassed = RFFrontEnd::isFemLnaBypassed();
    model.config.agcResetIntervalSec = RFFrontEnd::getAgcResetIntervalSec();

    model.battery.available = model.capabilities.battery;
    model.battery.voltageValid = snap.status.battery_mv != 0xFFFF;
    model.battery.voltageMv = model.battery.voltageValid ? snap.status.battery_mv : 0;
    model.battery.chargeRateAvailable = snap.hasBatteryChargeRatePctPerHour;
    model.battery.chargeRateValid = snap.batteryChargeRatePctPerHourValid;
    model.battery.chargeRatePctPerHour = snap.batteryChargeRatePctPerHour;

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
    model.gps.ageMs = model.gps.ageValid ? (uint32_t)(millis() - gps.lastUpdateMs) : 0;
    for (uint8_t i = 0; i < gps.satellitesInViewStored; ++i) {
        WebUiShared::GpsSatelliteModel satellite;
        satellite.prn = gps.satellitesInView[i].prn.c_str();
        satellite.elevationValid = gps.satellitesInView[i].elevationDegrees >= 0;
        satellite.elevationDegrees = gps.satellitesInView[i].elevationDegrees;
        satellite.azimuthValid = gps.satellitesInView[i].azimuthDegrees >= 0;
        satellite.azimuthDegrees = gps.satellitesInView[i].azimuthDegrees;
        satellite.snrValid = gps.satellitesInView[i].hasSnr;
        satellite.snrDb = gps.satellitesInView[i].snrDb;
        model.gps.satellites.push_back(satellite);
    }

    model.radio.available = true;
    model.radio.state = radioStateLabel(snap);
    model.radio.standby = snap.radioStandby;
    model.radio.autoCadEnabled = snap.autoCadEnabled;
    model.radio.frequencyHz = snap.radio.freq_hz;
    model.radio.bandwidthHz = snap.radio.bandwidth_hz;
    model.radio.spreadingFactor = snap.radio.sf;
    model.radio.codingRate = snap.radio.cr;
    model.radio.txPowerDbm = snap.radio.power_dbm;
    model.radio.syncword = snap.radio.syncword;
    model.radio.preambleLength = snap.radio.preamble_len;
    model.counters.rxPackets = snap.status.rx_count;
    model.counters.txPackets = snap.status.tx_count;
    model.counters.crcErrors = snap.status.crc_errors;
    model.counters.lastRssiDbm = snap.status.last_rssi;
    model.counters.lastSnrDb = snap.status.last_snr / 10.0f;
    model.counters.noiseFloorDbm = snap.status.noise_floor_x10 / 10.0f;
    return model;
}

static String htmlEscape(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        switch (value[i]) {
            case '&': out += F("&amp;"); break;
            case '<': out += F("&lt;"); break;
            case '>': out += F("&gt;"); break;
            default: out += value[i]; break;
        }
    }
    return out;
}

static String jsonEscape(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        switch (value[i]) {
            case '\"': out += F("\\\""); break;
            case '\\': out += F("\\\\"); break;
            case '\n': out += F("\\n"); break;
            case '\r': out += F("\\r"); break;
            case '\t': out += F("\\t"); break;
            default: out += value[i]; break;
        }
    }
    return out;
}

static String jsonQuote(const String& value) {
    return String("\"") + jsonEscape(value) + "\"";
}

static String boolJson(bool value) {
    return value ? String("true") : String("false");
}

static String ipJson(const IPAddress& ip) {
    return (uint32_t)ip != 0 ? jsonQuote(ip.toString()) : String("null");
}

static void sendJson(int code, const String& body) {
    httpServer->send(code, "application/json; charset=utf-8", body);
}

static void sendJsonError(int code, const String& message) {
    sendJson(code, String("{\"error\":") + jsonQuote(message) + "}");
}

static String buildSystemJson(const RuntimeStats::Snapshot& snap,
                              const NetworkSnapshot& net,
                              const String& clientIP) {
    String body;
    body.reserve(512);
    body += F("{\"board\":");
    body += jsonQuote(BOARD.name);
    body += F(",\"firmware\":");
    body += jsonQuote(snap.firmwareVersion);
    body += F(",\"hostname\":");
    body += jsonQuote(hostname);
    body += F(",\"mdns\":");
    body += jsonQuote(hostname + ".local");
    body += F(",\"interface\":");
    body += jsonQuote(net.iface);
    body += F(",\"current_ip\":");
    body += jsonQuote(currentIPString());
    body += F(",\"connected_client_ip\":");
    body += clientIP.length() > 0 ? jsonQuote(clientIP) : String("null");
    body += F(",\"uptime_sec\":");
    body += String(snap.status.uptime_sec);
    body += F(",\"uptime\":");
    body += jsonQuote(formatUptime(snap.status.uptime_sec));
    body += F(",\"die_temperature_c\":");
    body += String(snap.status.temp_c);
    body += F(",\"battery_voltage_mv\":");
    body += snap.status.battery_mv != 0xFFFF ? String(snap.status.battery_mv) : String("null");
    body += F(",\"battery_voltage_v\":");
    body += snap.status.battery_mv != 0xFFFF ? String(snap.status.battery_mv / 1000.0f, 3) : String("null");
    if (snap.hasBatteryChargeRatePctPerHour) {
        body += F(",\"battery_charge_rate_pct_per_hour\":");
        body += snap.batteryChargeRatePctPerHourValid
                    ? String(snap.batteryChargeRatePctPerHour, 3)
                    : String("null");
    }
    if (RFFrontEnd::hasStationG3LnaControl()) {
        body += F(",\"station_g3_power_monitor_available\":");
        body += boolJson(snap.stationG3PowerMonitorAvailable);
        body += F(",\"station_g3_input_voltage_v\":");
        body += snap.stationG3PowerMonitorAvailable && snap.stationG3PowerValid
                    ? String(snap.stationG3InputVoltageV, 3) : String("null");
        body += F(",\"station_g3_current_ma\":");
        body += snap.stationG3PowerMonitorAvailable && snap.stationG3PowerValid
                    ? String(snap.stationG3CurrentMa, 1) : String("null");
        body += F(",\"station_g3_power_w\":");
        body += snap.stationG3PowerMonitorAvailable && snap.stationG3PowerValid
                    ? String(snap.stationG3PowerW, 3) : String("null");
        body += F(",\"station_g3_minimum_input_voltage_v\":");
        body += snap.stationG3PowerMonitorAvailable && snap.stationG3PowerValid
                    ? String(snap.stationG3MinimumInputVoltageV, 3) : String("null");
        body += F(",\"station_g3_maximum_current_ma\":");
        body += snap.stationG3PowerMonitorAvailable && snap.stationG3PowerValid
                    ? String(snap.stationG3MaximumCurrentMa, 1) : String("null");
    }
    body += F("}");
    return body;
}

static String buildRadioJson(const RuntimeStats::Snapshot& snap) {
    String body;
    body.reserve(512);
    body += F("{\"state\":");
    body += jsonQuote(radioStateLabel(snap));
    body += F(",\"standby\":");
    body += boolJson(snap.radioStandby);
    body += F(",\"auto_cad_enabled\":");
    body += boolJson(snap.autoCadEnabled);
    body += F(",\"frequency_hz\":");
    body += String(snap.radio.freq_hz);
    body += F(",\"frequency_mhz\":");
    body += String(snap.radio.freq_hz / 1000000.0f, 3);
    body += F(",\"bandwidth_hz\":");
    body += String(snap.radio.bandwidth_hz);
    body += F(",\"bandwidth_khz\":");
    body += String(snap.radio.bandwidth_hz / 1000.0f, 1);
    body += F(",\"spreading_factor\":");
    body += String(snap.radio.sf);
    body += F(",\"coding_rate\":");
    body += String(snap.radio.cr);
    body += F(",\"tx_power_dbm\":");
    body += String(snap.radio.power_dbm);
    body += F(",\"syncword\":");
    body += jsonQuote(String("0x") + String(snap.radio.syncword, HEX));
    body += F(",\"syncword_value\":");
    body += String(snap.radio.syncword);
    body += F(",\"preamble_len\":");
    body += String(snap.radio.preamble_len);
    if (RFFrontEnd::hasPaModeControl()) {
        body += F(",\"pa_high_power_enabled\":");
        body += boolJson(RFFrontEnd::isPaHighPowerEnabled());
    }
    if (RFFrontEnd::hasStationG3LnaControl()) {
        body += F(",\"station_g3_external_lna_enabled\":");
        body += boolJson(RFFrontEnd::isStationG3LnaEnabled());
    }
    if (RFFrontEnd::hasHeltecV43LnaControl()) {
        body += F(",\"heltec_v43_external_lna_enabled\":");
        body += boolJson(RFFrontEnd::isExternalLnaEnabled());
        body += F(",\"heltec_v43_fem_lna_bypassed\":");
        body += boolJson(RFFrontEnd::isFemLnaBypassed());
        body += F(",\"agc_reset_interval_sec\":");
        body += String(RFFrontEnd::getAgcResetIntervalSec());
    }
    body += F("}");
    return body;
}

static String buildCountersJson(const RuntimeStats::Snapshot& snap) {
    String body;
    body.reserve(256);
    body += F("{\"rx_packets\":");
    body += String(snap.status.rx_count);
    body += F(",\"tx_packets\":");
    body += String(snap.status.tx_count);
    body += F(",\"crc_errors\":");
    body += String(snap.status.crc_errors);
    body += F(",\"last_rssi_dbm\":");
    body += String(snap.status.last_rssi);
    body += F(",\"last_snr_db\":");
    body += String(snap.status.last_snr / 10.0f, 1);
    body += F(",\"noise_floor_dbm\":");
    body += String(snap.status.noise_floor_x10 / 10.0f, 1);
    body += F("}");
    return body;
}

static String buildNetworkJson(const WifiManager::Config& cfg,
                               const NetworkSnapshot& net) {
    String body;
    body.reserve(768);
    body += F("{\"mode\":");
    body += jsonQuote(cfg.useStaticIP ? "static" : "dhcp");
    body += F(",\"use_static_ip\":");
    body += boolJson(cfg.useStaticIP);
    body += F(",\"interface\":");
    body += jsonQuote(net.iface);
    body += F(",\"live\":");
    body += boolJson(net.live);
    body += F(",\"current_ip\":");
    body += jsonQuote(currentIPString());
    body += F(",\"subnet\":");
    body += ipJson(net.subnet);
    body += F(",\"gateway\":");
    body += ipJson(net.gateway);
    body += F(",\"dns1\":");
    body += ipJson(net.dns1);
    body += F(",\"dns2\":");
    body += ipJson(net.dns2);
    if (net.has_wifi_rssi) {
        body += F(",\"wifi_rssi_dbm\":");
        body += String(net.wifi_rssi_dbm);
    }
    body += F(",\"tcp_port\":");
    body += String(cfg.tcpPort);
    body += F(",\"token_set\":");
    body += boolJson(cfg.tcpToken.length() > 0);
    body += F(",\"saved\":{");
    body += F("\"static_ip\":");
    body += ipJson(cfg.staticIP);
    body += F(",\"subnet\":");
    body += ipJson(cfg.subnet);
    body += F(",\"gateway\":");
    body += ipJson(cfg.gateway);
    body += F(",\"dns1\":");
    body += ipJson(cfg.dns1);
    body += F(",\"dns2\":");
    body += ipJson(cfg.dns2);
    body += F("}}");
    return body;
}

static String buildConfigJson(const WifiManager::Config& cfg) {
    String body;
    body.reserve(512);
    body += F("{\"hostname\":");
    body += jsonQuote(cfg.hostname);
    body += F(",\"effective_hostname\":");
    body += jsonQuote(hostname);
    body += F(",\"tcp_token\":");
    body += jsonQuote(cfg.tcpToken);
    body += F(",\"tcp_port\":");
    body += String(cfg.tcpPort);
    body += F(",\"use_static_ip\":");
    body += boolJson(cfg.useStaticIP);
    body += F(",\"static_ip\":");
    body += ipJson(cfg.staticIP);
    body += F(",\"subnet\":");
    body += ipJson(cfg.subnet);
    body += F(",\"gateway\":");
    body += ipJson(cfg.gateway);
    body += F(",\"dns1\":");
    body += ipJson(cfg.dns1);
    body += F(",\"dns2\":");
    body += ipJson(cfg.dns2);
    if (WifiManager::hasWifiAntennaSwitch()) {
        body += F(",\"wifi_external_antenna\":");
        body += boolJson(cfg.wifiExternalAntenna);
    }
    if (RFFrontEnd::hasHeltecV43LnaControl()) {
        body += F(",\"heltec_v43_external_lna_enabled\":");
        body += boolJson(RFFrontEnd::isExternalLnaEnabled());
        body += F(",\"heltec_v43_fem_lna_bypassed\":");
        body += boolJson(RFFrontEnd::isFemLnaBypassed());
        body += F(",\"agc_reset_interval_sec\":");
        body += String(RFFrontEnd::getAgcResetIntervalSec());
    }
    if (RFFrontEnd::hasPaModeControl()) {
        body += F(",\"pa_high_power_enabled\":");
        body += boolJson(RFFrontEnd::isPaHighPowerEnabled());
    }
    if (RFFrontEnd::hasStationG3LnaControl()) {
        body += F(",\"station_g3_external_lna_enabled\":");
        body += boolJson(RFFrontEnd::isStationG3LnaEnabled());
    }
    body += F(",\"gps_enabled\":");
    body += boolJson(cfg.gpsEnabled);
    body += F(",\"gps_available\":");
    body += boolJson(GPSManager::hasGpsPins());
    body += F("}");
    return body;
}

static String buildStatsJson(const RuntimeStats::Snapshot& snap,
                             const WifiManager::Config& cfg,
                             const NetworkSnapshot& net,
                             const String& clientIP) {
    String body;
    body.reserve(2048);
    body += F("{\"battery_voltage_mv\":");
    body += snap.status.battery_mv != 0xFFFF ? String(snap.status.battery_mv) : String("null");
    body += F(",\"battery_voltage_v\":");
    body += snap.status.battery_mv != 0xFFFF ? String(snap.status.battery_mv / 1000.0f, 3) : String("null");
    if (snap.hasBatteryChargeRatePctPerHour) {
        body += F(",\"solar_charge_rate_percent_per_hour\":");
        body += snap.batteryChargeRatePctPerHourValid
                    ? String(snap.batteryChargeRatePctPerHour, 3)
                    : String("null");
    }
#if defined(BOARD_STATION_G3)
    {
        const auto& power = StationG3Power::snapshot();
        if (power.available) {
            body += F(",\"bus_voltage_v\":");
            body += power.valid ? String(power.inputVoltageV, 3) : String("null");
            body += F(",\"current_ma\":");
            body += power.valid ? String(power.currentMa, 2) : String("null");
            body += F(",\"power_mw\":");
            body += power.valid ? String(static_cast<uint32_t>(power.powerW * 1000.0f)) : String("null");
        }
    }
#endif
    body += F(",\"system\":");
    body += buildSystemJson(snap, net, clientIP);
    body += F(",\"radio\":");
    body += buildRadioJson(snap);
    body += F(",\"counters\":");
    body += buildCountersJson(snap);
    body += F(",\"network\":");
    body += buildNetworkJson(cfg, net);
    body += F(",\"gps\":");
    body += GPSManager::buildJson();
    body += F("}");
    return body;
}

static bool parseJsonIp(JsonVariantConst value, IPAddress& ip, const char* field, String& error) {
    if (value.isNull()) {
        ip = IPAddress((uint32_t)0);
        return true;
    }
    if (!value.is<const char*>()) {
        error = String(field) + " must be a string IP address.";
        return false;
    }
    String raw = value.as<const char*>();
    raw.trim();
    if (raw.length() == 0) {
        ip = IPAddress((uint32_t)0);
        return true;
    }
    if (!ip.fromString(raw)) {
        error = String(field) + " is not a valid IPv4 address.";
        return false;
    }
    return true;
}

struct RfConfigPatch {
    bool hasPaHighPower = false;
    bool paHighPowerEnabled = false;
    bool hasStationG3Lna = false;
    bool stationG3LnaEnabled = true;
    bool hasExternalLna = false;
    bool externalLnaEnabled = false;
    bool hasAgcResetInterval = false;
    uint16_t agcResetIntervalSec = 0;
};

static bool applyConfigPatch(JsonVariantConst root,
                             WifiManager::Config& cfg,
                             RfConfigPatch& rfPatch,
                             String& error) {
    if (!root.is<JsonObjectConst>()) {
        error = "JSON body must be an object.";
        return false;
    }

    JsonObjectConst obj = root.as<JsonObjectConst>();

    JsonVariantConst hostVal = obj["hostname"];
    if (!hostVal.isNull()) {
        if (!hostVal.is<const char*>()) {
            error = "hostname must be a string.";
            return false;
        }
        cfg.hostname = String(hostVal.as<const char*>());
        cfg.hostname.trim();
    }

    JsonVariantConst tokenVal = obj["tcp_token"];
    if (!tokenVal.isNull()) {
        if (!tokenVal.is<const char*>()) {
            error = "tcp_token must be a string.";
            return false;
        }
        cfg.tcpToken = String(tokenVal.as<const char*>());
        if (cfg.tcpToken.length() > MAX_TCP_TOKEN_LEN) {
            error = "tcp_token must be 0-64 characters.";
            return false;
        }
    }

    JsonVariantConst portVal = obj["tcp_port"];
    if (!portVal.isNull()) {
        if (!portVal.is<uint16_t>()) {
            error = "tcp_port must be an integer.";
            return false;
        }
        cfg.tcpPort = portVal.as<uint16_t>();
        if (cfg.tcpPort == 0) {
            error = "tcp_port must be between 1 and 65535.";
            return false;
        }
    }

    JsonVariantConst staticVal = obj["use_static_ip"];
    if (!staticVal.isNull()) {
        if (!staticVal.is<bool>()) {
            error = "use_static_ip must be true or false.";
            return false;
        }
        cfg.useStaticIP = staticVal.as<bool>();
    }

    JsonVariantConst antennaVal = obj["wifi_external_antenna"];
    if (!antennaVal.isNull()) {
        if (!WifiManager::hasWifiAntennaSwitch()) {
            error = "wifi_external_antenna is not supported on this board.";
            return false;
        }
        if (!antennaVal.is<bool>()) {
            error = "wifi_external_antenna must be true or false.";
            return false;
        }
        cfg.wifiExternalAntenna = antennaVal.as<bool>();
    }

    JsonVariantConst gpsVal = obj["gps_enabled"];
    if (!gpsVal.isNull()) {
        if (!GPSManager::hasGpsPins()) {
            error = "gps_enabled is not supported on this board.";
            return false;
        }
        if (!gpsVal.is<bool>()) {
            error = "gps_enabled must be true or false.";
            return false;
        }
        cfg.gpsEnabled = gpsVal.as<bool>();
    }

    JsonVariantConst paVal = obj["pa_high_power_enabled"];
    if (!paVal.isNull()) {
        if (!RFFrontEnd::hasPaModeControl()) {
            error = "PA mode control is not supported on this board.";
            return false;
        }
        if (!paVal.is<bool>()) {
            error = "pa_high_power_enabled must be true or false.";
            return false;
        }
        rfPatch.hasPaHighPower = true;
        rfPatch.paHighPowerEnabled = paVal.as<bool>();
    }

    JsonVariantConst stationG3LnaVal = obj["station_g3_external_lna_enabled"];
    if (!stationG3LnaVal.isNull()) {
        if (!RFFrontEnd::hasStationG3LnaControl()) {
            error = "Station G3 external LNA control is not supported on this board.";
            return false;
        }
        if (!stationG3LnaVal.is<bool>()) {
            error = "station_g3_external_lna_enabled must be true or false.";
            return false;
        }
        rfPatch.hasStationG3Lna = true;
        rfPatch.stationG3LnaEnabled = stationG3LnaVal.as<bool>();
    }

    JsonVariantConst lnaVal = obj["heltec_v43_external_lna_enabled"];
    if (!lnaVal.isNull()) {
        if (!RFFrontEnd::hasHeltecV43LnaControl()) {
            error = "heltec_v43_external_lna_enabled is not supported on this board.";
            return false;
        }
        if (!lnaVal.is<bool>()) {
            error = "heltec_v43_external_lna_enabled must be true or false.";
            return false;
        }
        rfPatch.hasExternalLna = true;
        rfPatch.externalLnaEnabled = lnaVal.as<bool>();
    }

    JsonVariantConst agcVal = obj["agc_reset_interval_sec"];
    if (!agcVal.isNull()) {
        if (!RFFrontEnd::hasHeltecV43LnaControl()) {
            error = "agc_reset_interval_sec is not supported on this board.";
            return false;
        }
        if (!agcVal.is<unsigned int>()) {
            error = "agc_reset_interval_sec must be an integer from 0 to 3600.";
            return false;
        }
        uint32_t sec = agcVal.as<uint32_t>();
        if (sec > 3600U) {
            error = "agc_reset_interval_sec must be between 0 and 3600.";
            return false;
        }
        rfPatch.hasAgcResetInterval = true;
        rfPatch.agcResetIntervalSec = (uint16_t)sec;
    }

    JsonVariantConst networkVal = obj["network"];
    if (!networkVal.isNull()) {
        if (!networkVal.is<JsonObjectConst>()) {
            error = "network must be an object.";
            return false;
        }
        JsonObjectConst network = networkVal.as<JsonObjectConst>();

        JsonVariantConst nestedStaticVal = network["use_static_ip"];
        if (!nestedStaticVal.isNull()) {
            if (!nestedStaticVal.is<bool>()) {
                error = "network.use_static_ip must be true or false.";
                return false;
            }
            cfg.useStaticIP = nestedStaticVal.as<bool>();
        }

        if (!parseJsonIp(network["static_ip"], cfg.staticIP, "network.static_ip", error)) return false;
        if (!parseJsonIp(network["subnet"], cfg.subnet, "network.subnet", error)) return false;
        if (!parseJsonIp(network["gateway"], cfg.gateway, "network.gateway", error)) return false;
        if (!parseJsonIp(network["dns1"], cfg.dns1, "network.dns1", error)) return false;
        if (!parseJsonIp(network["dns2"], cfg.dns2, "network.dns2", error)) return false;

        JsonVariantConst antennaVal = network["wifi_external_antenna"];
        if (!antennaVal.isNull()) {
            if (!WifiManager::hasWifiAntennaSwitch()) {
                error = "network.wifi_external_antenna is not supported on this board.";
                return false;
            }
            if (!antennaVal.is<bool>()) {
                error = "network.wifi_external_antenna must be true or false.";
                return false;
            }
            cfg.wifiExternalAntenna = antennaVal.as<bool>();
        }

        JsonVariantConst lnaVal = network["heltec_v43_external_lna_enabled"];
        if (!lnaVal.isNull()) {
            if (!RFFrontEnd::hasHeltecV43LnaControl()) {
                error = "network.heltec_v43_external_lna_enabled is not supported on this board.";
                return false;
            }
            if (!lnaVal.is<bool>()) {
                error = "network.heltec_v43_external_lna_enabled must be true or false.";
                return false;
            }
            rfPatch.hasExternalLna = true;
            rfPatch.externalLnaEnabled = lnaVal.as<bool>();
        }

        JsonVariantConst agcVal = network["agc_reset_interval_sec"];
        if (!agcVal.isNull()) {
            if (!RFFrontEnd::hasHeltecV43LnaControl()) {
                error = "network.agc_reset_interval_sec is not supported on this board.";
                return false;
            }
            if (!agcVal.is<unsigned int>()) {
                error = "network.agc_reset_interval_sec must be an integer from 0 to 3600.";
                return false;
            }
            uint32_t sec = agcVal.as<uint32_t>();
            if (sec > 3600U) {
                error = "network.agc_reset_interval_sec must be between 0 and 3600.";
                return false;
            }
            rfPatch.hasAgcResetInterval = true;
            rfPatch.agcResetIntervalSec = (uint16_t)sec;
        }
    }

    if (cfg.useStaticIP &&
        (((uint32_t)cfg.staticIP == 0) || ((uint32_t)cfg.subnet == 0) || ((uint32_t)cfg.gateway == 0))) {
        error = "static_ip, subnet, and gateway are required when use_static_ip is true.";
        return false;
    }

    return true;
}

// ─── Rollback sanity check ──────────────────────────────────
//
// When ESP-IDF boots from a newly-written slot, otadata marks it as
// "PENDING_VERIFY". On the NEXT reboot the bootloader auto-reverts to
// the previous slot unless we call esp_ota_mark_app_valid_cancel_rollback()
// first. We only call it once we've proven:
//   1. the radio came up (checked in main.cpp before OTAManager::begin)
//   2. a valid host frame was parsed — proves USB-CDC + frame parser work
//   3. we've been running for SANITY_TIMEOUT_MS without crashing
static void attemptMarkValid() {
    if (markedValid) return;
    if (!sawValidFrame) return;
    if ((int32_t)(millis() - sanityDeadline) < 0) return;

    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        markedValid = true;   // nothing we can do; stop retrying
        return;
    }
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        Serial.printf("[OTA] marked running app valid: %s\n",
                      err == ESP_OK ? "OK" : "FAIL");
    } else {
        Serial.printf("[OTA] running app state=%d (no rollback needed)\n", (int)state);
    }
    markedValid = true;
}

// ─── HTTP auth + handlers ───────────────────────────────────
static bool checkAuth() {
    // LAN-only policy first: drop any client whose source IP is
    // outside RFC1918 / link-local / loopback. Same rule as the
    // TCP protocol server (see net_filter.h). NAT-forwarded /
    // tunneled requests with a public source IP get a 403 and
    // never reach the auth check or the upload handler.
    IPAddress addr = httpServer->client().remoteIP();
    if (!isLanAddress(addr)) {
        Serial.printf("[OTA] reject non-LAN client %u.%u.%u.%u\n",
                      addr[0], addr[1], addr[2], addr[3]);
        httpServer->send(403, "text/plain",
                         "Forbidden: openHop Modem accepts LAN clients only.\n");
        return false;
    }
    if (httpPassword.length() == 0) httpPassword = DEFAULT_HTTP_PASSWORD;
    if (!httpServer->authenticate(HTTP_AUTH_USER, httpPassword.c_str())) {
        httpServer->requestAuthentication(BASIC_AUTH, modemTitle().c_str());
        return false;
    }
    return true;
}

static void handleRoot() {
    if (!checkAuth()) return;
    const std::string rendered = WebUiShared::renderRootPage(buildWebUiModel());
    httpServer->send(200, "text/html; charset=utf-8", rendered.c_str());
}

[[maybe_unused]] static void handleRootLegacy() {
    if (!checkAuth()) return;
    String title = modemTitle();
    const auto& cfg = WifiManager::getConfig();
    String clientIP = TCPServer::getClientIP();
    NetworkSnapshot net = getNetworkSnapshot();
    String ipValue = ipFieldValue(cfg.useStaticIP, cfg.staticIP, net.ip);
    String subnetValue = ipFieldValue(cfg.useStaticIP, cfg.subnet, net.subnet);
    String gatewayValue = ipFieldValue(cfg.useStaticIP, cfg.gateway, net.gateway);
    String dns1Value = ipFieldValue(cfg.useStaticIP, cfg.dns1, net.dns1);
    String dns2Value = ipFieldValue(cfg.useStaticIP, cfg.dns2, net.dns2);
    String leaseHint;
    if (cfg.useStaticIP) {
        leaseHint = "Static mode is saved. These values are what the modem will use after reboot.";
    } else if (net.live) {
        leaseHint = "DHCP is active. These fields show the live lease from ";
        leaseHint += net.iface;
        leaseHint += ".";
    } else {
        leaseHint = "DHCP is active. No live lease is available yet, so the fields are blank.";
    }
    String body;
    body.reserve(8192);
    body += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>");
    body += title;
    body += F("</title>"
              "<style>body{font-family:system-ui,sans-serif;max-width:760px;margin:1.25em auto;padding:0 1em;color:#222;line-height:1.45}"
              "h2{margin:.2em 0 .35em}p{margin:.45em 0}.m{color:#666;font-size:.92em}"
              ".summary{background:#f7f7f7;border:1px solid #ddd;border-radius:8px;padding:.8em 1em;margin:1em 0 1.2em}"
              ".summary strong{display:inline-block;min-width:4.5em}.chips{margin-top:.55em}"
              ".chip{display:inline-block;background:#efefef;border:1px solid #ddd;border-radius:999px;padding:.2em .6em;margin:0 .35em .35em 0;font-size:.9em}"
              "details{border:1px solid #ddd;border-radius:8px;padding:.65em .8em;margin:0 0 .9em;background:#fff}"
              "summary{cursor:pointer;font-weight:700;list-style:none}summary::-webkit-details-marker{display:none}"
              ".inside{margin-top:.8em}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:.75em 1em}"
              "label{display:block;margin-top:.8em;font-weight:600}input{width:100%;padding:.55em;box-sizing:border-box;font-size:1em;border:1px solid #ccc;border-radius:6px}"
              "input[type=file]{padding:.3em 0;border:0}input[type=checkbox]{width:auto;margin-right:.45em}"
              ".checkline{display:flex;align-items:center;gap:.35em;margin-top:.8em}button{margin-top:.9em;padding:.6em 1em;background:#2f6f5e;color:#fff;border:0;border-radius:6px;cursor:pointer}"
              "button.danger{background:#b3261e}code{font-family:ui-monospace,SFMono-Regular,monospace;background:#f3f3f3;padding:.1em .35em;border-radius:4px}"
              "@media (max-width:640px){body{padding:0 .75em}}</style></head><body>");
    body += "<h2>" + title + "</h2>";
    body += "<div class='summary'><p><strong>mDNS</strong> " + hostname + ".local</p>";
    body += "<p><strong>IP</strong> " + currentIPString() + "</p>";
    body += "<p><strong>Interface</strong> ";
    body += net.iface;
    body += "</p><p><strong>Current connection</strong> ";
    body += clientIP.length() > 0 ? clientIP : String("none");
    body += "</p><div class='chips'><span class='chip'>";
    body += cfg.tcpToken.length() > 0 ? "openHop protected" : "openHop open";
    body += "</span><span class='chip'>";
    body += cfg.useStaticIP ? "Static network saved" : "DHCP mode";
    body += F("</span></div><p class='m'><a href='/stats'>View stats page</a></p></div>");

    body += F("<details open><summary>OTA Update</summary><div class='inside'>"
              "<p>Upload an app-only <code>firmware.bin</code> over the LAN.</p>"
              "<form method='POST' action='/update' enctype='multipart/form-data'>"
              "<input type='file' name='firmware' accept='.bin' required><br>"
              "<button type='submit'>Upload firmware.bin</button>"
              "</form><p class='m'>CLI alternative: <code>curl -u admin:&lt;password&gt; -F firmware=@firmware.bin http://");
    body += hostname + ".local/update</code></p></div></details>";

    body += F("<details><summary>Hostname</summary><div class='inside'>"
              "<p>Controls the mDNS / OTA hostname the modem advertises on the network.</p>"
              "<form method='POST' action='/hostname'>"
              "<label>mDNS / OTA hostname</label>");
    body += "<input type='text' name='hostname' autocomplete='off' maxlength='32' value='" +
            cfg.hostname +
            "' placeholder='leave blank for default'>";
    body += F("<button type='submit'>Save hostname</button>"
              "</form><p class='m'>Blank resets to the board default. Reboot required.</p></div></details>");

    body += F("<details><summary>Network</summary><div class='inside'><p>");
    body += leaseHint;
    body += F("</p><form method='POST' action='/network'><div class='checkline'><input type='checkbox' id='static' name='static' value='1'");
    if (cfg.useStaticIP) body += F(" checked");
    body += F("><label for='static'>Use static IP instead of DHCP</label></div><div class='grid'>");
    body += "<div><label>Static IP</label><input type='text' name='ip' value='" + ipValue + "' placeholder='192.168.1.42'></div>";
    body += "<div><label>Subnet mask</label><input type='text' name='sn' value='" + subnetValue + "' placeholder='255.255.255.0'></div>";
    body += "<div><label>Gateway</label><input type='text' name='gw' value='" + gatewayValue + "' placeholder='192.168.1.1'></div>";
    body += "<div><label>DNS 1</label><input type='text' name='dns1' value='" + dns1Value + "' placeholder='1.1.1.1'></div>";
    body += "<div><label>DNS 2</label><input type='text' name='dns2' value='" + dns2Value + "' placeholder='8.8.8.8'></div>";
    body += "<div><label>Current source</label><div class='chip'>" + String(net.iface) + "</div></div>";
    body += F("</div>");
    if (WifiManager::hasWifiAntennaSwitch()) {
        body += F("<div class='checkline'><input type='checkbox' id='wifi_ant_ext' name='wifi_ant_ext' value='1'");
        if (cfg.wifiExternalAntenna) body += F(" checked");
        body += F("><label for='wifi_ant_ext'>Use external Wi-Fi antenna</label></div>");
    }
    body += F("<button type='submit'>Save network settings</button></form></div></details>");

    if (RFFrontEnd::hasHeltecV43LnaControl()) {
        body += F("<details open><summary>Heltec V4.3 RF Front-End</summary><div class='inside'>"
                  "<p>Toggle the KCT8103L external RX LNA for receive only. The firmware always bypasses the FEM LNA during transmit so the TX path remains available.</p>"
                  "<form method='POST' action='/rf-lna'>"
                  "<div class='checkline'><input type='checkbox' id='v43_lna_on' name='v43_lna_on' value='1'");
        if (RFFrontEnd::isExternalLnaEnabled()) body += F(" checked");
        body += F("><label for='v43_lna_on'>Enable external FEM RX LNA</label></div>");
        body += F("<label>agc.reset.interval (seconds, 0 disables)<input name='agc_reset_interval_sec' type='number' min='0' max='3600' step='1' value='");
        body += String(RFFrontEnd::getAgcResetIntervalSec());
        body += F("'></label>"
                  "<p class='m'>Periodically restarts RX gain control during long idle periods to prevent strong out-of-band interference from clamping the noise floor.</p>"
                  "<button type='submit'>Save RF front-end settings</button>"
                  "</form><p class='m'>Settings apply immediately and persist across reboots. Unchecked LNA = GPIO5/CTX HIGH, external LNA bypassed.</p></div></details>");
    }

    if (RFFrontEnd::hasPaModeControl() && RFFrontEnd::hasStationG3LnaControl()) {
        body += F("<details open><summary>Station G3 RF Front-End</summary><div class='inside'>"
                  "<p>Configure the Station G3 PA level on GPIO9 and receive-only external LNA on GPIO10.</p>"
                  "<form method='POST' action='/rf-pa'>"
                  "<div class='checkline'><input type='checkbox' id='g3_pa_high' name='g3_pa_high' value='1'");
        if (RFFrontEnd::isPaHighPowerEnabled()) body += F(" checked");
        body += F("><label for='g3_pa_high'>Enable higher PA mode</label></div>"
                  "<div class='checkline'><input type='checkbox' id='g3_lna_on' name='g3_lna_on' value='1'");
        if (RFFrontEnd::isStationG3LnaEnabled()) body += F(" checked");
        body += F("><label for='g3_lna_on'>Enable Station G3 external RX LNA</label></div>"
                  "<button type='submit'>Save RF front-end settings</button>"
                  "</form><p class='m'>The external LNA is receive-only and is always bypassed before transmit. Disable it when local RF noise reduces SNR.</p>"
                  "<p class='m'><strong>RF safety:</strong> remove the PA PL1 and LNA P jumpers for GPIO control. PA PL2 remains a physical jumper. Higher PA mode can produce substantially more antenna power; use a suitable antenna/load and comply with regional limits. The lower PA mode is the default.</p></div></details>");
    }

    if (GPSManager::hasGpsPins()) {
        body += F("<details open><summary>GPS</summary><div class='inside'>"
                  "<p>Turn the onboard GPS receiver interface on only when location data is needed. Default is off to save battery.</p>"
                  "<form method='POST' action='/gps'>"
                  "<div class='checkline'><input type='checkbox' id='gps_enabled' name='gps_enabled' value='1'");
        if (cfg.gpsEnabled) body += F(" checked");
        body += F("><label for='gps_enabled'>Enable GPS</label></div>"
                  "<button type='submit'>Save GPS setting</button>"
                  "</form><p class='m'>Applies immediately and persists across reboots.</p></div></details>");
    }

    body += F("<details><summary>openHop Token</summary><div class='inside'>"
              "<p>This token must match the <code>token</code> value in openHop so openHop can connect to the radio.</p>"
              "<form method='POST' action='/token'>"
              "<label>New openHop token</label>"
              "<input type='password' name='token' autocomplete='new-password' maxlength='64'>"
              "<label>Confirm openHop token</label>"
              "<input type='password' name='confirm' autocomplete='new-password' maxlength='64'>"
              "<button type='submit'>Save openHop token</button>"
              "</form><p class='m'>Current mode: <span class='chip'>");
    body += cfg.tcpToken.length() > 0 ? "Protected" : "Open";
    body += F("</span>. Leave both fields blank to clear it. Reboot required.</p></div></details>");

    body += F("<details><summary>HTTP Password</summary><div class='inside'>"
              "<p>Protects this web page and OTA uploads. Username: <code>admin</code>.</p>"
              "<form method='POST' action='/auth'>"
              "<label>New password</label>"
              "<input type='password' name='password' autocomplete='new-password' required minlength='1' maxlength='64'>"
              "<label>Confirm password</label>"
              "<input type='password' name='confirm' autocomplete='new-password' required minlength='1' maxlength='64'>"
              "<button type='submit'>Save password</button>"
              "</form><p class='m'>Password changes take effect on the next request.</p></div></details>");

    body += F("<details><summary>Wi-Fi Setup Mode</summary><div class='inside'>"
              "<p>Clear the saved modem configuration and reboot into the open <code>openHop-Modem-XXXX</code> setup AP.</p>"
              "<form method='POST' action='/wifi-reset' onsubmit=\"return confirm('Clear saved modem configuration and reboot into Wi-Fi setup AP?');\">"
              "<button class='danger' type='submit'>Enter Wi-Fi Setup Mode</button>"
              "</form><p class='m'>Use this before moving the modem to a different Wi-Fi network.</p></div></details>");

    body += F("<details><summary>Reboot</summary><div class='inside'>"
              "<p>Restart the modem without changing any settings.</p>"
              "<form method='POST' action='/reboot'>"
              "<button type='submit'>Reboot modem</button>"
              "</form></div></details>"
              "</body></html>");
    httpServer->send(200, "text/html; charset=utf-8", body);
}

static void handleStats() {
    if (!checkAuth()) return;
    const std::string rendered = WebUiShared::renderStatsPage(buildWebUiModel());
    httpServer->send(200, "text/html; charset=utf-8", rendered.c_str());
}

[[maybe_unused]] static void handleStatsLegacy() {
    if (!checkAuth()) return;

    const auto& cfg = WifiManager::getConfig();
    RuntimeStats::Snapshot snap = RuntimeStats::capture();
    NetworkSnapshot net = getNetworkSnapshot();
    GPSManager::Snapshot gps = GPSManager::snapshot();
    String clientIP = TCPServer::getClientIP();
    String body;
    body.reserve(6144);
    body += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<meta http-equiv='refresh' content='5'>"
              "<title>Modem Stats</title>"
              "<style>body{font-family:system-ui,sans-serif;max-width:760px;margin:1.25em auto;padding:0 1em;color:#222;line-height:1.45}"
              "h2{margin:.2em 0 .35em}h3{margin:1.2em 0 .45em}.m{color:#666;font-size:.92em}"
              ".card{background:#f7f7f7;border:1px solid #ddd;border-radius:8px;padding:.8em 1em;margin:1em 0}"
              ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:.75em 1em}"
              ".kv{border:1px solid #ddd;border-radius:8px;background:#fff;padding:.7em .8em}"
              ".k{display:block;color:#666;font-size:.9em;margin-bottom:.2em}.v{font-weight:600}"
              ".actions{margin:1em 0}.actions a{margin-right:1em}code{font-family:ui-monospace,SFMono-Regular,monospace;background:#f3f3f3;padding:.1em .35em;border-radius:4px}"
              "@media (max-width:640px){body{padding:0 .75em}}</style></head><body>");
    body += "<h2>" + modemTitle() + " Stats</h2>";
    body += F("<div class='actions'><a href='/'>Back to main page</a></div>");
    body += F("<p class='m'>Auto-refreshes every 5 seconds.</p>");

    body += F("<div class='card'><div class='grid'>");
    body += "<div class='kv'><span class='k'>Firmware</span><span class='v'>" + htmlEscape(snap.firmwareVersion) + "</span></div>";
    body += "<div class='kv'><span class='k'>Hostname</span><span class='v'>" + htmlEscape(hostname) + ".local</span></div>";
    body += "<div class='kv'><span class='k'>Current IP</span><span class='v'>" + currentIPString() + "</span></div>";
    body += "<div class='kv'><span class='k'>Connected client</span><span class='v'>" + (clientIP.length() > 0 ? clientIP : String("none")) + "</span></div>";
    body += "<div class='kv'><span class='k'>Interface</span><span class='v'>" + String(net.iface) + "</span></div>";
    if (net.has_wifi_rssi) {
        body += "<div class='kv'><span class='k'>Wi-Fi signal</span><span class='v'>" +
                String(net.wifi_rssi_dbm) + " dBm</span></div>";
    }
    body += "<div class='kv'><span class='k'>Uptime</span><span class='v'>" + formatUptime(snap.status.uptime_sec) + "</span></div>";
    if (snap.status.battery_mv != 0xFFFF) {
        body += "<div class='kv'><span class='k'>Battery</span><span class='v'>" +
                String(snap.status.battery_mv / 1000.0f, 3) + " V</span></div>";
    }
    if (snap.hasBatteryChargeRatePctPerHour) {
        body += "<div class='kv'><span class='k'>Battery charge rate</span><span class='v'>" +
                (snap.batteryChargeRatePctPerHourValid
                    ? String(snap.batteryChargeRatePctPerHour, 3) + " %/hr"
                    : String("unknown")) + "</span></div>";
    }
    if (gps.enabled) {
        body += "<div class='kv'><span class='k'>GPS fix</span><span class='v'>" +
                String(gps.fixValid ? "valid" : (gps.seen ? "no fix" : "waiting")) +
                "</span></div>";
        if (gps.fixValid) {
            body += "<div class='kv'><span class='k'>GPS location</span><span class='v'>" +
                    String(gps.latitude, 6) + ", " + String(gps.longitude, 6) +
                    "</span></div>";
        }
    }
    body += "</div></div>";

    body += F("<h3>Radio</h3><div class='grid'>");
    body += "<div class='kv'><span class='k'>State</span><span class='v'>" + String(radioStateLabel(snap)) + "</span></div>";
    body += "<div class='kv'><span class='k'>Frequency</span><span class='v'>" + String(snap.radio.freq_hz / 1000000.0f, 3) + " MHz</span></div>";
    body += "<div class='kv'><span class='k'>Bandwidth</span><span class='v'>" + String(snap.radio.bandwidth_hz / 1000.0f, 1) + " kHz</span></div>";
    body += "<div class='kv'><span class='k'>Spreading factor</span><span class='v'>SF" + String(snap.radio.sf) + "</span></div>";
    body += "<div class='kv'><span class='k'>Coding rate</span><span class='v'>4/" + String(snap.radio.cr) + "</span></div>";
    body += "<div class='kv'><span class='k'>TX power</span><span class='v'>" + String(snap.radio.power_dbm) + " dBm</span></div>";
    body += "<div class='kv'><span class='k'>Syncword</span><span class='v'>0x" + String(snap.radio.syncword, HEX) + "</span></div>";
    body += "<div class='kv'><span class='k'>Preamble</span><span class='v'>" + String(snap.radio.preamble_len) + "</span></div>";
    if (RFFrontEnd::hasPaModeControl()) {
        body += "<div class='kv'><span class='k'>Station G3 PA mode</span><span class='v'>" +
                String(RFFrontEnd::isPaHighPowerEnabled() ? "Higher" : "Lower") + "</span></div>";
    }
    if (RFFrontEnd::hasStationG3LnaControl()) {
        body += "<div class='kv'><span class='k'>Station G3 external RX LNA</span><span class='v'>" +
                String(RFFrontEnd::isStationG3LnaEnabled() ? "Enabled" : "Bypassed") + "</span></div>";
    }
    body += "</div>";

    body += F("<h3>Counters</h3><div class='grid'>");
    body += "<div class='kv'><span class='k'>RX packets</span><span class='v'>" + String(snap.status.rx_count) + "</span></div>";
    body += "<div class='kv'><span class='k'>TX packets</span><span class='v'>" + String(snap.status.tx_count) + "</span></div>";
    body += "<div class='kv'><span class='k'>CRC errors</span><span class='v'>" + String(snap.status.crc_errors) + "</span></div>";
    body += "<div class='kv'><span class='k'>Last RSSI</span><span class='v'>" + String(snap.status.last_rssi) + " dBm</span></div>";
    body += "<div class='kv'><span class='k'>Last SNR</span><span class='v'>" + String(snap.status.last_snr / 10.0f, 1) + " dB</span></div>";
    body += "<div class='kv'><span class='k'>Noise floor</span><span class='v'>" + String(snap.status.noise_floor_x10 / 10.0f, 1) + " dBm</span></div>";
    body += "<div class='kv'><span class='k'>Die temperature</span><span class='v'>" + String(snap.status.temp_c) + " C</span></div>";
    body += "</div>";

    if (RFFrontEnd::hasStationG3LnaControl()) {
        body += F("<h3>Station G3 Power</h3><div class='grid'>");
        body += "<div class='kv'><span class='k'>INA219 monitor</span><span class='v'>" +
                String(snap.stationG3PowerMonitorAvailable ? "Detected" : "Not detected") + "</span></div>";
        if (snap.stationG3PowerMonitorAvailable) {
            String readingState = snap.stationG3PowerValid ? String("Current") : String("Read error");
            body += "<div class='kv'><span class='k'>Reading state</span><span class='v'>" + readingState + "</span></div>";
            if (snap.stationG3PowerValid) {
                body += "<div class='kv'><span class='k'>Input voltage</span><span class='v'>" + String(snap.stationG3InputVoltageV, 3) + " V</span></div>";
                body += "<div class='kv'><span class='k'>Current</span><span class='v'>" + String(snap.stationG3CurrentMa, 1) + " mA</span></div>";
                body += "<div class='kv'><span class='k'>Power</span><span class='v'>" + String(snap.stationG3PowerW, 3) + " W</span></div>";
                body += "<div class='kv'><span class='k'>Minimum input voltage</span><span class='v'>" + String(snap.stationG3MinimumInputVoltageV, 3) + " V</span></div>";
                body += "<div class='kv'><span class='k'>Maximum current</span><span class='v'>" + String(snap.stationG3MaximumCurrentMa, 1) + " mA</span></div>";
            }
        }
        body += "</div>";
    }

    body += F("<h3>Network</h3><div class='grid'>");
    body += "<div class='kv'><span class='k'>Mode</span><span class='v'>" + String(cfg.useStaticIP ? "Static" : "DHCP") + "</span></div>";
    body += "<div class='kv'><span class='k'>Port</span><span class='v'>" + String(cfg.tcpPort) + "</span></div>";
    if (net.has_wifi_rssi) {
        body += "<div class='kv'><span class='k'>Wi-Fi RSSI</span><span class='v'>" +
                String(net.wifi_rssi_dbm) + " dBm</span></div>";
    }
    if (WifiManager::hasWifiAntennaSwitch()) {
        body += "<div class='kv'><span class='k'>Wi-Fi antenna</span><span class='v'>" +
                String(cfg.wifiExternalAntenna ? "External" : "Internal") + "</span></div>";
    }
    if (RFFrontEnd::hasHeltecV43LnaControl()) {
        body += "<div class='kv'><span class='k'>Heltec V4.3 external LNA</span><span class='v'>" +
                String(RFFrontEnd::isExternalLnaEnabled() ? "Enabled" : "Bypassed") + "</span></div>";
    }
    body += "<div class='kv'><span class='k'>Gateway</span><span class='v'>" + ((uint32_t)net.gateway != 0 ? net.gateway.toString() : String("none")) + "</span></div>";
    body += "<div class='kv'><span class='k'>Subnet</span><span class='v'>" + ((uint32_t)net.subnet != 0 ? net.subnet.toString() : String("none")) + "</span></div>";
    body += "<div class='kv'><span class='k'>DNS 1</span><span class='v'>" + ((uint32_t)net.dns1 != 0 ? net.dns1.toString() : String("none")) + "</span></div>";
    body += "<div class='kv'><span class='k'>DNS 2</span><span class='v'>" + ((uint32_t)net.dns2 != 0 ? net.dns2.toString() : String("none")) + "</span></div>";
    body += "</div></body></html>";
    httpServer->send(200, "text/html; charset=utf-8", body);
}

static void handleApiTemp() {
    if (!checkAuth()) return;

    RuntimeStats::Snapshot snap = RuntimeStats::capture();
    String body;
    body.reserve(192);
    body += F("{\"die_temperature_c\":");
    body += String(snap.status.temp_c);
    body += F(",\"battery_voltage_mv\":");
    body += snap.status.battery_mv != 0xFFFF ? String(snap.status.battery_mv) : String("null");
    body += F(",\"battery_voltage_v\":");
    body += snap.status.battery_mv != 0xFFFF ? String(snap.status.battery_mv / 1000.0f, 3) : String("null");
    if (snap.hasBatteryChargeRatePctPerHour) {
        body += F(",\"battery_charge_rate_pct_per_hour\":");
        body += snap.batteryChargeRatePctPerHourValid
                    ? String(snap.batteryChargeRatePctPerHour, 3)
                    : String("null");
    }
#if defined(BOARD_STATION_G3)
    {
        const auto& power = StationG3Power::snapshot();
        if (power.available) {
            body += F(",\"bus_voltage_v\":");
            body += power.valid ? String(power.inputVoltageV, 3) : String("null");
            body += F(",\"current_ma\":");
            body += power.valid ? String(power.currentMa, 2) : String("null");
            body += F(",\"power_mw\":");
            body += power.valid ? String(static_cast<uint32_t>(power.powerW * 1000.0f)) : String("null");
        }
    }
#endif
    body += F(",\"firmware\":\"");
    body += snap.firmwareVersion;
    body += F("\",\"hostname\":\"");
    body += hostname;
    body += F("\"}");
    sendJson(200, body);
}

static void handleApiSystem() {
    if (!checkAuth()) return;

    RuntimeStats::Snapshot snap = RuntimeStats::capture();
    NetworkSnapshot net = getNetworkSnapshot();
    sendJson(200, buildSystemJson(snap, net, TCPServer::getClientIP()));
}

static void handleApiRadio() {
    if (!checkAuth()) return;

    sendJson(200, buildRadioJson(RuntimeStats::capture()));
}

static void handleApiNetwork() {
    if (!checkAuth()) return;

    sendJson(200, buildNetworkJson(WifiManager::getConfig(), getNetworkSnapshot()));
}

static void handleApiGps() {
    if (!checkAuth()) return;

    sendJson(200, GPSManager::buildJson());
}

static void handleApiStats() {
    if (!checkAuth()) return;

    const auto& cfg = WifiManager::getConfig();
    RuntimeStats::Snapshot snap = RuntimeStats::capture();
    NetworkSnapshot net = getNetworkSnapshot();
    sendJson(200, buildStatsJson(snap, cfg, net, TCPServer::getClientIP()));
}

static void handleApiConfigGet() {
    if (!checkAuth()) return;

    sendJson(200, buildConfigJson(WifiManager::getConfig()));
}

static void handleApiConfigPost() {
    if (!checkAuth()) return;

    String raw = httpServer->arg("plain");
    if (raw.length() == 0) {
        sendJsonError(400, "Request body must contain JSON.");
        return;
    }

    JsonDocument doc;
    DeserializationError jsonError = deserializeJson(doc, raw);
    if (jsonError) {
        sendJsonError(400, String("Invalid JSON: ") + jsonError.c_str());
        return;
    }

    WifiManager::Config cfg = WifiManager::getConfig();
    RfConfigPatch rfPatch;
    String error;
    if (!applyConfigPatch(doc.as<JsonVariantConst>(), cfg, rfPatch, error)) {
        sendJsonError(400, error);
        return;
    }

    if (rfPatch.hasPaHighPower || rfPatch.hasStationG3Lna) {
        bool paHighPower = rfPatch.hasPaHighPower
                               ? rfPatch.paHighPowerEnabled
                               : RFFrontEnd::isPaHighPowerEnabled();
        bool lnaEnabled = rfPatch.hasStationG3Lna
                              ? rfPatch.stationG3LnaEnabled
                              : RFFrontEnd::isStationG3LnaEnabled();
        if (!RFFrontEnd::setStationG3RfConfig(paHighPower, lnaEnabled, true)) {
            sendJsonError(500, "Failed to save Station G3 RF front-end settings.");
            return;
        }
    }
    if (rfPatch.hasExternalLna &&
        !RFFrontEnd::setFemLnaBypassed(!rfPatch.externalLnaEnabled, true)) {
        sendJsonError(500, "Failed to save Heltec V4.3 LNA setting.");
        return;
    }
    if (rfPatch.hasAgcResetInterval &&
        !RFFrontEnd::setAgcResetIntervalSec(rfPatch.agcResetIntervalSec, true)) {
        sendJsonError(500, "Failed to save agc.reset.interval setting.");
        return;
    }

    WifiManager::saveConfig(cfg);

    Serial.printf("[OTA] API config updated by %s\n",
                  httpServer->client().remoteIP().toString().c_str());

    sendJson(200, String("{\"status\":\"saved\",\"rebooting\":true,\"config\":") +
                   buildConfigJson(cfg) + "}");
    delay(500);
    ESP.restart();
}

static void handleApiReboot() {
    if (!checkAuth()) return;

    Serial.printf("[OTA] API reboot requested by %s\n",
                  httpServer->client().remoteIP().toString().c_str());
    sendJson(200, F("{\"status\":\"rebooting\"}"));
    delay(500);
    ESP.restart();
}

static void handleWifiReset() {
    if (!checkAuth()) return;

    Serial.printf("[OTA] Wi-Fi setup reset requested by %s\n",
                  httpServer->client().remoteIP().toString().c_str());
    sendSimplePage(F("Entering Wi-Fi setup mode"),
                   F("Entering Wi-Fi setup mode"),
                   F("Saved modem configuration is being cleared. The modem will reboot into the openHop-Modem setup AP."));
    delay(500);
    WifiManager::factoryReset();   // does not return
}

static void handleHostnameSave() {
    if (!checkAuth()) return;

    WifiManager::Config cfg = WifiManager::getConfig();
    String requested = httpServer->arg("hostname");
    requested.trim();
    cfg.hostname = requested;
    WifiManager::saveConfig(cfg);

    Serial.printf("[OTA] hostname updated by %s -> '%s'\n",
                  httpServer->client().remoteIP().toString().c_str(),
                  requested.c_str());

    String body = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<title>Hostname saved</title></head>"
                    "<body style='font-family:system-ui,sans-serif;max-width:540px;"
                    "margin:2em auto;padding:0 1em;color:#222'>"
                    "<h2>Hostname saved</h2>"
                    "<p>The modem will reboot now and come back with the updated hostname.</p>"
                    "<p><a href='/'>Back to OTA page</a></p>"
                    "</body></html>");
    httpServer->send(200, "text/html; charset=utf-8", body);
    delay(500);
    ESP.restart();
}

static void handleNetworkSave() {
    if (!checkAuth()) return;

    WifiManager::Config cfg = WifiManager::getConfig();
    cfg.useStaticIP = httpServer->hasArg("static");
    cfg.staticIP    = parseIPArg(httpServer->arg("ip"));
    cfg.subnet      = parseIPArg(httpServer->arg("sn"));
    cfg.gateway     = parseIPArg(httpServer->arg("gw"));
    cfg.dns1        = parseIPArg(httpServer->arg("dns1"));
    cfg.dns2        = parseIPArg(httpServer->arg("dns2"));
    if (WifiManager::hasWifiAntennaSwitch()) {
        cfg.wifiExternalAntenna = httpServer->hasArg("wifi_ant_ext");
    } else {
        cfg.wifiExternalAntenna = false;
    }

    if (cfg.useStaticIP) {
        if ((uint32_t)cfg.staticIP == 0 || (uint32_t)cfg.subnet == 0 || (uint32_t)cfg.gateway == 0) {
            httpServer->send(400, "text/plain",
                             "Static IP, subnet, and gateway are required when static mode is enabled.\n");
            return;
        }
    }

    WifiManager::saveConfig(cfg);

    Serial.printf("[OTA] network config updated by %s -> %s\n",
                  httpServer->client().remoteIP().toString().c_str(),
                  cfg.useStaticIP ? "static" : "dhcp");

    sendSimplePage(F("Network saved"),
                   F("Network saved"),
                   cfg.useStaticIP
                       ? F("The modem will reboot now and come back using the configured static network settings.")
                       : F("The modem will reboot now and come back using DHCP."));
    delay(500);
    ESP.restart();
}

static void handleGpsSave() {
    if (!checkAuth()) return;

    if (!GPSManager::hasGpsPins()) {
        httpServer->send(400, "text/plain", "GPS is not supported on this board.\n");
        return;
    }

    WifiManager::Config cfg = WifiManager::getConfig();
    cfg.gpsEnabled = httpServer->hasArg("gps_enabled");
    WifiManager::saveConfig(cfg);
    GPSManager::setEnabled(cfg.gpsEnabled);

    Serial.printf("[OTA] GPS %s by %s\n",
                  cfg.gpsEnabled ? "enabled" : "disabled",
                  httpServer->client().remoteIP().toString().c_str());

    sendSimplePage(cfg.gpsEnabled ? F("GPS enabled") : F("GPS disabled"),
                   cfg.gpsEnabled ? F("GPS enabled") : F("GPS disabled"),
                   cfg.gpsEnabled
                       ? F("The GPS UART is enabled. Fix data may take a moment to appear.")
                       : F("The GPS UART is disabled and the setting has been saved."));
}


static void handleRfLnaSave() {
    if (!checkAuth()) return;

    if (!RFFrontEnd::hasHeltecV43LnaControl()) {
        httpServer->send(400, "text/plain", "Heltec V4.3 LNA control is not supported on this board.\n");
        return;
    }

    bool enableExternalLna = httpServer->hasArg("v43_lna_on");
    bool ok = RFFrontEnd::setFemLnaBypassed(!enableExternalLna, true);
    if (!ok) {
        httpServer->send(500, "text/plain", "Failed to save Heltec V4.3 LNA setting.\n");
        return;
    }

    String agcRaw = httpServer->arg("agc_reset_interval_sec");
    agcRaw.trim();
    uint32_t agcIntervalSec = agcRaw.length() > 0 ? (uint32_t)agcRaw.toInt() : 0;
    if (agcIntervalSec > 3600U) {
        httpServer->send(400, "text/plain", "agc.reset.interval must be between 0 and 3600 seconds.\n");
        return;
    }
    if (!RFFrontEnd::setAgcResetIntervalSec((uint16_t)agcIntervalSec, true)) {
        httpServer->send(500, "text/plain", "Failed to save agc.reset.interval setting.\n");
        return;
    }

    Serial.printf("[OTA] Heltec V4.3 external FEM RX LNA %s, agc.reset.interval=%lu s by %s\n",
                  enableExternalLna ? "enabled" : "bypassed",
                  (unsigned long)agcIntervalSec,
                  httpServer->client().remoteIP().toString().c_str());

    String detail = String(enableExternalLna
                       ? F("The KCT8103L external RX LNA has been enabled for receive; transmit will still bypass it automatically.")
                       : F("The KCT8103L external RX LNA has been bypassed."));
    detail += String(F(" agc.reset.interval is ")) + String(agcIntervalSec) + F(" seconds (0 disables). Settings persist across reboots.");
    sendSimplePage(enableExternalLna ? F("RF settings saved") : F("RF settings saved"),
                   F("RF settings saved"), detail);
}

static void handleRfPaSave() {
    if (!checkAuth()) return;

    if (!RFFrontEnd::hasPaModeControl() || !RFFrontEnd::hasStationG3LnaControl()) {
        httpServer->send(400, "text/plain", "Station G3 RF front-end control is not supported on this board.\n");
        return;
    }

    bool highPower = httpServer->hasArg("g3_pa_high");
    bool enableExternalLna = httpServer->hasArg("g3_lna_on");
    if (!RFFrontEnd::setStationG3RfConfig(highPower, enableExternalLna, true)) {
        httpServer->send(500, "text/plain", "Failed to save Station G3 RF front-end settings.\n");
        return;
    }

    Serial.printf("[OTA] Station G3 PA mode %s, external RX LNA %s by %s\n",
                  highPower ? "higher" : "lower",
                  enableExternalLna ? "enabled" : "bypassed",
                  httpServer->client().remoteIP().toString().c_str());
    String detail = highPower
        ? F("Higher PA mode is enabled and saved. Verify the PA PL1 jumper is removed and observe RF power limits.")
        : F("Lower PA mode is enabled and saved.");
    detail += enableExternalLna
        ? F(" The Station G3 external RX LNA is enabled; transmit will bypass it automatically.")
        : F(" The Station G3 external RX LNA is bypassed.");
    sendSimplePage(F("RF settings saved"), F("RF settings saved"), detail);
}

static void handleTokenSave() {
    if (!checkAuth()) return;

    WifiManager::Config cfg = WifiManager::getConfig();
    String requested = httpServer->arg("token");
    String confirm   = httpServer->arg("confirm");

    if (requested.length() > MAX_TCP_TOKEN_LEN) {
        httpServer->send(400, "text/plain", "openHop token must be 0-64 characters.\n");
        return;
    }
    if (requested != confirm) {
        httpServer->send(400, "text/plain", "openHop token confirmation does not match.\n");
        return;
    }

    cfg.tcpToken = requested;
    WifiManager::saveConfig(cfg);

    Serial.printf("[OTA] openHop token updated by %s -> %s\n",
                  httpServer->client().remoteIP().toString().c_str(),
                  requested.length() > 0 ? "set" : "cleared");

    sendSimplePage(F("openHop token saved"),
                   F("openHop token saved"),
                   requested.length() > 0
                       ? F("The modem will reboot now and require the new openHop token.")
                       : F("The modem will reboot now and allow openHop access without a token again."));
    delay(500);
    ESP.restart();
}

static void handleReboot() {
    if (!checkAuth()) return;

    Serial.printf("[OTA] reboot requested by %s\n",
                  httpServer->client().remoteIP().toString().c_str());
    sendSimplePage(F("Rebooting"),
                   F("Rebooting"),
                   F("The modem will reboot now."));
    delay(500);
    ESP.restart();
}

static void handleAuthSave() {
    if (!checkAuth()) return;

    String password = httpServer->arg("password");
    String confirm  = httpServer->arg("confirm");
    if (password.length() == 0 || password.length() > MAX_HTTP_PASSWORD_LEN) {
        httpServer->send(400, "text/plain", "Password must be 1-64 characters.\n");
        return;
    }
    if (password != confirm) {
        httpServer->send(400, "text/plain", "Password confirmation does not match.\n");
        return;
    }
    if (!saveHttpPassword(password)) {
        httpServer->send(500, "text/plain", "Failed to save password.\n");
        return;
    }
    if (token.length() == 0) {
        ArduinoOTA.setPassword(httpPassword.c_str());
    }
    Serial.printf("[OTA] HTTP password changed by %s\n",
                  httpServer->client().remoteIP().toString().c_str());

    sendSimplePage(F("Password saved"),
                   F("Password saved"),
                   F("Use the new password the next time this page asks for credentials."));
}

static void handleUpdateResult() {
    if (!checkAuth()) return;
    bool ok = !Update.hasError();
    String body;
    if (ok) {
        body = F("OK — rebooting into new firmware. "
                 "If the new image fails its sanity check within 2 minutes, "
                 "the bootloader will roll back automatically.");
    } else {
        body = String(F("FAIL — ")) + Update.errorString();
    }
    httpServer->send(ok ? 200 : 500, "text/plain; charset=utf-8", body);
    if (ok) {
        delay(500);
        ESP.restart();
    }
}

static void handleUpdateUpload() {
    if (!checkAuth()) return;
    HTTPUpload& up = httpServer->upload();

    if (up.status == UPLOAD_FILE_START) {
        Serial.printf("[OTA/HTTP] upload start: %s (%u bytes expected)\n",
                      up.filename.c_str(), (unsigned)up.totalSize);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            Update.printError(Serial);
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("[OTA/HTTP] upload complete: %u bytes\n",
                          (unsigned)up.totalSize);
        } else {
            Update.printError(Serial);
        }
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.end();
        Serial.println("[OTA/HTTP] upload aborted");
    }
}

// ─── ArduinoOTA plumbing ────────────────────────────────────
static void configureArduinoOTA() {
    ArduinoOTA.setHostname(hostname.c_str());
    if (token.length() > 0) {
        ArduinoOTA.setPassword(token.c_str());
    } else {
        ArduinoOTA.setPassword(httpPassword.c_str());
    }
    ArduinoOTA.onStart([]() {
        Serial.println("[OTA/Arduino] start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("[OTA/Arduino] end");
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[OTA/Arduino] error %u\n", (unsigned)e);
    });
    ArduinoOTA.begin();
}

// ─── Public API ─────────────────────────────────────────────
void begin(const String& hn, const String& tk) {
    if (started) return;
    hostname = hn;
    token    = tk;
    loadHttpPassword();

    if (!MDNS.begin(hostname.c_str())) {
        Serial.println("[OTA] mDNS start failed");
    } else {
        MDNS.addService("http", "tcp", HTTP_PORT);
        MDNS.addService("arduino", "tcp", 3232);
        Serial.printf("[OTA] mDNS advertising %s.local\n", hostname.c_str());
    }

    configureArduinoOTA();

    httpServer = new WebServer(HTTP_PORT);
    httpServer->on("/",       HTTP_GET,  handleRoot);
    httpServer->on("/stats",  HTTP_GET,  handleStats);
    httpServer->on("/api/temp", HTTP_GET, handleApiTemp);
    httpServer->on("/api/system", HTTP_GET, handleApiSystem);
    httpServer->on("/api/radio", HTTP_GET, handleApiRadio);
    httpServer->on("/api/network", HTTP_GET, handleApiNetwork);
    httpServer->on("/api/gps", HTTP_GET, handleApiGps);
    httpServer->on("/api/stats", HTTP_GET, handleApiStats);
    httpServer->on("/api/config", HTTP_GET, handleApiConfigGet);
    httpServer->on("/api/config", HTTP_POST, handleApiConfigPost);
    httpServer->on("/api/reboot", HTTP_POST, handleApiReboot);
    httpServer->on("/hostname", HTTP_POST, handleHostnameSave);
    httpServer->on("/network", HTTP_POST, handleNetworkSave);
    httpServer->on("/gps",     HTTP_POST, handleGpsSave);
    httpServer->on("/rf-lna",  HTTP_POST, handleRfLnaSave);
    if (RFFrontEnd::hasPaModeControl() && RFFrontEnd::hasStationG3LnaControl()) {
        httpServer->on("/rf-pa", HTTP_POST, handleRfPaSave);
    }
    httpServer->on("/token",  HTTP_POST, handleTokenSave);
    httpServer->on("/auth",   HTTP_POST, handleAuthSave);
    httpServer->on("/wifi-reset", HTTP_POST, handleWifiReset);
    httpServer->on("/reboot", HTTP_POST, handleReboot);
    httpServer->on("/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);
    httpServer->onNotFound([]() { httpServer->send(404, "text/plain", "Not found"); });
    httpServer->begin();

    sanityDeadline = millis() + SANITY_TIMEOUT_MS;
    sawValidFrame  = false;
    markedValid    = false;
    started        = true;

    Serial.printf("[OTA] HTTP /update + ArduinoOTA ready on %s (http auth: %s, arduino ota: %s)\n",
                  currentIPString().c_str(),
                  HTTP_AUTH_USER,
                  token.length() > 0 ? "tcp token" : "http password");
}

void loop() {
    if (!started) return;
    ArduinoOTA.handle();
    if (httpServer) httpServer->handleClient();
    if (!markedValid) attemptMarkValid();
}

void notifyValidFrame() {
    sawValidFrame = true;
}

const char* getHostname() {
    return hostname.c_str();
}

} // namespace OTAManager
