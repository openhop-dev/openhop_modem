// =============================================================
// nrf_wifi_manager.cpp — WifiManager-compatible configuration
// adapter for nRF52 builds. W5100S targets persist Ethernet/TCP
// settings; USB-only targets retain harmless in-memory defaults.
// =============================================================
#if !defined(ARDUINO_ARCH_ESP32)

#include "wifi_manager.h"
#include "board_config.h"
#include "compat.h"

#if defined(PYMC_ETHERNET_W5100S)
#  include "nrf_settings.h"
#  include <nrf.h>
#endif

#ifndef PYMC_ETH_TCP_PORT
#  define PYMC_ETH_TCP_PORT 5055
#endif
#ifndef PYMC_ETH_TOKEN
#  define PYMC_ETH_TOKEN ""
#endif
#ifndef PYMC_ETH_HOSTNAME
#  define PYMC_ETH_HOSTNAME "pymc-rak4631-eth"
#endif

namespace WifiManager {

static Config config = {};
static bool loaded = false;
static String ipString = "---";

static void loadDefaults() {
    config = Config{};
#if defined(PYMC_ETHERNET_W5100S)
    config.hostname = PYMC_ETH_HOSTNAME;
    config.tcpToken = PYMC_ETH_TOKEN;
    config.tcpPort = PYMC_ETH_TCP_PORT;
    NrfSettings::loadNetworkConfig(config);
#else
    uint8_t mac[6] = {};
    compatGetMac(mac);
    char suffix[16];
    snprintf(suffix, sizeof(suffix), "%s-%02x%02x%02x",
             BOARD.mdns_prefix, mac[3], mac[4], mac[5]);
    config.hostname = suffix;
    config.tcpPort = 0;
#endif
    loaded = true;
}

void checkResetButton() {}

void loadConfigOnly() {
    if (!loaded) loadDefaults();
}

void begin() {
    loadConfigOnly();
}

void loop() {}

Mode getMode() { return Mode::OFFLINE; }
const char* getSSID() { return "---"; }
const char* getIPString() { return ipString.c_str(); }
const char* getHostname() {
    loadConfigOnly();
    return config.hostname.c_str();
}
bool isSTAConnected() { return false; }
bool isAPActive() { return false; }
bool hasWifiAntennaSwitch() { return false; }
void applyWifiAntennaSwitch() {}

const Config& getConfig() {
    loadConfigOnly();
    return config;
}

bool saveConfig(const Config& requested) {
    loadConfigOnly();
    Config next = requested;
    if (next.hostname.length() == 0) next.hostname = PYMC_ETH_HOSTNAME;
    if (next.hostname.length() > 32) next.hostname.remove(32);
    if (next.tcpToken.length() > 64) next.tcpToken.remove(64);
    if (next.tcpPort == 0) next.tcpPort = PYMC_ETH_TCP_PORT;
    next.ssid = "";
    next.password = "";
    next.wifiExternalAntenna = false;
    next.gpsEnabled = false;
#if defined(PYMC_ETHERNET_W5100S)
    if (!NrfSettings::saveNetworkConfig(next)) {
        Serial.println("[SETTINGS] failed to persist nRF52 Ethernet settings");
        return false;
    }
#endif
    config = next;
    return true;
}

void factoryReset() {
#if defined(PYMC_ETHERNET_W5100S)
    NrfSettings::clear();
    delay(100);
    NVIC_SystemReset();
    while (true) delay(1000);
#else
    loadDefaults();
#endif
}

}  // namespace WifiManager

#endif  // !ARDUINO_ARCH_ESP32
