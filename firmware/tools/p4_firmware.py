"""ESP32-P4 artifact contract shared by asset staging and release validation.

Revision integers use Espressif's major * 100 + minor encoding. These are
SDK/image compatibility limits, not evidence of physical board validation.
"""
from __future__ import annotations

import hashlib
import json
import re
import shutil
import struct
from pathlib import Path

P4_PROFILES = {
    environment: {
        "board_id": board,
        "profile": profile,
        "chip_variant": variant,
        "min_revision": minimum,
        "max_revision": maximum,
    }
    for board in ("esp32_p4_nano", "ethermesh_1w")
    for environment, profile, variant, minimum, maximum in (
        (board, "pre_v3", "esp32p4_es", 1, 199),
        (board + "_v3", "v3", "esp32p4", 301, 399),
    )
}
PART_OFFSETS = {
    "bootloader.bin": 0x2000,
    "partitions.bin": 0x8000,
    "boot_app0.bin": 0xE000,
    "firmware.bin": 0x10000,
}


def capture_build_info(environment: str, platform: str, settings: dict,
                       framework: Path, libraries: Path, build_dir: Path) -> dict:
    """Record the resolved precompiled SDK while PlatformIO owns its packages."""
    profile = P4_PROFILES[environment]
    sdkconfig = libraries / profile["chip_variant"] / "sdkconfig"
    text = sdkconfig.read_text(encoding="utf-8")
    selected_config = dict(re.findall(
        r"^(CONFIG_(?:ESP(?:32P4)?_REV_[A-Z0-9_]+|RTC_CLK_SRC_[A-Z0-9_]+|"
        r"HAL_WDT_USE_ROM_IMPL|BOOTLOADER_WDT[A-Z0-9_]*|SPIRAM[A-Z0-9_]*))=(.+)$",
        text, re.MULTILINE,
    ))
    info = {
        "environment": environment, "board_id": profile["board_id"],
        "chip_variant": profile["chip_variant"], "platform": platform,
        "arduino_version": json.loads((framework / "package.json").read_text())["version"],
        "idf_package_version": json.loads((libraries / "package.json").read_text())["version"],
        "sdkconfig_sha256": hashlib.sha256(sdkconfig.read_bytes()).hexdigest(),
        "settings": settings, "sdkconfig": selected_config,
    }
    build_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(framework / "tools/partitions/boot_app0.bin", build_dir / "boot_app0.bin")
    (build_dir / "p4-build.json").write_text(json.dumps(info, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return info


def fingerprint(path: Path) -> dict:
    data = path.read_bytes()
    return {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def image_revisions(data: bytes) -> tuple[int, int]:
    if len(data) < 24 or data[0] != 0xE9:
        raise ValueError("Invalid ESP32 image header")
    chip, _legacy_min, minimum, maximum = struct.unpack_from("<HBHH", data, 12)
    if chip != 18:
        raise ValueError(f"Expected ESP32-P4 image chip ID 18, got {chip}")
    return minimum, maximum


def app_idf_version(data: bytes) -> str:
    # esp_app_desc_t is at the beginning of the first segment (24 + 8).
    if len(data) < 176 or struct.unpack_from("<I", data, 32)[0] != 0xABCD5432:
        raise ValueError("ESP32 application descriptor is missing")
    return data[144:176].split(b"\0", 1)[0].decode("ascii")


def part_metadata(path: Path, offset: int) -> dict:
    result = {"offset": offset, **fingerprint(path)}
    if path.name in ("bootloader.bin", "firmware.bin"):
        data = path.read_bytes()
        minimum, maximum = image_revisions(data)
        modes = {0: "qio", 1: "qout", 2: "dio", 3: "dout", 4: "fast_read", 5: "slow_read"}
        frequencies = {0: 40, 1: 26, 2: 20, 15: 80}
        size_code, frequency_code = data[3] >> 4, data[3] & 15
        if data[2] not in modes or frequency_code not in frequencies or size_code > 7:
            raise ValueError(f"Unsupported P4 image flash settings: {path.name}")
        result["image_header"] = {
            "chip_id": 18, "flash_mode": modes[data[2]],
            "flash_frequency_mhz": frequencies[frequency_code],
            "flash_size_bytes": (1 << size_code) * 1024 * 1024,
            "min_revision": minimum, "max_revision": maximum,
        }
    return result


def write_p4_metadata(environment: str, build_dir: Path, dest: Path) -> Path:
    info = json.loads((build_dir / "p4-build.json").read_text(encoding="utf-8"))
    profile = P4_PROFILES[environment]
    if any(info[key] != expected for key, expected in (
        ("environment", environment), ("board_id", profile["board_id"]),
        ("chip_variant", profile["chip_variant"]),
    )):
        raise ValueError("P4 build identity does not match the requested environment")
    minimum = int(info["sdkconfig"]["CONFIG_ESP_REV_MIN_FULL"])
    maximum = int(info["sdkconfig"]["CONFIG_ESP_REV_MAX_FULL"])
    parts = {
        name: part_metadata(dest / name, offset)
        for name, offset in PART_OFFSETS.items()
    }
    metadata = {
        "schema_version": 1,
        "environment": environment,
        "board_id": profile["board_id"],
        "chip_family": "ESP32-P4",
        "silicon": {**profile, "min_revision": minimum, "max_revision": maximum},
        "framework": {
            "platform": info["platform"],
            "arduino": info["arduino_version"],
            "esp_idf": app_idf_version((dest / "firmware.bin").read_bytes()),
            "idf_package": info["idf_package_version"],
            "sdkconfig_sha256": info["sdkconfig_sha256"],
            "sdkconfig": info["sdkconfig"],
        },
        "settings": info["settings"],
        "parts": parts,
        "factory": {"path": "firmware.factory.bin", "offset": 0,
                    **fingerprint(dest / "firmware.factory.bin")},
        "update_contract": {
            "board_id": profile["board_id"],
            "silicon_profile": profile["profile"],
            "flash_size_bytes": info["settings"]["flash_size_bytes"],
            "bootloader_sha256": parts["bootloader.bin"]["sha256"],
            "partitions_sha256": parts["partitions.bin"]["sha256"],
        },
    }
    validate_p4_bundle(environment, dest, metadata)
    output = dest / "firmware.json"
    output.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return output


def validate_partition_layout(data: bytes, application_size: int, flash_size: int) -> None:
    partitions = []
    for position in range(0, len(data) - 31, 32):
        magic, kind, subtype, offset, size, _label, _flags = struct.unpack_from("<HBBII16sI", data, position)
        if magic in (0xFFFF, 0xEBEB):
            break
        if magic != 0x50AA or not size:
            raise ValueError("Invalid P4 partition table")
        partitions.append((offset, size, kind, subtype))
    previous_end = 0x9000  # partition table occupies the 0x8000 sector
    for offset, size, _kind, _subtype in sorted(partitions):
        if offset < previous_end or offset + size > flash_size:
            raise ValueError("P4 partitions overlap or exceed the configured flash")
        previous_end = offset + size
    if (0xE000, 0x2000, 1, 0) not in partitions:
        raise ValueError("P4 OTA initialization partition does not match offset 0xe000")
    for subtype in (0x10, 0x11):
        slots = [part for part in partitions if part[2:] == (0, subtype)]
        if len(slots) != 1 or slots[0][1] < application_size:
            raise ValueError("P4 application must fit both OTA slots")
        if subtype == 0x10 and slots[0][0] != PART_OFFSETS["firmware.bin"]:
            raise ValueError("P4 application offset differs from the partition table")


def validate_p4_bundle(environment: str, directory: Path, metadata: dict | None = None) -> dict:
    """Fail closed on a missing or mixed board/silicon/component contract."""
    if metadata is None:
        metadata = json.loads((directory / "firmware.json").read_text(encoding="utf-8"))
    if not isinstance(metadata, dict):
        raise ValueError("P4 metadata must be a JSON object")
    profile = P4_PROFILES[environment]
    if (metadata["schema_version"] != 1 or metadata["environment"] != environment
            or metadata["board_id"] != profile["board_id"]
            or metadata["chip_family"] != "ESP32-P4" or metadata["silicon"] != profile):
        raise ValueError(f"P4 identity/revision contract mismatch for {environment}")
    framework = metadata["framework"]
    config = framework["sdkconfig"]
    version_match = re.search(r"(\d+)\.(\d+)\.(\d+)", framework["esp_idf"])
    if version_match is None:
        raise ValueError("P4 application has no recognizable ESP-IDF version")
    idf_version = tuple(int(part) for part in version_match.groups())
    package_match = re.search(r"(\d+)\.(\d+)\.(\d+)", framework["idf_package"])
    if package_match is None or tuple(int(part) for part in package_match.groups()) != idf_version:
        raise ValueError("P4 application descriptor disagrees with the resolved SDK package")
    fixed_wdt_rom = (5, 5, 5) <= idf_version < (6, 0, 0) or idf_version >= (6, 0, 1)
    if profile["profile"] == "v3" and not fixed_wdt_rom:
        raise ValueError("P4 production bundles require the AR2026-004 patched SDK")
    if (int(config["CONFIG_ESP_REV_MIN_FULL"]) != profile["min_revision"]
            or int(config["CONFIG_ESP_REV_MAX_FULL"]) != profile["max_revision"]):
        raise ValueError("P4 SDK revision limits differ from the bundle contract")
    factory = (directory / "firmware.factory.bin").read_bytes()
    for name, offset in PART_OFFSETS.items():
        part = metadata["parts"][name]
        data = (directory / name).read_bytes()
        if part != part_metadata(directory / name, offset):
            raise ValueError(f"P4 component metadata mismatch: {name}")
        if factory[offset:offset + len(data)] != data:
            raise ValueError(f"P4 factory component mismatch at 0x{offset:x}: {name}")
        if name in ("bootloader.bin", "firmware.bin"):
            minimum, maximum = image_revisions(data)
            # pioarduino's elf2image currently emits broad 0..65535 headers.
            # Record the SDK's narrower actual range; never infer support from
            # the broad header, but reject an explicitly contradictory image.
            if minimum > profile["min_revision"] or maximum < profile["max_revision"]:
                raise ValueError(f"P4 image revision limits contradict selected SDK: {name}")
    if metadata["factory"] != {"path": "firmware.factory.bin", "offset": 0,
                               **fingerprint(directory / "firmware.factory.bin")}:
        raise ValueError("P4 factory metadata mismatch")
    if metadata["framework"]["esp_idf"] != app_idf_version((directory / "firmware.bin").read_bytes()):
        raise ValueError("P4 application ESP-IDF descriptor mismatch")
    flash_size = metadata["settings"]["flash_size_bytes"]
    if flash_size != 16 * 1024 * 1024 or len(factory) > flash_size:
        raise ValueError("P4 factory exceeds or disagrees with the 16 MiB board flash")
    validate_partition_layout((directory / "partitions.bin").read_bytes(),
                              (directory / "firmware.bin").stat().st_size, flash_size)
    expected_update = {
        "board_id": profile["board_id"], "silicon_profile": profile["profile"],
        "flash_size_bytes": metadata["settings"]["flash_size_bytes"],
        "bootloader_sha256": metadata["parts"]["bootloader.bin"]["sha256"],
        "partitions_sha256": metadata["parts"]["partitions.bin"]["sha256"],
    }
    if metadata["update_contract"] != expected_update:
        raise ValueError("P4 app-only update contract mismatch")
    return metadata
