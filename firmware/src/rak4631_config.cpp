#include "rak4631_config.h"

#include <cstring>

namespace Rak4631Config {
namespace {

constexpr uint8_t MAGIC[4] = {'O', 'H', 'C', 'F'};
constexpr size_t HEADER_SIZE = 8;
constexpr size_t IPV4_BYTES = 20;
constexpr size_t STRING_WIRE_SIZE = 1 + MAX_TEXT_LENGTH;
constexpr size_t PAYLOAD_SIZE = 1 + 2 + IPV4_BYTES + (3 * STRING_WIRE_SIZE);
constexpr size_t RECORD_SIZE = HEADER_SIZE + PAYLOAD_SIZE + 4;
constexpr uint8_t FLAG_STATIC_IP = 0x01;
constexpr uint8_t FLAG_GPS_ENABLED = 0x02;

size_t boundedLength(const char* text, size_t capacity) {
    if (!text) return 0;
    size_t length = 0;
    while (length < capacity && text[length] != '\0') ++length;
    return length;
}

void copyBounded(char (&destination)[MAX_TEXT_LENGTH + 1], const char* source) {
    std::memset(destination, 0, sizeof(destination));
    if (!source) return;
    const size_t length = boundedLength(source, MAX_TEXT_LENGTH);
    std::memcpy(destination, source, length);
}

uint16_t readU16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readU32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void writeU16(uint8_t*& output, uint16_t value) {
    *output++ = static_cast<uint8_t>(value & 0xffU);
    *output++ = static_cast<uint8_t>((value >> 8) & 0xffU);
}

void writeU32(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value & 0xffU);
    output[1] = static_cast<uint8_t>((value >> 8) & 0xffU);
    output[2] = static_cast<uint8_t>((value >> 16) & 0xffU);
    output[3] = static_cast<uint8_t>((value >> 24) & 0xffU);
}

uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

void writeAddress(uint8_t*& cursor, const IPv4Address& address) {
    std::memcpy(cursor, address.octets, sizeof(address.octets));
    cursor += sizeof(address.octets);
}

void readAddress(const uint8_t*& cursor, IPv4Address& address) {
    std::memcpy(address.octets, cursor, sizeof(address.octets));
    cursor += sizeof(address.octets);
}

bool writeText(uint8_t*& cursor, const char* text) {
    const size_t length = boundedLength(text, MAX_TEXT_LENGTH + 1);
    if (length > MAX_TEXT_LENGTH) return false;
    *cursor++ = static_cast<uint8_t>(length);
    std::memset(cursor, 0, MAX_TEXT_LENGTH);
    std::memcpy(cursor, text, length);
    cursor += MAX_TEXT_LENGTH;
    return true;
}

bool readText(const uint8_t*& cursor, char (&output)[MAX_TEXT_LENGTH + 1]) {
    const uint8_t length = *cursor++;
    if (length > MAX_TEXT_LENGTH) return false;
    std::memset(output, 0, sizeof(output));
    std::memcpy(output, cursor, length);
    cursor += MAX_TEXT_LENGTH;
    return true;
}

uint32_t addressValue(const IPv4Address& address) {
    return (static_cast<uint32_t>(address.octets[0]) << 24) |
           (static_cast<uint32_t>(address.octets[1]) << 16) |
           (static_cast<uint32_t>(address.octets[2]) << 8) |
           static_cast<uint32_t>(address.octets[3]);
}

bool isUsableUnicast(const IPv4Address& address) {
    const uint8_t first = address.octets[0];
    return first != 0 && first != 127 && first < 224;
}

bool isValidOptionalDns(const IPv4Address& address) {
    if (address.isZero()) return true;
    return isUsableUnicast(address) && addressValue(address) != 0xffffffffU;
}

bool isValidStaticNetwork(const Config& config) {
    const uint32_t mask = addressValue(config.subnet);
    const uint32_t invertedMask = ~mask;
    if (mask == 0 || mask == 0xffffffffU ||
        (invertedMask & (invertedMask + 1U)) != 0) {
        return false;
    }

    const uint32_t address = addressValue(config.staticIP);
    const uint32_t gateway = addressValue(config.gateway);
    const uint32_t network = address & mask;
    const uint32_t broadcast = network | invertedMask;
    return isUsableUnicast(config.staticIP) && isUsableUnicast(config.gateway) &&
           address != gateway &&
           (gateway & mask) == network &&
           address != network && address != broadcast &&
           gateway != network && gateway != broadcast;
}

}  // namespace

