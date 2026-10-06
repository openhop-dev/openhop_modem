// =============================================================
// wifi_manager.cpp — STA with AP-fallback, NVS config, PRG reset
// =============================================================
#include "wifi_manager.h"
#include "config_portal.h"
#include "board_config.h"
#include "compat.h"

#include <WiFi.h>
#include <Preferences.h>
#include <atomic>

namespace WifiManager {

static constexpr const char* NVS_NAMESPACE         = "lora_modem";
static constexpr uint16_t    DEFAULT_TCP_PORT      = 5055;
static constexpr uint32_t    STA_CONNECT_TIMEOUT_MS = 30000;
static constexpr uint32_t    PRG_RESET_HOLD_MS     = 5000;
static constexpr uint8_t     MAX_HOSTNAME_LEN      = 32;

static Config  cfg;
static Mode    currentMode = Mode::OFFLINE;
static String  apSSID;
static String  ipStr       = "---";
static String  effectiveHostname;
static bool    eventsRegistered = false;

// The Arduino event task only publishes bits/reason. No String, logging,
// driver calls or server mutation here. exchange() cannot lose a short outage.
static std::atomic<uint32_t> pendingEvents{0};
static constexpr uint32_t EVENT_INVALID = 1;
static constexpr uint32_t EVENT_STATE = 2;
static constexpr uint32_t EVENT_DOWN = 8;
static bool eventDown = false;
static constexpr uint32_t EVENT_REASON = 4;
static uint32_t invalidatedIP = 0;
static uint32_t lastSTAIP = 0;
static bool apActive = false;
static bool everConnected = false;
static bool fallbackWanted = false;
static uint32_t apAttemptAt = 0;
static bool apStopFailed = false;
static uint32_t apStopAt = 0;
enum class Phase { ATTEMPT, BACKOFF, RESTART_SETTLE };
static Phase phase = Phase::ATTEMPT;
static uint32_t phaseAt = 0;
static uint32_t backoffMs = 5000;
static uint32_t waitMs = 5000;
static unsigned failures = 0;

static void onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    uint32_t bits = 0;
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            // Latest reason and sticky invalidation are published atomically.
            bits = EVENT_INVALID | EVENT_STATE | EVENT_DOWN | EVENT_REASON |
                   (static_cast<uint32_t>(info.wifi_sta_disconnected.reason) << 8);
            break;
        case ARDUINO_EVENT_WIFI_STA_STOP:
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            bits = EVENT_INVALID | EVENT_STATE | EVENT_DOWN;
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            bits = EVENT_STATE;
            if (info.got_ip.ip_changed) bits |= EVENT_INVALID;
            break;
        default: return;
    }
    // Preserve sticky flags but replace, rather than OR together, reason codes.
    uint32_t previous = pendingEvents.load(std::memory_order_relaxed);
    uint32_t next;
    do {
        next = (bits & EVENT_REASON) ? ((previous & 0xff) | bits) : (previous | bits);
        next = (next & ~EVENT_DOWN) | (bits & EVENT_DOWN);
    } while (!pendingEvents.compare_exchange_weak(previous, next,
                 std::memory_order_relaxed, std::memory_order_relaxed));
}

static void loadConfig() {
    Preferences p;
    if (!p.begin(NVS_NAMESPACE, true)) {
        cfg = {};
        cfg.hostname = "";
        cfg.tcpPort = DEFAULT_TCP_PORT;
        cfg.wifiExternalAntenna = false;
        cfg.gpsEnabled = false;
        cfg.wifiPowerSave = true;
        effectiveHostname = "";
        return;
    }
    cfg.ssid        = p.getString("ssid",  "");
    cfg.password    = p.getString("pass",  "");
    cfg.hostname    = p.getString("host",  "");
    cfg.useStaticIP = p.getBool  ("static", false);
    cfg.staticIP    = IPAddress(p.getUInt("ip",  0));
    cfg.gateway     = IPAddress(p.getUInt("gw",  0));
    cfg.subnet      = IPAddress(p.getUInt("sn",  0));
    cfg.dns1        = IPAddress(p.getUInt("dns", 0));
    cfg.dns2        = IPAddress(p.getUInt("dns2", 0));
    cfg.tcpToken    = p.getString("token", "");
    cfg.tcpPort     = p.getUShort("port", DEFAULT_TCP_PORT);
    cfg.wifiExternalAntenna = p.getBool("ant_ext", false);
    cfg.gpsEnabled  = p.getBool("gps_en", false);
    cfg.wifiPowerSave = p.getBool("wifi_ps", true);
    p.end();
}

