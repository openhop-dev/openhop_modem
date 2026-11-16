#!/usr/bin/env python3
"""Host-only P4 silicon/artifact contracts; no builds, downloads or hardware."""
from __future__ import annotations

import configparser
import json
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

import build_firmware_assets as builder
import p4_firmware
import package_release_assets as release

FIRMWARE = Path(__file__).resolve().parents[1]
P4_PAIRS = (
    ("esp32_p4_nano", "esp32_p4_nano_v3"),
    ("ethermesh_1w", "ethermesh_1w_v3"),
)


def fixture_images(root, environment="ethermesh_1w_v3", minimum=301, maximum=399):
    """Synthetic host fixture, never a flashable firmware artifact."""
    out = root / "firmware/.pio/build" / environment
    out.mkdir(parents=True)
    image = bytearray(256)
    image[0:4] = bytes((0xE9, 1, 2, 0x40))
    struct.pack_into("<HBHH", image, 12, 18, 0, minimum, maximum)
    struct.pack_into("<I", image, 32, 0xABCD5432)
    image[144:151] = b"v5.5.5\0" if environment.endswith("_v3") else b"v5.5.4\0"
    (out / "firmware.bin").write_bytes(image)
    (out / "bootloader.bin").write_bytes(image[:24] + b"bootloader-fixture")
    table = bytearray()
    for kind, subtype, offset, size, name in (
        (1, 2, 0x9000, 0x5000, b"nvs"),
        (1, 0, 0xE000, 0x2000, b"otadata"),
        (0, 0x10, 0x10000, 0x640000, b"app0"),
        (0, 0x11, 0x650000, 0x640000, b"app1"),
    ):
        table.extend(struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size, name, 0))
    (out / "partitions.bin").write_bytes(table + b"\xff" * (0xC00 - len(table)))
    (out / "boot_app0.bin").write_bytes(b"\xff" * 0x2000)
    factory = bytearray(b"\xff" * (0x10000 + len(image)))
    for name, offset in (("bootloader.bin", 0x2000), ("partitions.bin", 0x8000),
                         ("boot_app0.bin", 0xE000), ("firmware.bin", 0x10000)):
        data = (out / name).read_bytes()
        factory[offset:offset + len(data)] = data
    (out / "firmware.factory.bin").write_bytes(factory)
    profile = "v3" if environment.endswith("_v3") else "pre_v3"
    board_id = environment.removesuffix("_v3")
    info = {
        "environment": environment, "board_id": board_id,
        "chip_variant": "esp32p4" if profile == "v3" else "esp32p4_es",
        "platform": "https://github.com/pioarduino/platform-espressif32/releases/download/"
                    + ("55.03.311" if profile == "v3" else "55.03.38-1") + "/platform-espressif32.zip",
        "arduino_version": "3.3.11" if profile == "v3" else "3.3.8",
        "idf_package_version": "5.5.5+sha.b774170ff46" if profile == "v3" else "5.5.4+sha.735507283d",
        "sdkconfig_sha256": "0" * 64,
        "settings": {"board": "esp32-p4-evboard", "cpu_hz": 400000000 if profile == "v3" else 360000000,
                     "flash_hz": 80000000, "flash_mode": "qio", "flash_size_bytes": 16777216,
                     "psram_hz": 200000000, "psram_enabled": True},
        "sdkconfig": {"CONFIG_ESP_REV_MIN_FULL": str(minimum), "CONFIG_ESP_REV_MAX_FULL": str(maximum),
                      "CONFIG_RTC_CLK_SRC_INT_RC": "y", "CONFIG_HAL_WDT_USE_ROM_IMPL": "y",
                      "CONFIG_BOOTLOADER_WDT_ENABLE": "y", "CONFIG_BOOTLOADER_WDT_TIME_MS": "9000",
                      "CONFIG_SPIRAM_MODE_HEX": "y", "CONFIG_SPIRAM_SPEED": "200"},
    }
    (out / "p4-build.json").write_text(json.dumps(info))
    return out


