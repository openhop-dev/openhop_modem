// =============================================================
// nrf_settings.cpp — CRC-protected LittleFS settings record for
// nRF52 W5100S Ethernet targets.
// =============================================================
#if defined(PYMC_ETHERNET_W5100S)

#include "nrf_settings.h"

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <stddef.h>

using namespace Adafruit_LittleFS_Namespace;

#ifndef PYMC_ETH_TCP_PORT
#  define PYMC_ETH_TCP_PORT 5055
#endif
#ifndef PYMC_ETH_TOKEN
#  define PYMC_ETH_TOKEN ""
#endif
#ifndef PYMC_ETH_HOSTNAME
#  define PYMC_ETH_HOSTNAME "pymc-rak4631-eth"
#endif
#ifndef PYMC_ETH_USE_DHCP
#  define PYMC_ETH_USE_DHCP 1
#endif
#ifndef PYMC_ETH_STATIC_IP
#  define PYMC_ETH_STATIC_IP 192, 168, 1, 50
#endif
#ifndef PYMC_ETH_GATEWAY
#  define PYMC_ETH_GATEWAY 192, 168, 1, 1
#endif
#ifndef PYMC_ETH_SUBNET
#  define PYMC_ETH_SUBNET 255, 255, 255, 0
#endif
#ifndef PYMC_ETH_DNS
#  define PYMC_ETH_DNS 1, 1, 1, 1
#endif

namespace NrfSettings {

static constexpr const char* SETTINGS_PATH = "/openhop_net_v1";
static constexpr uint32_t SETTINGS_MAGIC = 0x4F484E31UL;  // "OHN1"
static constexpr uint16_t SETTINGS_VERSION = 1;
static constexpr const char* DEFAULT_HTTP_PASSWORD = "password";

struct __attribute__((packed)) StoredSettings {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    char hostname[33];
    char tcpToken[65];
    char httpPassword[65];
    uint16_t tcpPort;
    uint8_t useStaticIP;
    uint8_t staticIP[4];
    uint8_t gateway[4];
    uint8_t subnet[4];
    uint8_t dns1[4];
    uint8_t dns2[4];
    uint8_t reserved[7];
    uint32_t crc32;
};

static StoredSettings settings = {};
static bool initialized = false;
static bool filesystemReady = false;
static String httpPassword;

static uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return ~crc;
}

static void copyString(char* dest, size_t capacity, const String& value) {
    if (capacity == 0) return;
    size_t len = value.length();
    if (len >= capacity) len = capacity - 1;
    memcpy(dest, value.c_str(), len);
    dest[len] = '\0';
    if (len + 1 < capacity) memset(dest + len + 1, 0, capacity - len - 1);
}

static void copyIP(uint8_t dest[4], const IPAddress& ip) {
    for (uint8_t i = 0; i < 4; ++i) dest[i] = ip[i];
}

static IPAddress makeIP(const uint8_t src[4]) {
    return IPAddress(src[0], src[1], src[2], src[3]);
}

static void setDefaults() {
    memset(&settings, 0, sizeof(settings));
    settings.magic = SETTINGS_MAGIC;
    settings.version = SETTINGS_VERSION;
    settings.size = sizeof(settings);
    copyString(settings.hostname, sizeof(settings.hostname), String(PYMC_ETH_HOSTNAME));
    copyString(settings.tcpToken, sizeof(settings.tcpToken), String(PYMC_ETH_TOKEN));
    copyString(settings.httpPassword, sizeof(settings.httpPassword), String(DEFAULT_HTTP_PASSWORD));
    settings.tcpPort = PYMC_ETH_TCP_PORT;
    settings.useStaticIP = PYMC_ETH_USE_DHCP ? 0 : 1;
    const uint8_t staticIP[4] = {PYMC_ETH_STATIC_IP};
    const uint8_t gateway[4] = {PYMC_ETH_GATEWAY};
    const uint8_t subnet[4] = {PYMC_ETH_SUBNET};
    const uint8_t dns[4] = {PYMC_ETH_DNS};
    memcpy(settings.staticIP, staticIP, sizeof(staticIP));
    memcpy(settings.gateway, gateway, sizeof(gateway));
    memcpy(settings.subnet, subnet, sizeof(subnet));
    memcpy(settings.dns1, dns, sizeof(dns));
    memset(settings.dns2, 0, sizeof(settings.dns2));
    settings.crc32 = crc32(reinterpret_cast<const uint8_t*>(&settings),
                           offsetof(StoredSettings, crc32));
    httpPassword = DEFAULT_HTTP_PASSWORD;
}

