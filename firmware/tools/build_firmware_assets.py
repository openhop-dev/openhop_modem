#!/usr/bin/env python3
"""Build PlatformIO firmware envs and stage flasher-ready assets.

This is intentionally repo-local and small: GitHub Actions calls it, but it
also works from a developer shell.  It discovers [env:<name>] blocks from
firmware/platformio.ini, optionally narrows the build set from git changes,
builds each selected env, then copies artifacts into firmware/<env>/.

ESP32-family envs get:
  bootloader.bin, partitions.bin, firmware.bin, manifest.json, SHA256SUMS.txt

nRF52 envs get:
  firmware.hex, firmware.zip, firmware.uf2, SHA256SUMS.txt
"""
from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "firmware"
PLATFORMIO_INI = FIRMWARE / "platformio.ini"
SELECTED_ENVS_FILE = FIRMWARE / ".selected_firmware_envs"

# Env-specific metadata used for flasher manifests and names.  Envs omitted
# here still build/copy artifacts; they just get conservative defaults.
ENV_METADATA: dict[str, dict[str, str | bool]] = {
    "heltec_v3": {
        "name": "Heltec WiFi LoRa 32 V3 openHop Modem",
        "chip_family": "ESP32-S3",
        "web_manifest": True,
    },
    "heltec_v4": {
        "name": "Heltec WiFi LoRa 32 V4 openHop Modem",
        "chip_family": "ESP32-S3",
        "web_manifest": True,
    },
    "heltec_v42": {
        "name": "Heltec WiFi LoRa 32 V4.2 openHop Modem",
        "chip_family": "ESP32-S3",
        "web_manifest": True,
    },
    "heltec_v43": {
        "name": "Heltec WiFi LoRa 32 V4.3 openHop Modem",
        "chip_family": "ESP32-S3",
        "web_manifest": True,
    },
    "heltec_tracker_v2": {
        "name": "Heltec Wireless Tracker V2 openHop Modem",
        "chip_family": "ESP32-S3",
        "web_manifest": True,
    },
    "ikoka_stick": {
        "name": "Ikoka Stick openHop Modem",
        "chip_family": "ESP32-S3",
        "web_manifest": True,
    },
    "xiao_wio_sx1262": {
        "name": "Seeed XIAO Wio-SX1262 openHop Modem",
        "chip_family": "ESP32-S3",
        "web_manifest": True,
    },
    "photon_1w_xiao_esp32c6": {
        "name": "MeshSmith Photon-1W XIAO ESP32-C6 openHop Modem",
        "chip_family": "ESP32-C6",
        "web_manifest": True,
    },
    "rak3112_wismesh": {
        "name": "RAK3112 WisMesh openHop Modem",
        "chip_family": "ESP32-S3",
        "web_manifest": True,
    },
    "station_g2": {
        "name": "B&Q Consulting Station G2 openHop Modem",
        "chip_family": "ESP32-S3",
        "web_manifest": True,
    },
    "esp32_p4_nano": {
        "name": "WaveShare ESP32-P4-Nano openHop Modem",
        "chip_family": "ESP32-P4",
        # ESP Web Tools support for P4 is still less common; artifacts are
        # staged, but a browser-flasher manifest is deliberately skipped.
        "web_manifest": False,
    },
    "ethermesh_1w": {
        "name": "MeshSmith EtherMesh-1W openHop Modem",
        "chip_family": "ESP32-P4",
        "web_manifest": False,
    },
    "lilygo_t3s3": {
        "name": "LilyGO T-LoRa T3-S3 openHop Modem",
        "chip_family": "ESP32-S3",
        "web_manifest": True,
    },
    "heltec_t114": {
        "name": "Heltec T114 openHop Modem",
        "chip_family": "NRF52",
        "web_manifest": False,
    },
    "xiao_nrf52_wio": {
        "name": "Seeed XIAO nRF52840 Wio-SX1262 openHop Modem",
        "chip_family": "NRF52",
        "web_manifest": False,
    },
    "rak4631_wismesh_eth": {
        "name": "RAK4631 WisMesh Ethernet openHop Modem",
        "chip_family": "NRF52",
        "web_manifest": False,
    },
}

