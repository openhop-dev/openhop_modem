// =============================================================
// wifi_manager.h — Wi-Fi STA + AP-fallback manager for Heltec V3
// NVS-backed config (Preferences), PRG button factory reset.
// =============================================================
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace WifiManager {

enum class Mode : uint8_t {
    OFFLINE        = 0,
    STA_CONNECTING = 1,
    STA_CONNECTED  = 2,
    AP_CONFIG      = 3,
};

struct Config {
    String    ssid;
    String    password;
    String    hostname;   // empty = derive "<board>-XXXXXX" from MAC
    bool      useStaticIP;
    IPAddress staticIP;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress dns1;
    IPAddress dns2;
    String    tcpToken;  // empty = no auth required
    uint16_t  tcpPort;   // default 5055
    bool      wifiExternalAntenna; // C6-only when BOARD enables antenna switch
    bool      gpsEnabled; // default off; applies to any board with GPS UART pins
    bool      wifiPowerSave; // default on; disable for lower latency at higher power draw
};

// Poll PRG button at boot; if held >= 3s, wipe NVS and reboot. Call from setup().
void checkResetButton();

// Load NVS config without bringing the radio up. Useful on boards where
// has_wifi = false (e.g. ESP32-P4-Nano in diagnostic mode) so the rest of
// the firmware can still read the saved tcpPort/tcpToken.
void loadConfigOnly();

// Load config and start a nonblocking STA attempt; loop owns recovery deadlines.
// Empty config starts setup AP; boot STA timeout adds AP while retrying STA.
void begin();

// Service background WiFi state transitions and config portal. Call every loop().
void loop();

// Main-loop-only: consume the old STA IPv4 address whose sessions are stale.
// Sticky across short event outages, zero when none. Call once after loop().
uint32_t consumeSTAInvalidation();

Mode        getMode();
const char* getSSID();        // current STA SSID, or AP SSID in AP_CONFIG
const char* getIPString();    // dotted quad, "---" when offline
const char* getHostname();    // configured hostname, or MAC-derived fallback
bool        isSTAConnected();
bool        isAPActive();
bool        hasWifiAntennaSwitch();
void        applyWifiAntennaSwitch();

const Config& getConfig();
void          saveConfig(const Config& cfg);

// Clear NVS Wi-Fi config and restart device. Does not return.
void factoryReset();

} // namespace WifiManager