class AssetTests(unittest.TestCase):
    def test_collector_rejects_stale_sdk_or_claimed_package_version(self):
        for defect in ("old_sdk", "package_mismatch"):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory(prefix="modem87-test-") as directory:
                root = Path(directory)
                out = fixture_images(root)
                info = json.loads((out / "p4-build.json").read_text())
                info["idf_package_version"] = "5.5.4+sha.735507283d"
                (out / "p4-build.json").write_text(json.dumps(info))
                if defect == "old_sdk":
                    application = bytearray((out / "firmware.bin").read_bytes())
                    application[144:151] = b"v5.5.4\0"
                    (out / "firmware.bin").write_bytes(application)
                    factory = bytearray((out / "firmware.factory.bin").read_bytes())
                    factory[0x10000:0x10000 + len(application)] = application
                    (out / "firmware.factory.bin").write_bytes(factory)
                with patch.object(builder, "ROOT", root), patch.object(builder, "FIRMWARE", root / "firmware"):
                    with self.assertRaises(ValueError, msg=f"Accepted stale P4 {defect} evidence"):
                        builder.collect_env("ethermesh_1w_v3")

    def test_external_staging_does_not_replace_tracked_asset_directories(self):
        with tempfile.TemporaryDirectory(prefix="modem87-test-") as directory:
            root = Path(directory)
            plan = subprocess.run([sys.executable, str(FIRMWARE / "tools/build_firmware_assets.py"),
                                   "--variant", "ethermesh_1w_v3", "--plan", "--output-dir", str(root / "assets")],
                                  capture_output=True, text=True)
            self.assertEqual(plan.returncode, 0, plan.stdout + plan.stderr)
            fixture_images(root)
            tracked = root / "firmware/ethermesh_1w_v3"
            tracked.mkdir()
            (tracked / "keep.txt").write_text("existing user artifact")
            with patch.object(builder, "ROOT", root), patch.object(builder, "FIRMWARE", root / "firmware"):
                builder.collect_env("ethermesh_1w_v3", output_dir=root / "assets")
            self.assertEqual((tracked / "keep.txt").read_text(), "existing user artifact")
            self.assertTrue((root / "assets/ethermesh_1w_v3/firmware.json").is_file())

    def test_collector_rejects_unsafe_ota_layout_or_external_xtal_startup(self):
        for defect in ("too_small", "overlap", "missing_app1", "external_xtal"):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory(prefix="modem87-test-") as directory:
                root = Path(directory)
                out = fixture_images(root)
                if defect == "external_xtal":
                    info = json.loads((out / "p4-build.json").read_text())
                    del info["sdkconfig"]["CONFIG_RTC_CLK_SRC_INT_RC"]
                    info["sdkconfig"]["CONFIG_RTC_CLK_SRC_EXT_CRYS"] = "y"
                    info["idf_package_version"] = "5.5.4+sha.735507283d"
                    (out / "p4-build.json").write_text(json.dumps(info))
                    application = bytearray((out / "firmware.bin").read_bytes())
                    application[144:151] = b"v5.5.4\0"
                    (out / "firmware.bin").write_bytes(application)
                    factory = bytearray((out / "firmware.factory.bin").read_bytes())
                    factory[0x10000:0x10000 + len(application)] = application
                    (out / "firmware.factory.bin").write_bytes(factory)
                else:
                    data = bytearray((out / "partitions.bin").read_bytes())
                    if defect == "too_small":
                        struct.pack_into("<I", data, 2 * 32 + 8, 16)
                    elif defect == "overlap":
                        struct.pack_into("<I", data, 3 * 32 + 4, 0x20000)
                    else:
                        data[3 * 32 + 3] = 0x12
                    (out / "partitions.bin").write_bytes(data)
                    factory = bytearray((out / "firmware.factory.bin").read_bytes())
                    factory[0x8000:0x8000 + len(data)] = data
                    (out / "firmware.factory.bin").write_bytes(factory)
                with patch.object(builder, "ROOT", root), patch.object(builder, "FIRMWARE", root / "firmware"):
                    with self.assertRaises(ValueError, msg=f"Accepted unsafe P4 {defect} configuration"):
                        builder.collect_env("ethermesh_1w_v3")

    def test_collector_rejects_wrong_silicon_or_mismatched_factory(self):
        for defect in ("old_application", "old_bootloader", "factory", "sdk_revision", "board_identity"):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory(prefix="modem87-test-") as directory:
                root = Path(directory)
                out = fixture_images(root)
                if defect in ("old_application", "old_bootloader"):
                    name = "firmware.bin" if defect == "old_application" else "bootloader.bin"
                    data = bytearray((out / name).read_bytes())
                    struct.pack_into("<HH", data, 15, 1, 199)
                    (out / name).write_bytes(data)
                    factory = bytearray((out / "firmware.factory.bin").read_bytes())
                    offset = p4_firmware.PART_OFFSETS[name]
                    factory[offset:offset + len(data)] = data
                    (out / "firmware.factory.bin").write_bytes(factory)
                elif defect == "factory":
                    data = bytearray((out / "firmware.factory.bin").read_bytes())
                    data[0x2000] ^= 1
                    (out / "firmware.factory.bin").write_bytes(data)
                else:
                    info = json.loads((out / "p4-build.json").read_text())
                    if defect == "sdk_revision":
                        info["sdkconfig"]["CONFIG_ESP_REV_MIN_FULL"] = "1"
                    else:
                        info["board_id"] = "esp32_p4_nano"
                    (out / "p4-build.json").write_text(json.dumps(info))
                with patch.object(builder, "ROOT", root), patch.object(builder, "FIRMWARE", root / "firmware"):
                    with self.assertRaises(ValueError, msg=f"Accepted invalid P4 {defect} bundle"):
                        builder.collect_env("ethermesh_1w_v3")

    def test_collector_emits_checked_revision_contract_and_split_ota_data(self):
        with tempfile.TemporaryDirectory(prefix="modem87-test-") as directory:
            root = Path(directory)
            for legacy, production in P4_PAIRS:
                for environment, minimum, maximum in ((legacy, 1, 199), (production, 301, 399)):
                    fixture_images(root, environment, minimum, maximum)
                    with patch.object(builder, "ROOT", root), patch.object(builder, "FIRMWARE", root / "firmware"):
                        builder.collect_env(environment)
                    dest = root / "firmware" / environment
                    self.assertTrue((dest / "firmware.json").is_file(), "P4 compatibility metadata is missing")
                    self.assertTrue((dest / "boot_app0.bin").is_file())
                    metadata = json.loads((dest / "firmware.json").read_text())
                    self.assertEqual(metadata["environment"], environment)
                    self.assertEqual(metadata["board_id"], legacy)
                    self.assertEqual(metadata["silicon"]["min_revision"], minimum)
                    self.assertEqual(metadata["silicon"]["max_revision"], maximum)
                    self.assertEqual(metadata["framework"]["arduino"], "3.3.11" if environment == production else "3.3.8")
                    self.assertEqual(metadata["framework"]["esp_idf"], "v5.5.5" if environment == production else "v5.5.4")
                    self.assertEqual(metadata["parts"]["bootloader.bin"]["offset"], 0x2000)
                    self.assertEqual(metadata["parts"]["boot_app0.bin"]["offset"], 0xE000)
                    self.assertIn("image_header", metadata["parts"]["firmware.bin"], "Configured QIO must be distinguished from the DIO image header")
                    self.assertEqual(metadata["settings"]["flash_mode"], "qio")
                    self.assertEqual(metadata["parts"]["firmware.bin"]["image_header"]["flash_mode"], "dio")
                    self.assertEqual(metadata["parts"]["firmware.bin"]["image_header"]["min_revision"], minimum)
                    self.assertEqual(metadata["update_contract"]["board_id"], legacy)
                    sums = (dest / "SHA256SUMS.txt").read_text()
                    self.assertIn("  firmware.json\n", sums)
                    self.assertIn("  boot_app0.bin\n", sums)