# Board headers generally map 1:1 to env names in this repo.
BOARD_HEADER_TO_ENV = {
    "firmware/include/boards/heltec_v3.h": "heltec_v3",
    "firmware/include/boards/heltec_v4.h": "heltec_v4",
    "firmware/include/boards/heltec_v42.h": "heltec_v42",
    "firmware/include/boards/heltec_v43.h": "heltec_v43",
    "firmware/include/boards/heltec_tracker_v2.h": "heltec_tracker_v2",
    "firmware/include/boards/ikoka_stick.h": "ikoka_stick",
    "firmware/include/boards/xiao_wio_sx1262.h": "xiao_wio_sx1262",
    "firmware/include/boards/photon_1w_xiao_esp32c6.h": "photon_1w_xiao_esp32c6",
    "firmware/include/boards/rak3112_wismesh.h": "rak3112_wismesh",
    "firmware/include/boards/station_g2.h": "station_g2",
    "firmware/include/boards/esp32_p4_nano.h": "esp32_p4_nano",
    "firmware/include/boards/ethermesh_1w.h": "ethermesh_1w",
    "firmware/include/boards/lilygo_t3s3.h": "lilygo_t3s3",
    "firmware/include/boards/heltec_t114.h": "heltec_t114",
    "firmware/include/boards/xiao_nrf52_wio.h": "xiao_nrf52_wio",
    "firmware/include/boards/rak4631_wismesh_eth.h": "rak4631_wismesh_eth",
}

# Changes here are shared enough that building all envs is safer.
SHARED_PREFIXES = (
    ".github/workflows/",
    "firmware/src/",
    "firmware/include/",
    "firmware/variants/",
    "firmware/boards/",
)
SHARED_FILES = {
    "firmware/platformio.ini",
    "firmware/build_release.sh",
    "firmware/tools/build_firmware_assets.py",
}


def run(cmd: list[str], cwd: Path = ROOT) -> str:
    print("+ " + " ".join(cmd), flush=True)
    completed = subprocess.run(cmd, cwd=cwd, check=False, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if completed.stdout:
        print(completed.stdout, end="")
    completed.check_returncode()
    return completed.stdout


def discover_envs() -> list[str]:
    envs: list[str] = []
    for line in PLATFORMIO_INI.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith("[env:") and stripped.endswith("]"):
            envs.append(stripped[len("[env:"):-1])
    if not envs:
        raise SystemExit(f"No [env:<name>] blocks found in {PLATFORMIO_INI}")
    return envs


def git_changed_files(base: str | None, head: str | None) -> list[str]:
    if not base or not head:
        return []
    # GitHub sets github.event.before to all zeroes for the first push of a
    # new branch.  That is not a real commit, so there is no diff range to
    # inspect; let auto mode fall back to building all envs.
    if set(base) == {"0"}:
        print("Changed-file base is the all-zero new-branch sentinel; building all envs.")
        return []
    try:
        out = run(["git", "diff", "--name-only", f"{base}...{head}"], ROOT)
    except subprocess.CalledProcessError:
        out = run(["git", "diff", "--name-only", base, head], ROOT)
    return [line.strip() for line in out.splitlines() if line.strip()]


def select_envs(requested: str, all_envs: list[str], base: str | None, head: str | None) -> list[str]:
    requested = requested.strip()
    if requested == "all":
        return all_envs
    if requested != "auto":
        requested_envs = [part.strip() for part in requested.split(",") if part.strip()]
        unknown = sorted(set(requested_envs) - set(all_envs))
        if unknown:
            raise SystemExit(f"Unknown env(s): {', '.join(unknown)}. Known: {', '.join(all_envs)}")
        return requested_envs

    changed = git_changed_files(base, head)
    if not changed:
        print("No changed-file range supplied; auto mode will build all envs.")
        return all_envs

    print("Changed files:")
    for path in changed:
        print(f"  {path}")

    selected: set[str] = set()
    for path in changed:
        if path in SHARED_FILES:
            print(f"Shared file changed ({path}); building all envs.")
            return all_envs
        if path in BOARD_HEADER_TO_ENV:
            selected.add(BOARD_HEADER_TO_ENV[path])
            continue
        if path.startswith("firmware/include/boards/"):
            # Unknown board header: safest option is all envs.
            print(f"Unknown board header changed ({path}); building all envs.")
            return all_envs
        if path.startswith("firmware/") and not any(
            path.startswith(f"firmware/{env}/") for env in all_envs
        ):
            if any(path.startswith(prefix) for prefix in SHARED_PREFIXES):
                print(f"Shared firmware path changed ({path}); building all envs.")
                return all_envs

    if selected:
        return [env for env in all_envs if env in selected]

    print("Only docs/prebuilt artifacts changed; no firmware envs selected.")
    return []


def pio_executable(cli_value: str | None) -> str:
    if cli_value:
        return cli_value
    return os.environ.get("PIO") or shutil.which("pio") or str(Path.home() / ".platformio/penv/bin/pio")


def clean_dest(dest: Path) -> None:
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True, exist_ok=True)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def write_sha256s(dest: Path, files: list[Path]) -> None:
    lines = [f"{sha256_file(path)}  {path.name}" for path in files if path.exists()]
    (dest / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_nrf52_uf2(hex_file: Path, dest: Path, env: str) -> Path:
    uf2conv = (Path.home() / ".platformio" / "packages" /
               "framework-arduinoadafruitnrf52" / "tools" / "uf2conv" /
               "uf2conv.py")
    if not uf2conv.exists():
        raise SystemExit(f"Expected UF2 converter missing for {env}: {uf2conv}")
    uf2_file = dest / "firmware.uf2"
    run([sys.executable, str(uf2conv), str(hex_file), "-c", "-f", "0xADA52840",
         "-o", str(uf2_file)], FIRMWARE)
    return uf2_file


def firmware_version(env: str) -> str:
    main_cpp = FIRMWARE / "src/main.cpp"
    base = "unknown"
    if main_cpp.exists():
        for line in main_cpp.read_text(encoding="utf-8", errors="ignore").splitlines():
            marker = "#define FW_VERSION_BASE"
            if line.strip().startswith(marker):
                parts = line.split('"')
                if len(parts) >= 2:
                    base = parts[1]
                break
    # Board fw_suffix normally matches env enough for flasher display.
    return f"{base}-{env}"


def write_esp_manifest(env: str, dest: Path) -> Path | None:
    meta = ENV_METADATA.get(env, {})
    if meta.get("web_manifest") is not True:
        return None
    chip_family = str(meta.get("chip_family", "ESP32"))
    manifest = {
        "name": str(meta.get("name", f"{env} openHop Modem")),
        "version": firmware_version(env),
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": chip_family,
                "parts": [
                    {"path": "bootloader.bin", "offset": 0},
                    {"path": "partitions.bin", "offset": 0x8000},
                    {"path": "firmware.bin", "offset": 0x10000},
                ],
            }
        ],
    }
    out = dest / "manifest.json"
    out.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return out


