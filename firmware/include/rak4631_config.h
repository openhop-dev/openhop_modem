#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Rak4631Config {

constexpr uint16_t SCHEMA_VERSION = 1;
constexpr size_t MAX_TEXT_LENGTH = 64;
constexpr size_t MAX_ENCODED_SIZE = 256;

struct IPv4Address {
    uint8_t octets[4];

    bool isZero() const;
    bool operator==(const IPv4Address& other) const;
};

enum class ValidationStatus : uint8_t {
    OK = 0,
    TEXT_TOO_LONG,
    INVALID_PORT,
    STATIC_ADDRESS_REQUIRED,
    INVALID_STATIC_NETWORK,
};

enum class DecodeStatus : uint8_t {
    OK = 0,
    BAD_MAGIC,
    BAD_LENGTH,
    BAD_CRC,
    UNSUPPORTED_SCHEMA,
    INVALID_CONFIG,
};

struct Config {
    uint16_t schemaVersion;
    char hostname[MAX_TEXT_LENGTH + 1];
    bool useStaticIP;
    IPv4Address staticIP;
    IPv4Address subnet;
    IPv4Address gateway;
    IPv4Address dns1;
    IPv4Address dns2;
    uint16_t tcpPort;
    char tcpToken[MAX_TEXT_LENGTH + 1];
    char httpPassword[MAX_TEXT_LENGTH + 1];
    // Preserved for compatibility with the current shared configuration shape.
    // Task 2 does not add any further GPS settings.
    bool gpsEnabled;

    bool operator==(const Config& other) const;
};

Config makeDefaults(const char* hostname, uint16_t tcpPort, const char* tcpToken);
void sanitizeHostname(const char* input, char output[MAX_TEXT_LENGTH + 1]);
ValidationStatus validateAndNormalize(Config& config);

bool encode(const Config& config, uint8_t* output, size_t capacity, size_t& written);
DecodeStatus decode(const uint8_t* data, size_t length, Config& output);

// Recompute the encoded record's CRC after a test mutates header metadata.
// Returns false when the supplied record is too short or structurally invalid.
bool rewriteIntegrity(uint8_t* data, size_t length);

// RAK4631 InternalFS persistence API. The active in-memory configuration is
// loaded once by begin(); callers should reboot after a successful save before
// applying network, TCP port, or token changes.
bool begin();
const Config& getConfig();
bool saveConfig(const Config& config);
bool factoryReset();
const char* getEffectiveHostname();

}  // namespace Rak4631Config