class ReleaseTests(unittest.TestCase):
    def test_missing_production_assets_never_fall_back_to_legacy(self):
        with tempfile.TemporaryDirectory(prefix="modem87-test-") as directory:
            root = Path(directory)
            (root / "platformio.ini").write_text("\n".join(f"[env:{env}]" for env in p4_firmware.P4_PROFILES))
            for legacy, _production in P4_PAIRS:
                dest = root / legacy
                dest.mkdir()
                (dest / "SHA256SUMS.txt").write_text("unused fixture\n")
            with patch.object(release, "FIRMWARE", root), self.assertRaises(SystemExit) as error:
                release.discover_asset_dirs()
            for _legacy, production in P4_PAIRS:
                self.assertIn(production, str(error.exception))

    def test_non_p4_checksums_do_not_require_p4_metadata(self):
        with tempfile.TemporaryDirectory(prefix="modem87-test-") as directory:
            dest = Path(directory) / "heltec_v3"
            dest.mkdir()
            (dest / "firmware.bin").write_bytes(b"non-P4 host fixture")
            builder.write_sha256s(dest, [dest / "firmware.bin"])
            self.assertEqual(release.checksummed_files(dest), [dest / "firmware.bin"])

    def test_p4_release_zips_are_complete_deterministic_and_roundtrip_checksums(self):
        with tempfile.TemporaryDirectory(prefix="modem87-test-") as directory:
            root = Path(directory)
            firmware = root / "firmware"
            for legacy, production in P4_PAIRS:
                for environment, minimum, maximum in ((legacy, 1, 199), (production, 301, 399)):
                    fixture_images(root, environment, minimum, maximum)
                    with patch.object(builder, "ROOT", root), patch.object(builder, "FIRMWARE", firmware):
                        builder.collect_env(environment)
            (firmware / "platformio.ini").write_text("\n".join(f"[env:{env}]" for env in p4_firmware.P4_PROFILES))
            with patch.object(release, "FIRMWARE", firmware):
                first, first_sums, envs = release.package("host-test", root / "zip-a")
                second, second_sums, other_envs = release.package("host-test", root / "zip-b")
            self.assertEqual(set(envs), set(p4_firmware.P4_PROFILES))
            self.assertEqual(envs, other_envs)
            self.assertEqual(first_sums.read_bytes(), second_sums.read_bytes())
            for left, right in zip(first, second, strict=True):
                self.assertEqual(left.read_bytes(), right.read_bytes())
                with zipfile.ZipFile(left) as archive:
                    names = archive.namelist()
                    self.assertEqual(len(names), len(set(names)))
                    self.assertEqual(set(names), {*p4_firmware.PART_OFFSETS, "firmware.factory.bin",
                                                  "firmware.json", "SHA256SUMS.txt", "README.txt"})
                    self.assertIsNone(archive.testzip())
                    metadata = json.loads(archive.read("firmware.json"))
                    extracted = root / "extracted" / metadata["environment"]
                    archive.extractall(extracted)
                    release.checksummed_files(extracted)

    def test_release_rejects_missing_or_wrong_board_metadata_even_with_valid_checksums(self):
        for defect in ("missing", "wrong_board", "missing_checksum_entry"):
            with self.subTest(defect=defect), tempfile.TemporaryDirectory(prefix="modem87-test-") as directory:
                root = Path(directory)
                fixture_images(root)
                with patch.object(builder, "ROOT", root), patch.object(builder, "FIRMWARE", root / "firmware"):
                    builder.collect_env("ethermesh_1w_v3")
                dest = root / "firmware/ethermesh_1w_v3"
                metadata_path = dest / "firmware.json"
                if defect == "missing":
                    metadata_path.unlink()
                elif defect == "wrong_board":
                    data = json.loads(metadata_path.read_text())
                    data["board_id"] = "esp32_p4_nano"
                    metadata_path.write_text(json.dumps(data))
                files = sorted(path for path in dest.iterdir() if path.name != "SHA256SUMS.txt"
                               and not (defect == "missing_checksum_entry" and path.name == "firmware.json"))
                builder.write_sha256s(dest, files)
                with self.assertRaises((SystemExit, ValueError, FileNotFoundError),
                                       msg=f"Release accepted {defect} compatibility metadata"):
                    release.checksummed_files(dest)


