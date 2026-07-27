#!/usr/bin/env python3
"""Host tests for the RAK4631 flash-layout contract."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
FIRMWARE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

from nrf52_flash_layout import (  # noqa: E402
    RAK4631_LAYOUT,
    FlashLayout,
    validate_rak4631_artifact,
    validate_rak4631_board,
)
from build_firmware_assets import validate_nrf52_build  # noqa: E402


class FlashLayoutTest(unittest.TestCase):
    def test_rak4631_regions_match_bootloader_dual_bank_contract(self) -> None:
        layout = RAK4631_LAYOUT
        self.assertEqual(layout.application_start, 0x26000)
        self.assertEqual(layout.application_size, 0x62000)
        self.assertEqual(layout.staging_start, 0x88000)
        self.assertEqual(layout.staging_size, 0x62000)
        self.assertEqual(layout.app_data_start, 0xEA000)
        self.assertEqual(layout.littlefs_start, 0xED000)
        self.assertEqual(layout.littlefs_size, 0x7000)
        self.assertEqual(layout.bootloader_start, 0xF4000)
        self.assertEqual(layout.mbr_params_start, 0xFE000)
        self.assertEqual(layout.settings_start, 0xFF000)
        layout.validate()

    def test_overlap_is_rejected(self) -> None:
        layout = FlashLayout(
            application_start=0x26000,
            application_size=0x63000,
            staging_start=0x88000,
            staging_size=0x62000,
            app_data_start=0xEA000,
            littlefs_start=0xED000,
            littlefs_size=0x7000,
            bootloader_start=0xF4000,
            mbr_params_start=0xFE000,
            settings_start=0xFF000,
            flash_end=0x100000,
        )
        with self.assertRaisesRegex(ValueError, "application overlaps staging"):
            layout.validate()

    def test_artifact_must_fit_active_and_staging_slots(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            artifact = Path(tmp) / "firmware.bin"
            artifact.write_bytes(b"x" * 1024)
            validate_rak4631_artifact(artifact)
            artifact.write_bytes(b"x" * (RAK4631_LAYOUT.application_size + 1))
            with self.assertRaisesRegex(ValueError, "exceeds RAK4631 active slot"):
                validate_rak4631_artifact(artifact)

    def test_board_metadata_must_enforce_custom_linker_and_slot_size(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            board = Path(tmp) / "board.json"
            board.write_text(json.dumps({
                "build": {"arduino": {"ldscript": "nrf52840_s140_v6_openhop_ota.ld"}},
                "upload": {"maximum_size": RAK4631_LAYOUT.application_size},
            }))
            validate_rak4631_board(board)

            metadata = json.loads(board.read_text())
            metadata["upload"]["maximum_size"] += 1
            board.write_text(json.dumps(metadata))
            with self.assertRaisesRegex(ValueError, "maximum_size"):
                validate_rak4631_board(board)

    def test_repository_board_metadata_enforces_reserved_staging_slot(self) -> None:
        validate_rak4631_board(FIRMWARE / "boards/rak4631_wismesh_eth.json")

    def test_repository_board_uses_openhop_usb_product_identity(self) -> None:
        metadata = json.loads(
            (FIRMWARE / "boards/rak4631_wismesh_eth.json").read_text(encoding="utf-8")
        )
        self.assertEqual(metadata["build"]["usb_product"], "WisMesh-OpenHop-Ethernet")
        self.assertNotIn("pymc", metadata["build"]["usb_product"].lower())

    def test_repository_linker_caps_application_before_staging(self) -> None:
        linker = FIRMWARE / "boards/nrf52840_s140_v6_openhop_ota.ld"
        text = linker.read_text(encoding="utf-8")
        self.assertIn("ORIGIN = 0x26000", text)
        self.assertIn("LENGTH = 0x88000 - 0x26000", text)

    def test_platformio_resolves_repository_linker_path(self) -> None:
        text = (FIRMWARE / "platformio.ini").read_text(encoding="utf-8")
        self.assertIn(
            "board_build.ldscript = boards/nrf52840_s140_v6_openhop_ota.ld",
            text,
        )

    def test_asset_builder_validates_rak_binary_against_board_contract(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            artifact = Path(tmp) / "firmware.bin"
            artifact.write_bytes(b"x" * (RAK4631_LAYOUT.application_size + 1))
            with self.assertRaisesRegex(ValueError, "exceeds RAK4631 active slot"):
                validate_nrf52_build("rak4631_wismesh_eth", artifact)

            # Other nRF52 targets retain their existing linker/size contracts.
            validate_nrf52_build("heltec_t114", artifact)


if __name__ == "__main__":
    unittest.main()