bool IPv4Address::isZero() const {
    return octets[0] == 0 && octets[1] == 0 && octets[2] == 0 && octets[3] == 0;
}

bool IPv4Address::operator==(const IPv4Address& other) const {
    return std::memcmp(octets, other.octets, sizeof(octets)) == 0;
}

bool Config::operator==(const Config& other) const {
    return schemaVersion == other.schemaVersion &&
           std::strcmp(hostname, other.hostname) == 0 &&
           useStaticIP == other.useStaticIP &&
           staticIP == other.staticIP && subnet == other.subnet &&
           gateway == other.gateway && dns1 == other.dns1 && dns2 == other.dns2 &&
           tcpPort == other.tcpPort &&
           std::strcmp(tcpToken, other.tcpToken) == 0 &&
           std::strcmp(httpPassword, other.httpPassword) == 0 &&
           gpsEnabled == other.gpsEnabled;
}

void sanitizeHostname(const char* input, char output[MAX_TEXT_LENGTH + 1]) {
    std::memset(output, 0, MAX_TEXT_LENGTH + 1);
    if (!input) return;

    size_t outputLength = 0;
    bool lastWasHyphen = false;
    // A DNS hostname label is limited to 63 octets. Other persisted text
    // fields retain the 64-character storage contract.
    constexpr size_t MAX_HOSTNAME_LENGTH = 63;
    for (size_t i = 0; input[i] != '\0' && outputLength < MAX_HOSTNAME_LENGTH; ++i) {
        char value = input[i];
        if (value >= 'A' && value <= 'Z') value = static_cast<char>(value - 'A' + 'a');
        const bool valid = (value >= 'a' && value <= 'z') ||
                           (value >= '0' && value <= '9') || value == '-';
        if (!valid) value = '-';
        if (value == '-') {
            if (outputLength == 0 || lastWasHyphen) continue;
            lastWasHyphen = true;
        } else {
            lastWasHyphen = false;
        }
        output[outputLength++] = value;
    }
    while (outputLength > 0 && output[outputLength - 1] == '-') --outputLength;
    output[outputLength] = '\0';
}

Config makeDefaults(const char* hostname, uint16_t tcpPort, const char* tcpToken) {
    Config config{};
    config.schemaVersion = SCHEMA_VERSION;
    copyBounded(config.hostname, hostname);
    char sanitized[MAX_TEXT_LENGTH + 1]{};
    sanitizeHostname(config.hostname, sanitized);
    copyBounded(config.hostname, sanitized);
    config.useStaticIP = false;
    config.tcpPort = (tcpPort == 0 || tcpPort == 80) ? 5055 : tcpPort;
    copyBounded(config.tcpToken, tcpToken);
    copyBounded(config.httpPassword, "password");
    config.gpsEnabled = false;
    return config;
}