bool hasWifiAntennaSwitch() {
    return BOARD.wifi_antenna_switch.enabled &&
           BOARD.wifi_antenna_switch.gpio3_pin >= 0 &&
           BOARD.wifi_antenna_switch.gpio14_pin >= 0;
}

void applyWifiAntennaSwitch() {
    if (!hasWifiAntennaSwitch()) return;

    pinMode(BOARD.wifi_antenna_switch.gpio3_pin, OUTPUT);
    pinMode(BOARD.wifi_antenna_switch.gpio14_pin, OUTPUT);

    if (cfg.wifiExternalAntenna) {
        // Photon ESP32-C6 external Wi-Fi antenna path.
        digitalWrite(BOARD.wifi_antenna_switch.gpio3_pin, LOW);
        digitalWrite(BOARD.wifi_antenna_switch.gpio14_pin, HIGH);
    } else {
        // Internal/on-module antenna path: inverse of the external select.
        digitalWrite(BOARD.wifi_antenna_switch.gpio3_pin, HIGH);
        digitalWrite(BOARD.wifi_antenna_switch.gpio14_pin, LOW);
    }

    Serial.printf("[WiFi] antenna=%s gpio%d=%s gpio%d=%s\n",
                  cfg.wifiExternalAntenna ? "external" : "internal",
                  BOARD.wifi_antenna_switch.gpio3_pin,
                  cfg.wifiExternalAntenna ? "LOW" : "HIGH",
                  BOARD.wifi_antenna_switch.gpio14_pin,
                  cfg.wifiExternalAntenna ? "HIGH" : "LOW");
}

static String buildDefaultHostname() {
    uint8_t mac[6] = {0};
    compatGetMac(mac);
    char buf[40];
    snprintf(buf, sizeof(buf), "%s-%02x%02x%02x",
             BOARD.mdns_prefix, mac[3], mac[4], mac[5]);
    return String(buf);
}