class BuildInfoTests(unittest.TestCase):
    def test_capture_uses_selected_sdk_not_the_other_p4_library_tree(self):
        self.assertTrue(hasattr(p4_firmware, "capture_build_info"), "P4 builds do not record their resolved SDK")
        with tempfile.TemporaryDirectory(prefix="modem87-test-") as directory:
            root = Path(directory)
            framework = root / "framework"
            libs = root / "libs"
            (framework / "tools/partitions").mkdir(parents=True)
            (framework / "package.json").write_text('{"version":"3.3.8"}')
            (framework / "tools/partitions/boot_app0.bin").write_bytes(b"test-otadata")
            libs.mkdir()
            (libs / "package.json").write_text('{"version":"5.5.4+sha.735507283d"}')
            for environment, minimum, maximum in (("ethermesh_1w", 1, 199), ("ethermesh_1w_v3", 301, 399)):
                out = fixture_images(root, environment, minimum, maximum)
                expected = json.loads((out / "p4-build.json").read_text())
                selected = libs / expected["chip_variant"]
                selected.mkdir()
                cfg = "\n".join(f"{key}={value}" for key, value in expected["sdkconfig"].items()) + "\n"
                (selected / "sdkconfig").write_text(cfg)
                actual = p4_firmware.capture_build_info(
                    environment, expected["platform"], expected["settings"], framework, libs, out,
                )
                self.assertEqual(actual["chip_variant"], expected["chip_variant"])
                self.assertEqual(actual["sdkconfig"], expected["sdkconfig"])
                self.assertEqual(actual["arduino_version"], "3.3.8")
                self.assertEqual((out / "boot_app0.bin").read_bytes(), b"test-otadata")
                self.assertEqual(json.loads((out / "p4-build.json").read_text()), actual)
                section = configparser.ConfigParser(interpolation=None)
                section.read(FIRMWARE / "platformio.ini")
                self.assertEqual(section["env:ethermesh_1w"]["extra_scripts"], "post:tools/platformio_p4_metadata.py")
                self.assertEqual(section["env:esp32_p4_nano"]["extra_scripts"], "post:tools/platformio_p4_metadata.py")