ValidationStatus validateAndNormalize(Config& config) {
    if (boundedLength(config.hostname, sizeof(config.hostname)) > MAX_TEXT_LENGTH ||
        boundedLength(config.tcpToken, sizeof(config.tcpToken)) > MAX_TEXT_LENGTH ||
        boundedLength(config.httpPassword, sizeof(config.httpPassword)) > MAX_TEXT_LENGTH) {
        return ValidationStatus::TEXT_TOO_LONG;
    }
    // Port 80 is reserved by the independent W5100S management listener.
    if (config.tcpPort == 0 || config.tcpPort == 80) {
        return ValidationStatus::INVALID_PORT;
    }
    if (config.useStaticIP &&
        (config.staticIP.isZero() || config.subnet.isZero() || config.gateway.isZero())) {
        return ValidationStatus::STATIC_ADDRESS_REQUIRED;
    }
    if (config.useStaticIP && !isValidStaticNetwork(config)) {
        return ValidationStatus::INVALID_STATIC_NETWORK;
    }
    if (!isValidOptionalDns(config.dns1) || !isValidOptionalDns(config.dns2)) {
        return ValidationStatus::INVALID_STATIC_NETWORK;
    }

    char sanitized[MAX_TEXT_LENGTH + 1]{};
    sanitizeHostname(config.hostname, sanitized);
    copyBounded(config.hostname, sanitized);
    config.schemaVersion = SCHEMA_VERSION;
    return ValidationStatus::OK;
}

bool encode(const Config& input, uint8_t* output, size_t capacity, size_t& written) {
    written = 0;
    if (!output || capacity < RECORD_SIZE) return false;
    Config config = input;
    if (validateAndNormalize(config) != ValidationStatus::OK) return false;

    uint8_t* cursor = output;
    std::memcpy(cursor, MAGIC, sizeof(MAGIC));
    cursor += sizeof(MAGIC);
    writeU16(cursor, SCHEMA_VERSION);
    writeU16(cursor, static_cast<uint16_t>(PAYLOAD_SIZE));
    *cursor++ = static_cast<uint8_t>((config.useStaticIP ? FLAG_STATIC_IP : 0) |
                                     (config.gpsEnabled ? FLAG_GPS_ENABLED : 0));
    writeU16(cursor, config.tcpPort);
    writeAddress(cursor, config.staticIP);
    writeAddress(cursor, config.subnet);
    writeAddress(cursor, config.gateway);
    writeAddress(cursor, config.dns1);
    writeAddress(cursor, config.dns2);
    if (!writeText(cursor, config.hostname) ||
        !writeText(cursor, config.tcpToken) ||
        !writeText(cursor, config.httpPassword)) {
        return false;
    }

    const size_t withoutCrc = static_cast<size_t>(cursor - output);
    writeU32(cursor, crc32(output, withoutCrc));
    cursor += 4;
    written = static_cast<size_t>(cursor - output);
    return written == RECORD_SIZE;
}

DecodeStatus decode(const uint8_t* data, size_t length, Config& output) {
    if (!data || length < HEADER_SIZE + 4) return DecodeStatus::BAD_LENGTH;
    if (std::memcmp(data, MAGIC, sizeof(MAGIC)) != 0) return DecodeStatus::BAD_MAGIC;

    const uint16_t schema = readU16(data + 4);
    const uint16_t payloadLength = readU16(data + 6);
    if (length != HEADER_SIZE + static_cast<size_t>(payloadLength) + 4)
        return DecodeStatus::BAD_LENGTH;
    if (schema != SCHEMA_VERSION) return DecodeStatus::UNSUPPORTED_SCHEMA;
    if (payloadLength != PAYLOAD_SIZE) return DecodeStatus::BAD_LENGTH;

    const uint32_t storedCrc = readU32(data + length - 4);
    if (storedCrc != crc32(data, length - 4)) return DecodeStatus::BAD_CRC;

    Config decoded{};
    decoded.schemaVersion = schema;
    const uint8_t* cursor = data + HEADER_SIZE;
    const uint8_t flags = *cursor++;
    decoded.useStaticIP = (flags & FLAG_STATIC_IP) != 0;
    decoded.gpsEnabled = (flags & FLAG_GPS_ENABLED) != 0;
    decoded.tcpPort = readU16(cursor);
    cursor += 2;
    readAddress(cursor, decoded.staticIP);
    readAddress(cursor, decoded.subnet);
    readAddress(cursor, decoded.gateway);
    readAddress(cursor, decoded.dns1);
    readAddress(cursor, decoded.dns2);
    if (!readText(cursor, decoded.hostname) ||
        !readText(cursor, decoded.tcpToken) ||
        !readText(cursor, decoded.httpPassword)) {
        return DecodeStatus::INVALID_CONFIG;
    }
    if (validateAndNormalize(decoded) != ValidationStatus::OK)
        return DecodeStatus::INVALID_CONFIG;
    output = decoded;
    return DecodeStatus::OK;
}