def collect_env(env: str) -> None:
    out_dir = FIRMWARE / ".pio/build" / env
    dest = FIRMWARE / env
    clean_dest(dest)

    # nRF52 Adafruit builds produce firmware.hex + firmware.zip.
    hex_file = out_dir / "firmware.hex"
    zip_file = out_dir / "firmware.zip"
    bin_file = out_dir / "firmware.bin"
    copied: list[Path] = []

    if hex_file.exists() and not bin_file.exists():
        for source in (hex_file, zip_file):
            if not source.exists():
                raise SystemExit(f"Expected artifact missing for {env}: {source}")
            target = dest / source.name
            shutil.copy2(source, target)
            copied.append(target)
        copied.append(write_nrf52_uf2(hex_file, dest, env))
        write_sha256s(dest, copied)
        print(f"Staged nRF52 artifacts in {dest.relative_to(ROOT)}")
        return

    for name in ("bootloader.bin", "partitions.bin", "firmware.bin"):
        source = out_dir / name
        if not source.exists():
            raise SystemExit(f"Expected artifact missing for {env}: {source}")
        target = dest / name
        shutil.copy2(source, target)
        copied.append(target)

    manifest = write_esp_manifest(env, dest)
    if manifest:
        copied.append(manifest)
    write_sha256s(dest, copied)
    print(f"Staged ESP artifacts in {dest.relative_to(ROOT)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--variant", default="auto",
                        help="auto, all, or comma-separated PlatformIO env names")
    parser.add_argument("--changed-from", default=os.environ.get("GITHUB_EVENT_BEFORE"))
    parser.add_argument("--changed-to", default=os.environ.get("GITHUB_SHA"))
    parser.add_argument("--pio", default=None, help="Path to PlatformIO executable")
    parser.add_argument("--clean", action="store_true", help="Run pio clean before building")
    parser.add_argument("--plan", action="store_true", help="Only print selected envs; do not build")
    args = parser.parse_args()

    all_envs = discover_envs()
    selected = select_envs(args.variant, all_envs, args.changed_from, args.changed_to)
    SELECTED_ENVS_FILE.write_text("\n".join(selected) + ("\n" if selected else ""), encoding="utf-8")

    print("Selected envs: " + (", ".join(selected) if selected else "<none>"))
    if args.plan or not selected:
        return 0

    pio = pio_executable(args.pio)
    for env in selected:
        if args.clean:
            run([pio, "run", "-e", env, "--target", "clean"], FIRMWARE)
        run([pio, "run", "-e", env], FIRMWARE)
        collect_env(env)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