class ProfileTests(unittest.TestCase):
    def test_p4_asset_contract_changes_select_only_the_four_p4_builds(self):
        environments = builder.discover_envs()
        expected = [name for name in environments if name in p4_firmware.P4_PROFILES]
        for path in ("firmware/tools/p4_firmware.py", "firmware/tools/platformio_p4_metadata.py"):
            with self.subTest(path=path), patch.object(builder, "git_changed_files", return_value=[path]):
                self.assertEqual(builder.select_envs("auto", environments, "base", "head"), expected)

    def test_board_header_change_builds_both_silicon_profiles(self):
        environments = builder.discover_envs()
        for legacy, production in P4_PAIRS:
            with self.subTest(board=legacy):
                with patch.object(builder, "git_changed_files", return_value=[
                    f"firmware/include/boards/{legacy}.h"
                ]):
                    self.assertEqual(
                        builder.select_envs("auto", environments, "base", "head"),
                        [legacy, production],
                    )
                self.assertEqual(builder.select_envs(legacy, environments, None, None), [legacy])
                self.assertEqual(builder.ENV_METADATA[production]["chip_family"], "ESP32-P4")
                self.assertFalse(builder.ENV_METADATA[production]["web_manifest"])

    def test_production_profiles_preserve_board_policy_and_legacy_selection(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(FIRMWARE / "platformio.ini")
        environments = builder.discover_envs()
        for legacy, production in P4_PAIRS:
            with self.subTest(board=legacy):
                self.assertIn(production, environments)
                old = config[f"env:{legacy}"]
                new = config[f"env:{production}"]
                self.assertEqual(old["board"], "esp32-p4-evboard")
                self.assertEqual(new["extends"], f"env:{legacy}")
                self.assertEqual(new["board_build.chip_variant"], "esp32p4")
                self.assertEqual(new["board_build.f_cpu"], "400000000L")
                self.assertEqual(old["platform"], "https://github.com/pioarduino/platform-espressif32/releases/download/55.03.38-1/platform-espressif32.zip")
                self.assertEqual(new.get("platform"), "https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip")
                # Inherit the exact GPIO/RF macro, USB policy and flash layout.
                # Only production targets get the v3.2-compatible SDK/uploader.
                for key in ("board", "build_flags", "lib_deps",
                            "board_build.partitions", "board_build.flash_size"):
                    self.assertNotIn(key, new)


if __name__ == "__main__":
    unittest.main(verbosity=2)
