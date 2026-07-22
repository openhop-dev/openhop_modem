#!/usr/bin/env python3
"""Validate tracked firmware assets and package them for a GitHub Release."""
from __future__ import annotations

import argparse
import hashlib
import re
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "firmware"
SAFE_TAG = re.compile(r"^[A-Za-z0-9._-]+$")
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def discover_asset_dirs() -> list[Path]:
    asset_dirs = sorted(path.parent for path in FIRMWARE.glob("*/SHA256SUMS.txt"))
    if not asset_dirs:
        raise SystemExit("No firmware/<env>/SHA256SUMS.txt files found")
    return asset_dirs


def checksummed_files(asset_dir: Path) -> list[Path]:
    sums_path = asset_dir / "SHA256SUMS.txt"
    files: list[Path] = []
    for line_number, raw_line in enumerate(sums_path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line:
            continue
        parts = line.split(maxsplit=1)
        if len(parts) != 2 or not re.fullmatch(r"[0-9a-fA-F]{64}", parts[0]):
            raise SystemExit(f"Malformed checksum at {sums_path}:{line_number}")
        expected, filename = parts[0].lower(), parts[1].lstrip("*")
        if Path(filename).name != filename:
            raise SystemExit(f"Unsafe checksum path at {sums_path}:{line_number}: {filename}")
        source = asset_dir / filename
        if not source.is_file():
            raise SystemExit(f"Missing checksummed asset: {source}")
        actual = sha256_file(source)
        if actual != expected:
            raise SystemExit(
                f"Checksum mismatch for {source}: expected {expected}, got {actual}"
            )
        files.append(source)
    if not files:
        raise SystemExit(f"No assets listed in {sums_path}")
    return files


def write_zip_entry(archive: zipfile.ZipFile, name: str, data: bytes) -> None:
    info = zipfile.ZipInfo(name, ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def package(tag: str, output_dir: Path) -> tuple[list[Path], Path, list[str]]:
    if not SAFE_TAG.fullmatch(tag):
        raise SystemExit(f"Tag contains unsupported filename characters: {tag!r}")

    asset_dirs = discover_asset_dirs()
    envs = [asset_dir.name for asset_dir in asset_dirs]
    output_dir.mkdir(parents=True, exist_ok=True)
    for stale in output_dir.glob(f"openhop-modem-*-{tag}.zip*"):
        stale.unlink()

    zip_paths: list[Path] = []
    for asset_dir in asset_dirs:
        files = checksummed_files(asset_dir)
        archive_name = f"openhop-modem-{asset_dir.name}-{tag}.zip"
        zip_path = output_dir / archive_name
        temporary = zip_path.with_suffix(".zip.tmp")
        readme = (
            f"openHop Modem firmware {tag}\n"
            f"Environment: {asset_dir.name}\n"
            "\n"
            "Verify the included files with SHA256SUMS.txt.\n"
            "For ESP32 first installs, flash firmware.factory.bin at offset 0x0.\n"
            "For compatible app-only USB/OTA updates, use firmware.bin at 0x10000.\n"
            "For nRF52 targets, use firmware.uf2 or the Adafruit DFU firmware.zip.\n"
        )

        temporary.unlink(missing_ok=True)
        try:
            with zipfile.ZipFile(temporary, "w") as archive:
                write_zip_entry(archive, "README.txt", readme.encode())
                for source in [*files, asset_dir / "SHA256SUMS.txt"]:
                    write_zip_entry(archive, source.name, source.read_bytes())
            temporary.replace(zip_path)
        finally:
            temporary.unlink(missing_ok=True)
        zip_paths.append(zip_path)

    checksum_path = output_dir / f"openhop-modem-firmware-{tag}-SHA256SUMS.txt"
    checksum_path.write_text(
        "".join(f"{sha256_file(path)}  {path.name}\n" for path in zip_paths),
        encoding="utf-8",
    )
    return zip_paths, checksum_path, envs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag", required=True, help="Release tag used in the bundle filename")
    parser.add_argument(
        "--output-dir", type=Path, default=ROOT / "release-assets",
        help="Directory for per-environment ZIPs and their checksum manifest",
    )
    args = parser.parse_args()

    zip_paths, checksum_path, envs = package(args.tag, args.output_dir.resolve())
    print(f"Validated {len(envs)} firmware environments: {', '.join(envs)}")
    for zip_path in zip_paths:
        print(f"Created {zip_path}")
    print(f"Created {checksum_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