bool rewriteIntegrity(uint8_t* data, size_t length) {
    if (!data || length < HEADER_SIZE + 4) return false;
    const uint16_t payloadLength = readU16(data + 6);
    if (length != HEADER_SIZE + static_cast<size_t>(payloadLength) + 4) return false;
    writeU32(data + length - 4, crc32(data, length - 4));
    return true;
}

}  // namespace Rak4631Config

#if defined(BOARD_RAK4631_WISMESH_ETH)

#include "board_config.h"
#include "compat.h"

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <flash/flash_nrf5x.h>

#ifndef PYMC_ETH_TCP_PORT
#define PYMC_ETH_TCP_PORT 5055
#endif
#ifndef PYMC_ETH_TOKEN
#define PYMC_ETH_TOKEN ""
#endif
#ifndef PYMC_ETH_HOSTNAME
#define PYMC_ETH_HOSTNAME "openhop-rak4631-eth"
#endif

static_assert(sizeof(PYMC_ETH_HOSTNAME) - 1 <= Rak4631Config::MAX_TEXT_LENGTH,
              "PYMC_ETH_HOSTNAME exceeds the persisted configuration limit");
static_assert(sizeof(PYMC_ETH_TOKEN) - 1 <= Rak4631Config::MAX_TEXT_LENGTH,
              "PYMC_ETH_TOKEN exceeds the persisted configuration limit");

using namespace Adafruit_LittleFS_Namespace;

