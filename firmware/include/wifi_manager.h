// =============================================================
// wifi_manager.h — Wi-Fi STA + AP-fallback manager for Heltec V3
// NVS-backed config (Preferences), PRG button factory reset.
// =============================================================
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace WifiManager {

constexpr uint32_t MIN_RX_SLOT_MS = 5;
constexpr uint32_t DEFAULT_RX_SLOT_MS = 100;
constexpr uint32_t DEFAULT_ACTIVITY_HOLD_MS = 2000;

#ifndef PYMC_RX_SLOT_MS
#  define PYMC_RX_SLOT_MS DEFAULT_RX_SLOT_MS
#endif
#ifndef PYMC_ACTIVITY_HOLD_MS
#  define PYMC_ACTIVITY_HOLD_MS DEFAULT_ACTIVITY_HOLD_MS
#endif

constexpr uint32_t BUILD_DEFAULT_RX_SLOT_MS =
    (PYMC_RX_SLOT_MS < MIN_RX_SLOT_MS) ? MIN_RX_SLOT_MS : (uint32_t)PYMC_RX_SLOT_MS;
constexpr uint32_t BUILD_DEFAULT_ACTIVITY_HOLD_MS = (uint32_t)PYMC_ACTIVITY_HOLD_MS;

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
    uint32_t  rxSlotMs = BUILD_DEFAULT_RX_SLOT_MS;
    uint32_t  activityHoldMs = BUILD_DEFAULT_ACTIVITY_HOLD_MS;
    uint32_t  txEchoHoldMultiplier = 1;
};

// Poll PRG button at boot; if held >= 3s, wipe NVS and reboot. Call from setup().
void checkResetButton();

// Load NVS config without bringing the radio up. Useful on boards where
// has_wifi = false (e.g. ESP32-P4-Nano in diagnostic mode) so the rest of
// the firmware can still read the saved tcpPort/tcpToken.
void loadConfigOnly();

// Load NVS config, try STA connect; on failure or empty config, start AP mode with
// on-device configuration web UI. Blocks up to ~30s during STA attempt.
void begin();

// Service background WiFi state transitions and config portal. Call every loop().
void loop();

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
