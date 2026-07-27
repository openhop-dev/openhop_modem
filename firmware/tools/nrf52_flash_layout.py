#!/usr/bin/env python3
"""RAK4631 nRF52840 flash-layout contract and host-side validation."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

PAGE_SIZE = 0x1000
RAK4631_LINKER_SCRIPT = "nrf52840_s140_v6_openhop_ota.ld"


@dataclass(frozen=True)
class FlashLayout:
    application_start: int
    application_size: int
    staging_start: int
    staging_size: int
    app_data_start: int
    littlefs_start: int
    littlefs_size: int
    bootloader_start: int
    mbr_params_start: int
    settings_start: int
    flash_end: int

    def validate(self) -> None:
        values = (
            self.application_start,
            self.application_size,
            self.staging_start,
            self.staging_size,
            self.app_data_start,
            self.littlefs_start,
            self.littlefs_size,
            self.bootloader_start,
            self.mbr_params_start,
            self.settings_start,
            self.flash_end,
        )
        if any(value % PAGE_SIZE for value in values):
            raise ValueError("all RAK4631 flash boundaries and sizes must be page-aligned")
        if self.application_start + self.application_size > self.staging_start:
            raise ValueError("application overlaps staging")
        if self.staging_start + self.staging_size > self.app_data_start:
            raise ValueError("staging overlaps reserved application data")
        if self.app_data_start > self.littlefs_start:
            raise ValueError("reserved application data starts after LittleFS")
        if self.littlefs_start + self.littlefs_size > self.bootloader_start:
            raise ValueError("LittleFS overlaps bootloader")
        if self.bootloader_start >= self.mbr_params_start:
            raise ValueError("bootloader overlaps MBR parameters page")
        if self.mbr_params_start + PAGE_SIZE > self.settings_start:
            raise ValueError("MBR parameters page overlaps bootloader settings")
        if self.settings_start + PAGE_SIZE > self.flash_end:
            raise ValueError("bootloader settings exceed physical flash")


# Matches Adafruit_nRF52_Bootloader's nRF52840 dual-bank calculation with
# S140 v6 at 0x26000 and DFU_APP_DATA_RESERVED=10*4096:
#   bank size = floor((0xF4000 - 0x26000 - 0xA000) / 0x2000) * 0x1000
RAK4631_LAYOUT = FlashLayout(
    application_start=0x26000,
    application_size=0x62000,
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


def validate_rak4631_size(size: int, source: str = "firmware.bin") -> None:
    RAK4631_LAYOUT.validate()
    if size <= 0:
        raise ValueError(f"RAK4631 artifact is empty: {source}")
    if size > RAK4631_LAYOUT.application_size:
        raise ValueError(
            f"RAK4631 firmware size {size} exceeds RAK4631 active slot "
            f"{RAK4631_LAYOUT.application_size}"
        )
    if size > RAK4631_LAYOUT.staging_size:
        raise ValueError(
            f"RAK4631 firmware size {size} exceeds RAK4631 staging slot "
            f"{RAK4631_LAYOUT.staging_size}"
        )


def validate_rak4631_artifact(path: Path) -> None:
    validate_rak4631_size(path.stat().st_size, str(path))


def validate_rak4631_board(path: Path) -> None:
    RAK4631_LAYOUT.validate()
    metadata = json.loads(path.read_text(encoding="utf-8"))
    ldscript = metadata.get("build", {}).get("arduino", {}).get("ldscript")
    if ldscript != RAK4631_LINKER_SCRIPT:
        raise ValueError(
            f"RAK4631 board must use {RAK4631_LINKER_SCRIPT}, found {ldscript!r}"
        )
    maximum_size = metadata.get("upload", {}).get("maximum_size")
    if maximum_size != RAK4631_LAYOUT.application_size:
        raise ValueError(
            "RAK4631 upload.maximum_size must equal active slot size "
            f"{RAK4631_LAYOUT.application_size}, found {maximum_size!r}"
        )
