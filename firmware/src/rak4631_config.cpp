#include "rak4631_config.h"
#include "legacy_rak4631_build_flags.h"

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
    copyBounded(config.httpPassword, "openhop");
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
#include "rak4631_config_store.h"

#include <hal/nrf_nvmc.h>
#include <nrf_sdm.h>

#ifndef OPENHOP_ETH_TCP_PORT
#define OPENHOP_ETH_TCP_PORT 5055
#endif
#ifndef OPENHOP_ETH_TOKEN
#define OPENHOP_ETH_TOKEN ""
#endif
#ifndef OPENHOP_ETH_HOSTNAME
#define OPENHOP_ETH_HOSTNAME "openhop-rak4631-eth"
#endif

static_assert(sizeof(OPENHOP_ETH_HOSTNAME) - 1 <= Rak4631Config::MAX_TEXT_LENGTH,
              "OPENHOP_ETH_HOSTNAME exceeds the persisted configuration limit");
static_assert(sizeof(OPENHOP_ETH_TOKEN) - 1 <= Rak4631Config::MAX_TEXT_LENGTH,
              "OPENHOP_ETH_TOKEN exceeds the persisted configuration limit");

namespace Rak4631Config {
namespace {

Config makePlatformDefaults() {
    Config config = makeDefaults(OPENHOP_ETH_HOSTNAME, OPENHOP_ETH_TCP_PORT, OPENHOP_ETH_TOKEN);
#if defined(OPENHOP_RAK4631_GPS_DEFAULT_ENABLED) && OPENHOP_RAK4631_GPS_DEFAULT_ENABLED
    config.gpsEnabled = true;
#endif
    return config;
}
Config activeConfig = makePlatformDefaults();
bool persistenceReady = false;
bool initialized = false;
bool flashRuntimeReady = false;
char effectiveHostname[MAX_TEXT_LENGTH + 1] = {};

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

bool flashWritesAreSynchronous() {
    uint8_t softDeviceEnabled = 1;
    return flashRuntimeReady &&
           sd_softdevice_is_enabled(&softDeviceEnabled) == NRF_SUCCESS &&
           softDeviceEnabled == 0;
}

class NrfConfigFlash final : public Rak4631ConfigStore::Flash {
public:
    bool erasePage(uint32_t address) override {
        if (!validSlot(address) || !flashWritesAreSynchronous()) return false;
        if (!waitUntilReady()) return false;
        nrf_nvmc_mode_set(NRF_NVMC, NRF_NVMC_MODE_ERASE);
        nrf_nvmc_page_erase_start(NRF_NVMC, address);
        const bool erased = waitUntilReady();
        nrf_nvmc_mode_set(NRF_NVMC, NRF_NVMC_MODE_READONLY);
        if (!erased || !waitUntilReady()) return false;
        const uint8_t* page = reinterpret_cast<const uint8_t*>(address);
        for (size_t i = 0; i < Rak4631ConfigStore::PAGE_SIZE; ++i) {
            if (page[i] != 0xff) return false;
        }
        return true;
    }

    bool write(uint32_t address, const uint8_t* data, size_t length) override {
        if (!validRange(address, length) || data == nullptr || length == 0 ||
            (address & 3u) != 0 || (length & 3u) != 0 ||
            !flashWritesAreSynchronous()) {
            return false;
        }
        if (!waitUntilReady()) return false;
        nrf_nvmc_mode_set(NRF_NVMC, NRF_NVMC_MODE_WRITE);
        bool written = true;
        for (size_t offset = 0; offset < length; offset += sizeof(uint32_t)) {
            uint32_t word = 0;
            memcpy(&word, data + offset, sizeof(word));
            *reinterpret_cast<volatile uint32_t*>(address + offset) = word;
            if (!waitUntilReady()) {
                written = false;
                break;
            }
        }
        nrf_nvmc_mode_set(NRF_NVMC, NRF_NVMC_MODE_READONLY);
        return written && waitUntilReady() &&
               memcmp(reinterpret_cast<const void*>(address), data, length) == 0;
    }

