#pragma once

// Byte-level RAK4631 Ethernet OTA package contract. Firmware parsers must read
// little-endian fields at these offsets and validate lengths before accessing
// variable data. SHA-256 provides corruption detection, not authenticity.
#include <stdint.h>

namespace OtaPackage {

static constexpr uint8_t MAGIC[8] = {
    0x4f, 0x48, 0x4f, 0x54, 0x41, 0x0d, 0x0a, 0x1a  // "OHOTA" + marker
};
static constexpr char TARGET_ID[] = "rak4631_wismesh_eth";

enum : uint32_t {
    OTA_PACKAGE_MAGIC_OFFSET = 0,
    OTA_PACKAGE_SCHEMA_OFFSET = 8,
    OTA_PACKAGE_HEADER_LENGTH_OFFSET = 10,
    OTA_PACKAGE_TARGET_LENGTH_OFFSET = 12,
    OTA_PACKAGE_VERSION_LENGTH_OFFSET = 14,
    OTA_PACKAGE_SIGNATURE_TYPE_OFFSET = 16,
    OTA_PACKAGE_SIGNATURE_LENGTH_OFFSET = 18,
    OTA_PACKAGE_APPLICATION_ORIGIN_OFFSET = 20,
    OTA_PACKAGE_SOFTDEVICE_FWID_OFFSET = 24,
    OTA_PACKAGE_PAYLOAD_LENGTH_OFFSET = 28,
    OTA_PACKAGE_PAYLOAD_SHA256_OFFSET = 32,
    OTA_PACKAGE_FIXED_HEADER_LENGTH = 64,

    OTA_PACKAGE_SCHEMA = 1,
    OTA_PACKAGE_SIGNATURE_NONE = 0,
    OTA_PACKAGE_TARGET_MAX_LENGTH = 32,
    OTA_PACKAGE_VERSION_MAX_LENGTH = 64,
    OTA_PACKAGE_HEADER_MAX_LENGTH = 160,
    OTA_PACKAGE_PAYLOAD_MAX_LENGTH = 0x62000,
    OTA_PACKAGE_APPLICATION_ORIGIN = 0x26000,
    OTA_PACKAGE_SOFTDEVICE_FWID = 0x00B6,
    OTA_PACKAGE_SHA256_LENGTH = 32,
};

}  // namespace OtaPackage