namespace Rak4631Config {
namespace {

constexpr const char* CONFIG_PATH = "/openhop_eth.cfg";
constexpr const char* TEMP_PATH = "/openhop_eth.tmp";
Config makePlatformDefaults() {
    Config config = makeDefaults(PYMC_ETH_HOSTNAME, PYMC_ETH_TCP_PORT, PYMC_ETH_TOKEN);
#if defined(PYMC_RAK4631_GPS_DEFAULT_ENABLED) && PYMC_RAK4631_GPS_DEFAULT_ENABLED
    config.gpsEnabled = true;
#endif
    return config;
}
Config activeConfig = makePlatformDefaults();
bool filesystemReady = false;
bool initialized = false;
char effectiveHostname[MAX_TEXT_LENGTH + 1] = {};
constexpr uint32_t INTERNAL_FS_START = 0xED000;
constexpr uint32_t INTERNAL_FS_SIZE = 0x7000;

bool internalFsIsErased() {
    uint8_t bytes[128];
    for (uint32_t address = INTERNAL_FS_START;
         address < INTERNAL_FS_START + INTERNAL_FS_SIZE;
         address += sizeof(bytes)) {
        if (flash_nrf5x_read(bytes, address, sizeof(bytes)) != sizeof(bytes)) return false;
        for (uint8_t value : bytes) {
            if (value != 0xff) return false;
        }
    }
    return true;
}

void refreshEffectiveHostname() {
    sanitizeHostname(activeConfig.hostname, effectiveHostname);
    if (effectiveHostname[0] != '\0') return;
    uint8_t mac[6]{};
    compatGetMac(mac);
    char generated[MAX_TEXT_LENGTH + 1]{};
    snprintf(generated, sizeof(generated), "%s-%02x%02x%02x",
             BOARD.mdns_prefix, mac[3], mac[4], mac[5]);
    sanitizeHostname(generated, effectiveHostname);
}

bool readPersisted(Config& config, DecodeStatus& status) {
    File file(InternalFS);
    if (!file.open(CONFIG_PATH, FILE_O_READ)) return false;
    const size_t length = file.size();
    if (length > MAX_ENCODED_SIZE) {
        file.close();
        status = DecodeStatus::BAD_LENGTH;
        return true;
    }
    uint8_t bytes[MAX_ENCODED_SIZE]{};
    const int count = file.read(bytes, length);
    file.close();
    if (count < 0 || static_cast<size_t>(count) != length) {
        status = DecodeStatus::BAD_LENGTH;
        return true;
    }
    status = decode(bytes, length, config);
    return true;
}

}  // namespace

bool begin() {
    if (initialized) return filesystemReady;
    initialized = true;
    activeConfig = makePlatformDefaults();
    refreshEffectiveHostname();

    // InternalFileSystem::begin() auto-erases on mount failure. Call the base
    // mount-only implementation so a transient/corrupt mount cannot destroy
    // configuration or other files. Initialize only demonstrably blank flash.
    filesystemReady = InternalFS.Adafruit_LittleFS::begin();
    if (!filesystemReady && internalFsIsErased()) {
        Serial.println("[CFG/RAK] blank InternalFS; formatting first-use filesystem");
        filesystemReady = InternalFS.format() && InternalFS.Adafruit_LittleFS::begin();
    }
    if (!filesystemReady) {
        Serial.println("[CFG/RAK] InternalFS unavailable; using safe build defaults");
        return false;
    }

    Config loaded{};
    DecodeStatus status = DecodeStatus::BAD_LENGTH;
    if (!readPersisted(loaded, status)) {
        Serial.println("[CFG/RAK] no persisted Ethernet config; using build defaults");
        return true;
    }
    if (status != DecodeStatus::OK) {
        Serial.printf("[CFG/RAK] invalid persisted config (%u); using safe build defaults\n",
                      static_cast<unsigned>(status));
        // A corrupt record is isolated to this file. Do not format InternalFS.
        return true;
    }

    activeConfig = loaded;
    refreshEffectiveHostname();
    Serial.println("[CFG/RAK] persisted Ethernet config loaded");
    return true;
}

const Config& getConfig() {
    return activeConfig;
}

bool saveConfig(const Config& input) {
    if (!initialized) begin();
    if (!filesystemReady) return false;

    Config normalized = input;
    if (validateAndNormalize(normalized) != ValidationStatus::OK) return false;
    uint8_t bytes[MAX_ENCODED_SIZE]{};
    size_t length = 0;
    if (!encode(normalized, bytes, sizeof(bytes), length)) return false;

    if (InternalFS.exists(TEMP_PATH) && !InternalFS.remove(TEMP_PATH)) return false;
    if (InternalFS.exists(TEMP_PATH)) return false;
    File file(InternalFS);
    if (!file.open(TEMP_PATH, FILE_O_WRITE)) return false;
    const size_t written = file.write(bytes, length);
    file.close();
    if (written != length) {
        InternalFS.remove(TEMP_PATH);
        return false;
    }
    if (!InternalFS.rename(TEMP_PATH, CONFIG_PATH)) {
        InternalFS.remove(TEMP_PATH);
        return false;
    }

    // Deliberately keep activeConfig unchanged. Ethernet/TCP settings are a
    // boot snapshot and take effect only after the caller reboots.
    return true;
}

bool factoryReset() {
    if (!initialized) begin();
    if (!filesystemReady) return false;
    const bool primaryAbsentOrRemoved = !InternalFS.exists(CONFIG_PATH) || InternalFS.remove(CONFIG_PATH);
    const bool tempAbsentOrRemoved = !InternalFS.exists(TEMP_PATH) || InternalFS.remove(TEMP_PATH);
    return primaryAbsentOrRemoved && tempAbsentOrRemoved;
}

const char* getEffectiveHostname() {
    if (!initialized) begin();
    return effectiveHostname;
}

}  // namespace Rak4631Config

#endif
