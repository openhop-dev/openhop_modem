#!/usr/bin/env python3
"""Deterministic host tests for the RAK4631 Ethernet OTA package."""

from __future__ import annotations

import hashlib
import importlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

FIRMWARE = Path(__file__).resolve().parents[1]
TOOLS = FIRMWARE / "tools"
sys.path.insert(0, str(TOOLS))

import package_nrf52_ota as ota  # noqa: E402


class OtaPackageTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.root = Path(self.tempdir.name)
        self.payload = self.root / "firmware.bin"
        self.payload_bytes = bytes(range(256)) * 5 + b"synthetic-openhop-rak4631"
        self.payload.write_bytes(self.payload_bytes)
        self.package = self.root / "firmware.ota"
        ota.create_package(self.payload, self.package, "v1.2.3-rak4631_wismesh_eth")

    def assert_rejected(self, data: bytes, message: str) -> None:
        candidate = self.root / "mutated.ota"
        candidate.write_bytes(data)
        with self.assertRaisesRegex(ota.PackageError, message):
            ota.inspect_package(candidate)

    def mutate_u16(self, data: bytes, offset: int, value: int) -> bytes:
        changed = bytearray(data)
        struct.pack_into("<H", changed, offset, value)
        return bytes(changed)

    def mutate_u32(self, data: bytes, offset: int, value: int) -> bytes:
        changed = bytearray(data)
        struct.pack_into("<I", changed, offset, value)
        return bytes(changed)

    def test_create_is_byte_identical_and_inspect_reports_exact_metadata(self) -> None:
        second = self.root / "second.ota"
        ota.create_package(self.payload, second, "v1.2.3-rak4631_wismesh_eth")
        self.assertEqual(self.package.read_bytes(), second.read_bytes())
        metadata = ota.inspect_package(self.package)
        self.assertEqual(metadata["target"], "rak4631_wismesh_eth")
        self.assertEqual(metadata["schema"], 1)
        self.assertEqual(metadata["application_origin"], 0x26000)
        self.assertEqual(metadata["softdevice_fwid"], 0x00B6)
        self.assertEqual(metadata["firmware_version"], "v1.2.3-rak4631_wismesh_eth")
        self.assertEqual(metadata["payload_length"], len(self.payload_bytes))
        self.assertEqual(metadata["payload_sha256"], hashlib.sha256(self.payload_bytes).hexdigest())
        self.assertEqual(metadata["signature_type"], "none")
        self.assertEqual(metadata["signature_length"], 0)
        self.assertFalse(metadata["authenticated"])

    def test_header_constants_match_platform_neutral_c_offsets(self) -> None:
        header = (FIRMWARE / "include/ota_package.h").read_text(encoding="utf-8")
        expected = {
            "OTA_PACKAGE_MAGIC_OFFSET": ota.MAGIC_OFFSET,
            "OTA_PACKAGE_SCHEMA_OFFSET": ota.SCHEMA_OFFSET,
            "OTA_PACKAGE_HEADER_LENGTH_OFFSET": ota.HEADER_LENGTH_OFFSET,
            "OTA_PACKAGE_TARGET_LENGTH_OFFSET": ota.TARGET_LENGTH_OFFSET,
            "OTA_PACKAGE_VERSION_LENGTH_OFFSET": ota.VERSION_LENGTH_OFFSET,
            "OTA_PACKAGE_SIGNATURE_TYPE_OFFSET": ota.SIGNATURE_TYPE_OFFSET,
            "OTA_PACKAGE_SIGNATURE_LENGTH_OFFSET": ota.SIGNATURE_LENGTH_OFFSET,
            "OTA_PACKAGE_APPLICATION_ORIGIN_OFFSET": ota.APPLICATION_ORIGIN_OFFSET,
            "OTA_PACKAGE_SOFTDEVICE_FWID_OFFSET": ota.SOFTDEVICE_FWID_OFFSET,
            "OTA_PACKAGE_PAYLOAD_LENGTH_OFFSET": ota.PAYLOAD_LENGTH_OFFSET,
            "OTA_PACKAGE_PAYLOAD_SHA256_OFFSET": ota.PAYLOAD_SHA256_OFFSET,
            "OTA_PACKAGE_FIXED_HEADER_LENGTH": ota.FIXED_HEADER_LENGTH,
            "OTA_PACKAGE_TARGET_MAX_LENGTH": ota.TARGET_MAX_LENGTH,
            "OTA_PACKAGE_VERSION_MAX_LENGTH": ota.VERSION_MAX_LENGTH,
            "OTA_PACKAGE_PAYLOAD_MAX_LENGTH": ota.PAYLOAD_MAX_LENGTH,
        }
        for name, value in expected.items():
            self.assertRegex(header, rf"\b{name}\s*=\s*(?:0x{value:X}|{value})\b")
        self.assertNotIn("packed", header.lower())
        self.assertNotIn("struct ", header)

    def test_rejects_each_invalid_fixed_header_field(self) -> None:
        original = self.package.read_bytes()
        cases = [
            (bytes([original[0] ^ 1]) + original[1:], "magic"),
            (self.mutate_u16(original, ota.SCHEMA_OFFSET, 2), "schema"),
            (self.mutate_u16(original, ota.HEADER_LENGTH_OFFSET, ota.FIXED_HEADER_LENGTH - 1), "header length"),
            (self.mutate_u16(original, ota.TARGET_LENGTH_OFFSET, 0), "target length"),
            (self.mutate_u16(original, ota.VERSION_LENGTH_OFFSET, 0), "version length"),
            (self.mutate_u16(original, ota.SIGNATURE_TYPE_OFFSET, 1), "signature type"),
            (self.mutate_u16(original, ota.SIGNATURE_LENGTH_OFFSET, 1), "signature length"),
            (self.mutate_u32(original, ota.APPLICATION_ORIGIN_OFFSET, 0x27000), "application origin"),
            (self.mutate_u32(original, ota.SOFTDEVICE_FWID_OFFSET, 0x0123), "SoftDevice FWID"),
            (self.mutate_u32(original, ota.PAYLOAD_LENGTH_OFFSET, len(self.payload_bytes) + 1), "truncated"),
        ]
        for changed, message in cases:
            with self.subTest(message=message):
                self.assert_rejected(changed, message)

    def test_rejects_bad_target_version_hash_payload_and_framing(self) -> None:
        original = bytearray(self.package.read_bytes())
        target_start = ota.FIXED_HEADER_LENGTH
        version_start = target_start + len(ota.TARGET_ID_BYTES)
        payload_start = struct.unpack_from("<H", original, ota.HEADER_LENGTH_OFFSET)[0]

        changed = bytearray(original)
        changed[target_start] ^= 1
        self.assert_rejected(bytes(changed), "target")

        changed = bytearray(original)
        changed[version_start] = 0xFF
        self.assert_rejected(bytes(changed), "firmware version")

        changed = bytearray(original)
        changed[ota.PAYLOAD_SHA256_OFFSET] ^= 1
        self.assert_rejected(bytes(changed), "hash mismatch")

        changed = bytearray(original)
        changed[payload_start] ^= 1
        self.assert_rejected(bytes(changed), "hash mismatch")

        self.assert_rejected(bytes(original[:-1]), "truncated")
        self.assert_rejected(bytes(original) + b"x", "trailing bytes")
        self.assert_rejected(bytes(original[: ota.FIXED_HEADER_LENGTH - 1]), "truncated header")

    def test_rejects_declared_string_lengths_above_maxima_and_inconsistent_header(self) -> None:
        original = self.package.read_bytes()
        self.assert_rejected(
            self.mutate_u16(original, ota.TARGET_LENGTH_OFFSET, ota.TARGET_MAX_LENGTH + 1),
            "target length",
        )
        self.assert_rejected(
            self.mutate_u16(original, ota.VERSION_LENGTH_OFFSET, ota.VERSION_MAX_LENGTH + 1),
            "version length",
        )
        self.assert_rejected(
            self.mutate_u16(original, ota.HEADER_LENGTH_OFFSET, ota.FIXED_HEADER_LENGTH),
            "header length",
        )

    def test_rejects_payload_over_slot_and_bad_create_inputs(self) -> None:
        with self.assertRaisesRegex(ota.PackageError, "payload length"):
            ota.create_package_bytes(b"", self.root / "empty.ota", "v1")
        too_large = self.root / "too-large.bin"
        too_large.write_bytes(b"x" * (ota.PAYLOAD_MAX_LENGTH + 1))
        with self.assertRaisesRegex(ota.PackageError, "payload length"):
            ota.create_package(too_large, self.root / "too-large.ota", "v1")
        for version in ("", "x" * (ota.VERSION_MAX_LENGTH + 1), "bad\x00version"):
            with self.subTest(version=version):
                with self.assertRaises(ota.PackageError):
                    ota.create_package(self.payload, self.root / "bad.ota", version)

    def test_cli_create_and_inspect_json(self) -> None:
        cli_package = self.root / "cli.ota"
        subprocess.run(
            [sys.executable, str(TOOLS / "package_nrf52_ota.py"), "create",
             str(self.payload), str(cli_package), "--firmware-version", "v9-test"],
            check=True,
            capture_output=True,
            text=True,
        )
        completed = subprocess.run(
            [sys.executable, str(TOOLS / "package_nrf52_ota.py"), "inspect", str(cli_package)],
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertEqual(json.loads(completed.stdout)["firmware_version"], "v9-test")


class AssetIntegrationTest(unittest.TestCase):
    def test_rak_platformio_post_build_generates_ota_only_for_rak(self) -> None:
        ini = (FIRMWARE / "platformio.ini").read_text(encoding="utf-8")
        rak = ini.split("[env:rak4631_wismesh_eth]", 1)[1]
        self.assertIn("post:tools/platformio_rak4631_ota.py", rak)
        before_rak = ini.split("[env:rak4631_wismesh_eth]", 1)[0]
        self.assertNotIn("platformio_rak4631_ota.py", before_rak)

    def test_asset_builder_stages_ota_only_for_rak_and_checksums_it(self) -> None:
        builder = importlib.import_module("build_firmware_assets")
        with tempfile.TemporaryDirectory() as tmp:
            fake_firmware = Path(tmp) / "firmware"
            rak_out = fake_firmware / ".pio/build/rak4631_wismesh_eth"
            rak_out.mkdir(parents=True)
            (rak_out / "firmware.hex").write_bytes(b":00000001FF\n")
            with zipfile.ZipFile(rak_out / "firmware.zip", "w") as archive:
                archive.writestr("firmware.bin", b"synthetic")
            # A stale but internally valid side-effect must be regenerated from
            # the exact Nordic DFU payload before assets are staged.
            ota.create_package_bytes(b"stale", rak_out / "firmware.ota", "v-test")

            with mock.patch.object(builder, "FIRMWARE", fake_firmware), \
                 mock.patch.object(builder, "ROOT", Path(tmp)), \
                 mock.patch.object(builder, "validate_nrf52_build"), \
                 mock.patch.object(builder, "firmware_version_from_source", return_value="v-test"), \
                 mock.patch.object(builder, "write_nrf52_uf2") as uf2:
                uf2.side_effect = lambda _hex, dest, _env: (dest / "firmware.uf2")
                def create_uf2(_hex, dest, _env):
                    path = dest / "firmware.uf2"
                    path.write_bytes(b"uf2")
                    return path
                uf2.side_effect = create_uf2
                builder.collect_env("rak4631_wismesh_eth")

            staged = fake_firmware / "rak4631_wismesh_eth"
            self.assertTrue((staged / "firmware.ota").is_file())
            ota.validate_package_matches_dfu(
                staged / "firmware.ota", staged / "firmware.zip", "v-test"
            )
            self.assertIn("firmware.ota", (staged / "SHA256SUMS.txt").read_text())

    def test_release_zip_includes_checksummed_ota_without_replacing_dfu_zip(self) -> None:
        release = importlib.import_module("package_release_assets")
        with tempfile.TemporaryDirectory() as tmp:
            fake_firmware = Path(tmp) / "firmware"
            asset = fake_firmware / "rak4631_wismesh_eth"
            asset.mkdir(parents=True)
            ota_path = asset / "firmware.ota"
            ota.create_package_bytes(b"ethernet", ota_path, "v-test")
            zip_path = asset / "firmware.zip"
            with zipfile.ZipFile(zip_path, "w") as archive:
                archive.writestr("firmware.bin", b"ethernet")
            files = {
                "firmware.zip": zip_path.read_bytes(),
                "firmware.ota": ota_path.read_bytes(),
            }
            for name, data in files.items():
                (asset / name).write_bytes(data)
            (asset / "SHA256SUMS.txt").write_text("".join(
                f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in files.items()
            ))
            output = Path(tmp) / "release"
            with mock.patch.object(release, "FIRMWARE", fake_firmware), \
                 mock.patch.object(release, "firmware_version_from_source", return_value="v-test"):
                zip_paths, _, _ = release.package("v-test", output)
            with zipfile.ZipFile(zip_paths[0]) as archive:
                self.assertIn("firmware.ota", archive.namelist())
                self.assertIn("firmware.zip", archive.namelist())
                self.assertEqual(archive.read("firmware.zip"), zip_path.read_bytes())

    def test_rejects_ota_that_does_not_match_dfu_payload(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            zip_path = root / "firmware.zip"
            ota_path = root / "firmware.ota"
            with zipfile.ZipFile(zip_path, "w") as archive:
                archive.writestr("firmware.bin", b"payload-a")
            ota.create_package_bytes(b"payload-b", ota_path, "v-test")
            with self.assertRaisesRegex(ota.PackageError, "does not match"):
                ota.validate_package_matches_dfu(ota_path, zip_path, "v-test")


if __name__ == "__main__":
    unittest.main()
