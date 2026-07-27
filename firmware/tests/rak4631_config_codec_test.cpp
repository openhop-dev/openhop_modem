#include "rak4631_config.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace Rak4631Config;

namespace {

void setText(char (&dest)[MAX_TEXT_LENGTH + 1], const std::string& value) {
    assert(value.size() <= MAX_TEXT_LENGTH);
    std::memset(dest, 0, sizeof(dest));
    std::memcpy(dest, value.data(), value.size());
}

IPv4Address ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return IPv4Address{{a, b, c, d}};
}

std::vector<uint8_t> encodeConfig(const Config& config) {
    std::vector<uint8_t> bytes(MAX_ENCODED_SIZE);
    size_t written = 0;
    assert(encode(config, bytes.data(), bytes.size(), written));
    bytes.resize(written);
    return bytes;
}

void testDefaults() {
    Config config = makeDefaults("openhop-rak4631-eth", 5055, "build-token");
    assert(config.schemaVersion == SCHEMA_VERSION);
    assert(std::strcmp(config.hostname, "openhop-rak4631-eth") == 0);
    assert(!config.useStaticIP);
    assert(config.tcpPort == 5055);
    assert(std::strcmp(config.tcpToken, "build-token") == 0);
    assert(std::strcmp(config.httpPassword, "password") == 0);
    assert(!config.gpsEnabled);
}

void testValidRoundTrip() {
    Config source = makeDefaults("", 6000, "");
    setText(source.hostname, "rack-gateway-01");
    source.useStaticIP = true;
    source.staticIP = ip(192, 168, 4, 20);
    source.subnet = ip(255, 255, 255, 0);
    source.gateway = ip(192, 168, 4, 1);
    source.dns1 = ip(1, 1, 1, 1);
    source.dns2 = ip(8, 8, 8, 8);
    source.tcpPort = 6123;
    setText(source.tcpToken, "secret-token");
    setText(source.httpPassword, "new-password");
    source.gpsEnabled = true;

    const auto bytes = encodeConfig(source);
    Config decoded{};
    assert(decode(bytes.data(), bytes.size(), decoded) == DecodeStatus::OK);
    assert(decoded == source);
}

void testMaximumTextLengths() {
    const std::string maximum(MAX_TEXT_LENGTH, 'a');
    Config source = makeDefaults("", 5055, "");
    setText(source.hostname, maximum);
    setText(source.tcpToken, maximum);
    setText(source.httpPassword, maximum);

    const auto bytes = encodeConfig(source);
    Config decoded{};
    assert(decode(bytes.data(), bytes.size(), decoded) == DecodeStatus::OK);
    assert(std::strlen(decoded.hostname) == 63);
    assert(std::strlen(decoded.tcpToken) == MAX_TEXT_LENGTH);
    assert(std::strlen(decoded.httpPassword) == MAX_TEXT_LENGTH);

    Config invalid = source;
    std::memset(invalid.tcpToken, 'x', sizeof(invalid.tcpToken));
    size_t written = 0;
    uint8_t output[MAX_ENCODED_SIZE]{};
    assert(!encode(invalid, output, sizeof(output), written));
}

void testHostnameSanitization() {
    char output[MAX_TEXT_LENGTH + 1]{};
    sanitizeHostname(" --My Gateway__A...B-- ", output);
    assert(std::strcmp(output, "my-gateway-a-b") == 0);

    Config config = makeDefaults("", 5055, "");
    setText(config.hostname, "Bad HOST_name!");
    assert(validateAndNormalize(config) == ValidationStatus::OK);
    assert(std::strcmp(config.hostname, "bad-host-name") == 0);
    sanitizeHostname(std::string(64, 'a').c_str(), output);
    assert(std::strlen(output) == 63);
}

void testCrcFailure() {
    Config source = makeDefaults("host", 5055, "token");
    auto bytes = encodeConfig(source);
    bytes.back() ^= 0x80;
    Config decoded{};
    assert(decode(bytes.data(), bytes.size(), decoded) == DecodeStatus::BAD_CRC);
}

void testTruncatedFile() {
    Config source = makeDefaults("host", 5055, "token");
    auto bytes = encodeConfig(source);
    bytes.pop_back();
    Config decoded{};
    assert(decode(bytes.data(), bytes.size(), decoded) == DecodeStatus::BAD_LENGTH);
}

void testFutureSchemaRejected() {
    Config source = makeDefaults("host", 5055, "token");
    auto bytes = encodeConfig(source);
    bytes[4] = static_cast<uint8_t>(SCHEMA_VERSION + 1);
    bytes[5] = 0;
    rewriteIntegrity(bytes.data(), bytes.size());
    Config decoded{};
    assert(decode(bytes.data(), bytes.size(), decoded) == DecodeStatus::UNSUPPORTED_SCHEMA);
}

void testStaticModeRequiresAddresses() {
    Config source = makeDefaults("host", 5055, "token");
    source.useStaticIP = true;
    assert(validateAndNormalize(source) == ValidationStatus::STATIC_ADDRESS_REQUIRED);

    source.staticIP = ip(10, 0, 0, 2);
    source.subnet = ip(255, 255, 255, 0);
    source.gateway = ip(10, 0, 0, 1);
    assert(validateAndNormalize(source) == ValidationStatus::OK);
}

void testStaticModeRejectsInvalidNetworkTuples() {
    Config source = makeDefaults("host", 5055, "token");
    source.useStaticIP = true;
    source.staticIP = ip(192, 168, 4, 20);
    source.subnet = ip(255, 0, 255, 0);
    source.gateway = ip(192, 168, 4, 1);
    assert(validateAndNormalize(source) == ValidationStatus::INVALID_STATIC_NETWORK);

    source.subnet = ip(255, 255, 255, 0);
    source.gateway = ip(192, 168, 5, 1);
    assert(validateAndNormalize(source) == ValidationStatus::INVALID_STATIC_NETWORK);

    source.gateway = ip(192, 168, 4, 1);
    source.staticIP = ip(192, 168, 4, 255);
    assert(validateAndNormalize(source) == ValidationStatus::INVALID_STATIC_NETWORK);

    source.staticIP = source.gateway;
    assert(validateAndNormalize(source) == ValidationStatus::INVALID_STATIC_NETWORK);

    source.staticIP = ip(192, 168, 4, 20);
    source.dns1 = ip(224, 0, 0, 1);
    assert(validateAndNormalize(source) == ValidationStatus::INVALID_STATIC_NETWORK);
}

void testHttpPortIsReserved() {
    Config source = makeDefaults("host", 80, "token");
    assert(source.tcpPort == 5055);
    source.tcpPort = 80;
    assert(validateAndNormalize(source) == ValidationStatus::INVALID_PORT);
}

}  // namespace

int main() {
    testDefaults();
    testValidRoundTrip();
    testMaximumTextLengths();
    testHostnameSanitization();
    testCrcFailure();
    testTruncatedFile();
    testFutureSchemaRejected();
    testStaticModeRequiresAddresses();
    testStaticModeRejectsInvalidNetworkTuples();
    testHttpPortIsReserved();
    std::cout << "rak4631 config codec tests passed\n";
    return 0;
}