static String sanitizeHostname(const String& raw) {
    String out;
    out.reserve(raw.length());

    bool lastWasHyphen = false;
    for (size_t i = 0; i < raw.length() && out.length() < MAX_HOSTNAME_LEN; i++) {
        char c = raw[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');

        bool valid = (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') ||
                     c == '-';
        if (!valid) c = '-';

        if (c == '-') {
            if (out.length() == 0 || lastWasHyphen) continue;
            lastWasHyphen = true;
        } else {
            lastWasHyphen = false;
        }
        out += c;
    }

    while (out.endsWith("-")) {
        out.remove(out.length() - 1);
    }
    return out;
}

static void refreshEffectiveHostname() {
    effectiveHostname = sanitizeHostname(cfg.hostname);
    if (effectiveHostname.length() == 0) {
        effectiveHostname = buildDefaultHostname();
    }
}

void saveConfig(const Config& newCfg) {
    Preferences p;
    if (!p.begin(NVS_NAMESPACE, false)) return;
    p.putString("ssid",  newCfg.ssid);
    p.putString("pass",  newCfg.password);
    p.putString("host",  sanitizeHostname(newCfg.hostname));
    p.putBool  ("static", newCfg.useStaticIP);
    p.putUInt  ("ip",    (uint32_t)newCfg.staticIP);
    p.putUInt  ("gw",    (uint32_t)newCfg.gateway);
    p.putUInt  ("sn",    (uint32_t)newCfg.subnet);
    p.putUInt  ("dns",   (uint32_t)newCfg.dns1);
    p.putUInt  ("dns2",  (uint32_t)newCfg.dns2);
    p.putString("token", newCfg.tcpToken);
    p.putUShort("port",  newCfg.tcpPort);
    if (hasWifiAntennaSwitch()) {
        p.putBool("ant_ext", newCfg.wifiExternalAntenna);
    }
    p.putBool("gps_en", newCfg.gpsEnabled);
    p.putBool("wifi_ps", newCfg.wifiPowerSave);
    p.end();
    cfg = newCfg;
    cfg.hostname = sanitizeHostname(cfg.hostname);
    if (!hasWifiAntennaSwitch()) {
        cfg.wifiExternalAntenna = false;
    }
    refreshEffectiveHostname();
}

void factoryReset() {
    Preferences p;
    if (p.begin(NVS_NAMESPACE, false)) {
        p.clear();
        p.end();
    }
    delay(200);
    ESP.restart();
}

void checkResetButton() {
    const int pin = BOARD.pin_user_button;
    if (pin < 0) return;   // boards without a usable user button skip the reset hold
    const int active = BOARD.user_button_active_low ? LOW : HIGH;
    pinMode(pin, INPUT_PULLUP);
    if (digitalRead(pin) != active) return;

    uint32_t start = millis();
    while (digitalRead(pin) == active) {
        if (millis() - start >= PRG_RESET_HOLD_MS) {
            factoryReset();   // does not return
            return;
        }
        delay(10);
    }
}

static void buildAPSsid() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[32];
    snprintf(buf, sizeof(buf), "openHop-Modem-%02X%02X", mac[4], mac[5]);
    apSSID = buf;
}

static void startAPMode() {
    apAttemptAt = millis();
    buildAPSsid();
    // Keep STA available for saved-network retries without taking the portal down.
    if (!WiFi.mode(cfg.ssid.length() ? WIFI_AP_STA : WIFI_AP) ||
        !WiFi.softAP(apSSID.c_str(), nullptr)) {
        Serial.println("[WiFi] setup AP start failed; retry in 5s");
        return;
    }
    apActive = true;
    ipStr = WiFi.softAPIP().toString();
    currentMode = Mode::AP_CONFIG;
    ConfigPortal::begin();
    Serial.printf("[WiFi] setup AP ready ip=%s\n", ipStr.c_str());
}

static void attemptSTA() {
    phase = Phase::ATTEMPT;
    phaseAt = millis();
    WiFi.persistent(false);   // Preferences owns credentials; never erase here
    WiFi.setSleep(cfg.wifiPowerSave);
    // Firmware owns all retry deadlines, including AUTH_FAIL and LOST_IP.
    // Do not race Arduino's reason-dependent reconnect loop.
    WiFi.setAutoReconnect(false);
    bool ok = WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
    if (ok) ok = WiFi.setHostname(effectiveHostname.c_str());
    if (ok && cfg.useStaticIP) {
        ok = WiFi.config(cfg.staticIP, cfg.gateway, cfg.subnet, cfg.dns1, cfg.dns2);
    }
    // Default station netif remains DHCP; static configuration is reapplied
    // after every station recreation. Antenna and sleep policy are unchanged.
    if (ok) ok = WiFi.disconnect(false, false);
    if (ok) ok = WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str()) != WL_CONNECT_FAILED;
    Serial.printf("[WiFi] STA attempt %s (%s), deadline=30s failures=%u\n",
                  ok ? "started" : "API failed", cfg.useStaticIP ? "static" : "DHCP", failures);
    currentMode = apActive ? Mode::AP_CONFIG : Mode::STA_CONNECTING;
}

void loadConfigOnly() {
    // Just load Wi-Fi/TCP config from NVS — do not touch the radio.
    // Used on Wi-Fi-disabled boards so the saved tcpPort/tcpToken
    // are still available to the TCP server.
    loadConfig();
    cfg.hostname = sanitizeHostname(cfg.hostname);
    refreshEffectiveHostname();
    applyWifiAntennaSwitch();
}

void begin() {
    loadConfig();
    cfg.hostname = sanitizeHostname(cfg.hostname);
    refreshEffectiveHostname();
    applyWifiAntennaSwitch();

    Serial.printf("[Boot] WifiManager: saved ssid='%s' host='%s' %s\n",
                  cfg.ssid.c_str(),
                  effectiveHostname.c_str(),
                  cfg.ssid.length() == 0 ? "(empty -> AP mode)" : "");

    if (!eventsRegistered) {
        WiFi.onEvent(onWiFiEvent);
        eventsRegistered = true;
    }

    if (cfg.ssid.length() == 0) {
        currentMode = Mode::AP_CONFIG; // retry AP startup even if the driver fails
        startAPMode();
        return;
    }

    attemptSTA();
}