    bool read(uint32_t address, uint8_t* data, size_t length) override {
        if (!validRange(address, length) || data == nullptr || length == 0)
            return false;
        memcpy(data, reinterpret_cast<const void*>(address), length);
        return true;
    }

private:
    static bool waitUntilReady() {
        // A page erase is normally tens of milliseconds. Keep the poll bounded
        // so an NVMC fault cannot strand the network loop forever.
        constexpr uint32_t READY_POLL_LIMIT = 16000000u;
        for (uint32_t attempt = 0; attempt < READY_POLL_LIMIT; ++attempt) {
            if (nrf_nvmc_ready_check(NRF_NVMC)) return true;
        }
        return false;
    }
    static bool validSlot(uint32_t address) {
        return address == Rak4631ConfigStore::SLOT_A_ADDRESS ||
               address == Rak4631ConfigStore::SLOT_B_ADDRESS;
    }
    static bool validRange(uint32_t address, size_t length) {
        const uint32_t slot = address >= Rak4631ConfigStore::SLOT_B_ADDRESS
                                  ? Rak4631ConfigStore::SLOT_B_ADDRESS
                                  : Rak4631ConfigStore::SLOT_A_ADDRESS;
        return validSlot(slot) && address >= slot &&
               address <= slot + Rak4631ConfigStore::PAGE_SIZE &&
               length <= slot + Rak4631ConfigStore::PAGE_SIZE - address;
    }
};

NrfConfigFlash configFlash;
Rak4631ConfigStore::Store configStore(configFlash);

}  // namespace

bool prepareFlashRuntime() {
    uint8_t softDeviceEnabled = 0;
    if (sd_softdevice_is_enabled(&softDeviceEnabled) != NRF_SUCCESS) {
        flashRuntimeReady = false;
        return false;
    }
    if (softDeviceEnabled != 0 && sd_softdevice_disable() != NRF_SUCCESS) {
        flashRuntimeReady = false;
        return false;
    }
    softDeviceEnabled = 1;
    flashRuntimeReady =
        sd_softdevice_is_enabled(&softDeviceEnabled) == NRF_SUCCESS &&
        softDeviceEnabled == 0;
    return flashRuntimeReady;
}

bool begin() {
    if (initialized) return persistenceReady;
    initialized = true;
    activeConfig = makePlatformDefaults();
    refreshEffectiveHostname();

    if (!flashWritesAreSynchronous()) {
        Serial.println("[CFG/RAK] config flash disabled: asynchronous SoftDevice events are unavailable");
        return false;
    }

    Config loaded{};
    const Rak4631ConfigStore::LoadResult result = configStore.load(loaded);
    if (result == Rak4631ConfigStore::LoadResult::IO_ERROR) {
        Serial.println("[CFG/RAK] config flash read failed; using safe build defaults");
        return false;
    }
    persistenceReady = true;
    if (result == Rak4631ConfigStore::LoadResult::EMPTY) {
        Serial.println("[CFG/RAK] no persisted Ethernet config; using build defaults");
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
    if (!persistenceReady || !flashWritesAreSynchronous()) return false;

    Config normalized = input;
    if (validateAndNormalize(normalized) != ValidationStatus::OK) return false;
    if (!configStore.save(normalized)) return false;

    // Deliberately keep activeConfig unchanged. Ethernet/TCP settings are a
    // boot snapshot and take effect only after the caller reboots.
    return true;
}

bool factoryReset() {
    if (!initialized) begin();
    return persistenceReady && flashWritesAreSynchronous() && configStore.erase();
}

const char* getEffectiveHostname() {
    if (!initialized) begin();
    return effectiveHostname;
}

const char* getDfuBluetoothAddress() {
    static char address[18]{};
    if (address[0] == '\0') {
        const uint32_t low = NRF_FICR->DEVICEADDR[0];
        const uint32_t high = NRF_FICR->DEVICEADDR[1];
        // RAK's Adafruit-derived bootloader calls sd_ble_gap_addr_get(), then
        // increments addr[0] before open DFU advertising. Match that exact
        // uint8_t operation (including wrap without carry).
        const uint8_t dfuLowByte = static_cast<uint8_t>((low & 0xffU) + 1U);
        snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X",
                 static_cast<unsigned>((high >> 8) & 0xff),
                 static_cast<unsigned>(high & 0xff),
                 static_cast<unsigned>((low >> 24) & 0xff),
                 static_cast<unsigned>((low >> 16) & 0xff),
                 static_cast<unsigned>((low >> 8) & 0xff),
                 static_cast<unsigned>(dfuLowByte));
    }
    return address;
}

}  // namespace Rak4631Config

#endif