static bool recordValid(const StoredSettings& record) {
    if (record.magic != SETTINGS_MAGIC || record.version != SETTINGS_VERSION ||
        record.size != sizeof(StoredSettings)) {
        return false;
    }
    if (record.hostname[sizeof(record.hostname) - 1] != '\0' ||
        record.tcpToken[sizeof(record.tcpToken) - 1] != '\0' ||
        record.httpPassword[sizeof(record.httpPassword) - 1] != '\0') {
        return false;
    }
    if (record.tcpPort == 0 || record.httpPassword[0] == '\0') return false;
    return record.crc32 == crc32(reinterpret_cast<const uint8_t*>(&record),
                                 offsetof(StoredSettings, crc32));
}

static bool writeRecord() {
    if (!filesystemReady) return false;
    settings.magic = SETTINGS_MAGIC;
    settings.version = SETTINGS_VERSION;
    settings.size = sizeof(settings);
    settings.crc32 = crc32(reinterpret_cast<const uint8_t*>(&settings),
                           offsetof(StoredSettings, crc32));

    InternalFS.remove(SETTINGS_PATH);
    File file(InternalFS);
    if (!file.open(SETTINGS_PATH, FILE_O_WRITE)) return false;
    const size_t written = file.write(reinterpret_cast<const uint8_t*>(&settings),
                                      sizeof(settings));
    file.close();
    return written == sizeof(settings);
}

void begin() {
    if (initialized) return;
    initialized = true;
    setDefaults();

    filesystemReady = InternalFS.begin();
    if (!filesystemReady) {
        // A fresh Adafruit nRF52 data partition may be unformatted. Format once,
        // then continue with defaults if the retry still fails.
        InternalFS.format();
        filesystemReady = InternalFS.begin();
    }
    if (!filesystemReady) {
        Serial.println("[SETTINGS] LittleFS unavailable; using volatile defaults");
        return;
    }

    StoredSettings loaded = {};
    File file(InternalFS);
    if (file.open(SETTINGS_PATH, FILE_O_READ)) {
        const int count = file.read(reinterpret_cast<uint8_t*>(&loaded), sizeof(loaded));
        file.close();
        if (count == (int)sizeof(loaded) && recordValid(loaded)) {
            settings = loaded;
            httpPassword = settings.httpPassword;
            Serial.println("[SETTINGS] loaded nRF52 Ethernet management settings");
            return;
        }
        Serial.println("[SETTINGS] invalid settings record; using defaults");
    }
}

void loadNetworkConfig(WifiManager::Config& config) {
    begin();
    config.ssid = "";
    config.password = "";
    config.hostname = settings.hostname;
    config.useStaticIP = settings.useStaticIP != 0;
    config.staticIP = makeIP(settings.staticIP);
    config.gateway = makeIP(settings.gateway);
    config.subnet = makeIP(settings.subnet);
    config.dns1 = makeIP(settings.dns1);
    config.dns2 = makeIP(settings.dns2);
    config.tcpToken = settings.tcpToken;
    config.tcpPort = settings.tcpPort ? settings.tcpPort : PYMC_ETH_TCP_PORT;
    config.wifiExternalAntenna = false;
    config.gpsEnabled = false;
}

bool saveNetworkConfig(const WifiManager::Config& config) {
    begin();
    copyString(settings.hostname, sizeof(settings.hostname), config.hostname);
    copyString(settings.tcpToken, sizeof(settings.tcpToken), config.tcpToken);
    settings.tcpPort = config.tcpPort ? config.tcpPort : PYMC_ETH_TCP_PORT;
    settings.useStaticIP = config.useStaticIP ? 1 : 0;
    copyIP(settings.staticIP, config.staticIP);
    copyIP(settings.gateway, config.gateway);
    copyIP(settings.subnet, config.subnet);
    copyIP(settings.dns1, config.dns1);
    copyIP(settings.dns2, config.dns2);
    return writeRecord();
}

const String& getHttpPassword() {
    begin();
    return httpPassword;
}

bool saveHttpPassword(const String& password) {
    begin();
    if (password.length() == 0 || password.length() > 64) return false;
    copyString(settings.httpPassword, sizeof(settings.httpPassword), password);
    if (!writeRecord()) return false;
    httpPassword = password;
    return true;
}

bool clear() {
    begin();
    bool ok = true;
    if (filesystemReady) ok = InternalFS.remove(SETTINGS_PATH);
    setDefaults();
    return ok;
}

}  // namespace NrfSettings

#endif  // PYMC_ETHERNET_W5100S