void loop() {
    if (currentMode == Mode::OFFLINE) return;
    const uint32_t now = millis();
    const uint32_t events = pendingEvents.exchange(0, std::memory_order_relaxed);
    if (events & EVENT_REASON) {
        Serial.printf("[WiFi] STA disconnected reason=%u\n", (unsigned)(events >> 8));
    }
    if (events & EVENT_STATE) eventDown = events & EVENT_DOWN;
    const uint32_t ip = static_cast<uint32_t>(WiFi.localIP());
    const bool usable = !eventDown && WiFi.status() == WL_CONNECTED && ip != 0;
    if (lastSTAIP && ((events & EVENT_INVALID) || !usable || ip != lastSTAIP)) {
        invalidatedIP = lastSTAIP;
        lastSTAIP = 0;
    }
    if (usable && cfg.ssid.length()) {
        lastSTAIP = ip;
        if (apActive) {
            // Stop port 80 before the main loop can start the management server.
            // A failed AP shutdown remains retryable without deleting the portal.
            if (apStopFailed && now - apStopAt < 5000) {
                ConfigPortal::loop();
                return;
            }
            apStopAt = now;
            if (!WiFi.softAPdisconnect(true)) {
                apStopFailed = true;
                Serial.println("[WiFi] AP shutdown failed; retry in 5s");
                ConfigPortal::loop();
                return;
            }
            apStopFailed = false;
            ConfigPortal::end();
            apActive = false;
        }
        if (currentMode != Mode::STA_CONNECTED) {
            Serial.printf("[WiFi] STA usable ip=%s\n", WiFi.localIP().toString().c_str());
        }
        ipStr = WiFi.localIP().toString();
        currentMode = Mode::STA_CONNECTED;
        everConnected = true;
        fallbackWanted = false;
        failures = 0;
        backoffMs = 5000;
        return;
    }
    if (currentMode == Mode::STA_CONNECTED) {
        currentMode = Mode::STA_CONNECTING;
        phase = Phase::ATTEMPT;
        phaseAt = now; // bounded grace for a transient event/IP loss
    }
    ipStr = apActive ? WiFi.softAPIP().toString() : String("---");
    if (apActive) ConfigPortal::loop();
    if ((fallbackWanted || !cfg.ssid.length()) && !apActive && now - apAttemptAt >= 5000) {
        startAPMode();
    }
    if (!cfg.ssid.length()) return; // intentional setup: never attempt an empty SSID

    if (phase == Phase::ATTEMPT && now - phaseAt >= STA_CONNECT_TIMEOUT_MS) {
        ++failures;
        // Only boot failure opens a setup AP. A previously configured runtime
        // outage must not expose the unauthenticated setup portal on the LAN.
        if (!everConnected && !apActive) {
            fallbackWanted = true;
            startAPMode();
        }
        phase = Phase::BACKOFF;
        phaseAt = now;
        waitMs = backoffMs;
        backoffMs = backoffMs >= 30000 ? 60000 : backoffMs * 2;
        Serial.printf("[WiFi] STA deadline status=%d retry=%lus failures=%u\n",
                      (int)WiFi.status(), (unsigned long)(waitMs / 1000), failures);
    } else if (phase == Phase::BACKOFF && now - phaseAt >= waitMs) {
        if (failures % 3 == 0) {
            // Disable STA only; preserve the recovery AP and all Ethernet state.
            if (!WiFi.mode(apActive ? WIFI_AP : WIFI_OFF)) {
                phaseAt = now;
                Serial.println("[WiFi] STA restart failed; bounded retry");
                return;
            }
            phase = Phase::RESTART_SETTLE;
            phaseAt = now;
            Serial.println("[WiFi] restarting station interface");
        } else {
            attemptSTA();
        }
    } else if (phase == Phase::RESTART_SETTLE && now - phaseAt >= 250) {
        attemptSTA();
    }
}

uint32_t consumeSTAInvalidation() {
    const uint32_t ip = invalidatedIP;
    invalidatedIP = 0;
    return ip;
}

Mode        getMode()         { return currentMode; }
bool        isSTAConnected()  { return currentMode == Mode::STA_CONNECTED; }
bool        isAPActive()      { return apActive; }
const char* getIPString()     { return ipStr.c_str(); }
const Config& getConfig()     { return cfg; }
const char* getHostname()     { return effectiveHostname.c_str(); }

const char* getSSID() {
    if (currentMode == Mode::AP_CONFIG) return apSSID.c_str();
    if (cfg.ssid.length() > 0)          return cfg.ssid.c_str();
    return "---";
}

} // namespace WifiManager
