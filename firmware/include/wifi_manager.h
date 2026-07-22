// =============================================================
// wifi_manager.h — shared network configuration interface.
// ESP32 builds manage Wi-Fi/AP fallback with NVS. nRF52 W5100S builds use
// the same Config type for persisted Ethernet/TCP settings.
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
};

// Poll the platform reset gesture where supported. Call from setup().
void checkResetButton();

// Load persisted network/TCP config without bringing the interface up.
void loadConfigOnly();

// Bring up Wi-Fi on ESP32. On nRF52 this only ensures config is loaded;
// Ethernet itself is started by EthernetManager.
void begin();

// Service platform network-manager state. Call every loop().
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
bool          saveConfig(const Config& cfg);

// Clear persisted network config and restart where supported.
void factoryReset();

} // namespace WifiManager
