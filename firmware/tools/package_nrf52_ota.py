#!/usr/bin/env python3
"""Create and verify deterministic RAK4631 Ethernet OTA packages.

The payload SHA-256 detects corruption only. This schema reserves signature
metadata as type/length zero and provides no authenticity guarantee.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import sys
import tempfile
import zipfile
from pathlib import Path

MAGIC = b"OHOTA\r\n\x1a"
MAGIC_OFFSET = 0
SCHEMA_OFFSET = 8
HEADER_LENGTH_OFFSET = 10
TARGET_LENGTH_OFFSET = 12
VERSION_LENGTH_OFFSET = 14
SIGNATURE_TYPE_OFFSET = 16
SIGNATURE_LENGTH_OFFSET = 18
APPLICATION_ORIGIN_OFFSET = 20
SOFTDEVICE_FWID_OFFSET = 24
PAYLOAD_LENGTH_OFFSET = 28
PAYLOAD_SHA256_OFFSET = 32
FIXED_HEADER_LENGTH = 64

SCHEMA = 1
SIGNATURE_NONE = 0
TARGET_ID = "rak4631_wismesh_eth"
TARGET_ID_BYTES = TARGET_ID.encode("ascii")
TARGET_MAX_LENGTH = 32
VERSION_MAX_LENGTH = 64
HEADER_MAX_LENGTH = FIXED_HEADER_LENGTH + TARGET_MAX_LENGTH + VERSION_MAX_LENGTH
PAYLOAD_MAX_LENGTH = 0x62000
APPLICATION_ORIGIN = 0x26000
SOFTDEVICE_FWID = 0x00B6
SHA256_LENGTH = 32
_FIXED_FORMAT = "<8sHHHHHHIII32s"


class PackageError(ValueError):
    """The OTA package violates the board-specific format contract."""


def _encode_version(firmware_version: str) -> bytes:
    if "\x00" in firmware_version:
        raise PackageError("firmware version must not contain NUL")
    try:
        encoded = firmware_version.encode("utf-8")
    except UnicodeEncodeError as exc:
        raise PackageError("firmware version is not valid UTF-8") from exc
    if not encoded or len(encoded) > VERSION_MAX_LENGTH:
        raise PackageError(
            f"firmware version length must be 1..{VERSION_MAX_LENGTH} bytes"
        )
    return encoded


def firmware_version_from_source(
    main_cpp: Path, board_header: Path | None = None
) -> str:
    """Return the runtime version assembled by main.cpp and BoardConfig."""
    main_text = Path(main_cpp).read_text(encoding="utf-8", errors="strict")
    match = re.search(
        r'^\s*#define\s+FW_VERSION_BASE\s+"([^"]+)"\s*$',
        main_text,
        re.MULTILINE,
    )
    if not match:
        raise PackageError(f"FW_VERSION_BASE not found in {main_cpp}")
    suffix = TARGET_ID
    if board_header is not None:
        board_text = Path(board_header).read_text(encoding="utf-8", errors="strict")
        board_match = re.search(
            r'BOARD\s*=\s*\{\s*"[^"]+"\s*,\s*"([^"]+)"',
            board_text,
            re.DOTALL,
        )
        if not board_match:
            raise PackageError(f"BoardConfig firmware suffix not found in {board_header}")
        suffix = board_match.group(1)
    if suffix != TARGET_ID:
        raise PackageError(
            f"board firmware suffix {suffix!r} does not match OTA target {TARGET_ID!r}"
        )
    return f"{match.group(1)}-{suffix}"


def _package_bytes(payload: bytes, firmware_version: str) -> bytes:
    payload_length = len(payload)
    if payload_length == 0 or payload_length > PAYLOAD_MAX_LENGTH:
        raise PackageError(
            f"payload length must be 1..{PAYLOAD_MAX_LENGTH}, got {payload_length}"
        )
    version = _encode_version(firmware_version)
    target = TARGET_ID_BYTES
    header_length = FIXED_HEADER_LENGTH + len(target) + len(version)
    if header_length > HEADER_MAX_LENGTH:
        raise PackageError(f"header length {header_length} exceeds maximum")
    digest = hashlib.sha256(payload).digest()
    fixed = struct.pack(
        _FIXED_FORMAT,
        MAGIC,
        SCHEMA,
        header_length,
        len(target),
        len(version),
        SIGNATURE_NONE,
        0,
        APPLICATION_ORIGIN,
        SOFTDEVICE_FWID,
        payload_length,
        digest,
    )
    if len(fixed) != FIXED_HEADER_LENGTH:
        raise AssertionError("internal fixed header size mismatch")
    return fixed + target + version + payload


def create_package_bytes(payload: bytes, output: Path, firmware_version: str) -> dict[str, object]:
    """Write a package atomically from exact raw application bytes."""
    package = _package_bytes(payload, firmware_version)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", suffix=".tmp", dir=output.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(package)
            handle.flush()
            os.fsync(handle.fileno())
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    return inspect_package(output)


def create_package(payload_path: Path, output: Path, firmware_version: str) -> dict[str, object]:
    payload_path = Path(payload_path)
    try:
        payload_size = payload_path.stat().st_size
    except OSError as exc:
        raise PackageError(f"unable to stat payload {payload_path}: {exc}") from exc
    if payload_size == 0 or payload_size > PAYLOAD_MAX_LENGTH:
        raise PackageError(
            f"payload length must be 1..{PAYLOAD_MAX_LENGTH}, got {payload_size}"
        )
    try:
        payload = payload_path.read_bytes()
    except OSError as exc:
        raise PackageError(f"unable to read payload {payload_path}: {exc}") from exc
    return create_package_bytes(payload, Path(output), firmware_version)


def _read_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def inspect_package(path: Path) -> dict[str, object]:
    """Strictly inspect and verify a complete package."""
    path = Path(path)
    try:
        package_size = path.stat().st_size
    except OSError as exc:
        raise PackageError(f"unable to stat package {path}: {exc}") from exc
    if package_size < FIXED_HEADER_LENGTH:
        raise PackageError("truncated header")
    if package_size > HEADER_MAX_LENGTH + PAYLOAD_MAX_LENGTH:
        raise PackageError(f"package exceeds maximum length: {package_size}")
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise PackageError(f"unable to read package {path}: {exc}") from exc
    if len(data) < FIXED_HEADER_LENGTH:
        raise PackageError("truncated header")
    if data[MAGIC_OFFSET:SCHEMA_OFFSET] != MAGIC:
        raise PackageError("bad magic")

    schema = _read_u16(data, SCHEMA_OFFSET)
    if schema != SCHEMA:
        raise PackageError(f"unsupported schema {schema}")
    header_length = _read_u16(data, HEADER_LENGTH_OFFSET)
    target_length = _read_u16(data, TARGET_LENGTH_OFFSET)
    version_length = _read_u16(data, VERSION_LENGTH_OFFSET)
    signature_type = _read_u16(data, SIGNATURE_TYPE_OFFSET)
    signature_length = _read_u16(data, SIGNATURE_LENGTH_OFFSET)
    application_origin = _read_u32(data, APPLICATION_ORIGIN_OFFSET)
    softdevice_fwid = _read_u32(data, SOFTDEVICE_FWID_OFFSET)
    payload_length = _read_u32(data, PAYLOAD_LENGTH_OFFSET)
    expected_digest = data[PAYLOAD_SHA256_OFFSET:PAYLOAD_SHA256_OFFSET + SHA256_LENGTH]

    if target_length < 1 or target_length > TARGET_MAX_LENGTH:
        raise PackageError(f"invalid target length {target_length}")
    if version_length < 1 or version_length > VERSION_MAX_LENGTH:
        raise PackageError(f"invalid version length {version_length}")
    if signature_type != SIGNATURE_NONE:
        raise PackageError(f"unsupported signature type {signature_type}")
    if signature_length != 0:
        raise PackageError(f"signature length must be zero, got {signature_length}")
    expected_header_length = (
        FIXED_HEADER_LENGTH + target_length + version_length + signature_length
    )
    if (
        header_length != expected_header_length
        or header_length > HEADER_MAX_LENGTH
        or header_length < FIXED_HEADER_LENGTH
    ):
        raise PackageError(
            f"invalid header length {header_length}; expected {expected_header_length}"
        )
    if len(data) < header_length:
        raise PackageError("truncated variable header")

    target_start = FIXED_HEADER_LENGTH
    version_start = target_start + target_length
    target_bytes = data[target_start:version_start]
    if target_bytes != TARGET_ID_BYTES:
        raise PackageError(f"wrong target {target_bytes!r}")
    version_bytes = data[version_start:version_start + version_length]
    try:
        firmware_version = version_bytes.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise PackageError("invalid firmware version UTF-8") from exc
    if "\x00" in firmware_version:
        raise PackageError("invalid firmware version NUL")

    if application_origin != APPLICATION_ORIGIN:
        raise PackageError(
            f"wrong application origin 0x{application_origin:x}; expected 0x{APPLICATION_ORIGIN:x}"
        )
    if softdevice_fwid != SOFTDEVICE_FWID:
        raise PackageError(
            f"wrong SoftDevice FWID 0x{softdevice_fwid:04x}; expected 0x{SOFTDEVICE_FWID:04x}"
        )
    if payload_length == 0 or payload_length > PAYLOAD_MAX_LENGTH:
        raise PackageError(
            f"payload length must be 1..{PAYLOAD_MAX_LENGTH}, got {payload_length}"
        )

    expected_total = header_length + payload_length
    if len(data) < expected_total:
        raise PackageError(
            f"truncated payload: declared {payload_length}, have {len(data) - header_length}"
        )
    if len(data) > expected_total:
        raise PackageError(f"trailing bytes: expected {expected_total}, have {len(data)}")
    payload = data[header_length:expected_total]
    actual_digest = hashlib.sha256(payload).digest()
    if actual_digest != expected_digest:
        raise PackageError("payload hash mismatch")

    return {
        "magic": MAGIC.hex(),
        "schema": schema,
        "header_length": header_length,
        "target": TARGET_ID,
        "firmware_version": firmware_version,
        "application_origin": application_origin,
        "softdevice_fwid": softdevice_fwid,
        "payload_length": payload_length,
        "payload_sha256": actual_digest.hex(),
        "signature_type": "none",
        "signature_length": 0,
        "authenticated": False,
        "package_length": len(data),
        "package_sha256": hashlib.sha256(data).hexdigest(),
    }


def read_dfu_payload(zip_path: Path) -> bytes:
    """Read the single application payload from an Adafruit Nordic DFU ZIP."""
    try:
        with zipfile.ZipFile(zip_path) as archive:
            matches = [info for info in archive.infolist() if info.filename == "firmware.bin"]
            if len(matches) != 1:
                raise PackageError(
                    f"{zip_path} must contain exactly one firmware.bin, found {len(matches)}"
                )
            size = matches[0].file_size
            if size == 0 or size > PAYLOAD_MAX_LENGTH:
                raise PackageError(
                    f"DFU payload length must be 1..{PAYLOAD_MAX_LENGTH}, got {size}"
                )
            return archive.read(matches[0])
    except (OSError, zipfile.BadZipFile) as exc:
        raise PackageError(f"unable to read Nordic DFU ZIP {zip_path}: {exc}") from exc


def validate_package_matches_dfu(
    package_path: Path, zip_path: Path, expected_version: str | None = None
) -> dict[str, object]:
    metadata = inspect_package(package_path)
    payload = read_dfu_payload(zip_path)
    digest = hashlib.sha256(payload).hexdigest()
    if metadata["payload_length"] != len(payload) or metadata["payload_sha256"] != digest:
        raise PackageError("firmware.ota payload does not match firmware.zip:firmware.bin")
    if expected_version is not None and metadata["firmware_version"] != expected_version:
        raise PackageError(
            f"firmware.ota version {metadata['firmware_version']!r} does not match "
            f"runtime version {expected_version!r}"
        )
    return metadata


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="mode", required=True)
    create = subparsers.add_parser("create", help="create deterministic firmware.ota")
    create.add_argument("payload", type=Path, help="raw firmware.bin")
    create.add_argument("output", type=Path, help="output firmware.ota")
    create.add_argument("--firmware-version", required=True)
    inspect = subparsers.add_parser("inspect", aliases=["verify"], help="inspect and verify package")
    inspect.add_argument("package", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.mode == "create":
            metadata = create_package(args.payload, args.output, args.firmware_version)
        else:
            metadata = inspect_package(args.package)
    except PackageError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(metadata, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
