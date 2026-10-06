#!/usr/bin/env python3
"""Compile and run the Grove BME280 host test for Station G2 and G3.

Run from anywhere: python3 firmware/tools/test_environment_sensor.py
Requires PlatformIO (to fetch the pinned Bosch library), gcc, and g++.
Does not build firmware or access hardware.
"""

from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

FIRMWARE = Path(__file__).resolve().parents[1]
LIBRARY = FIRMWARE / ".pio/libdeps/station_g2/BME280_SensorAPI"
BOARDS = ("BOARD_STATION_G2", "BOARD_STATION_G3")


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=FIRMWARE, check=True)


def main() -> int:
    for compiler in ("gcc", "g++"):
        if shutil.which(compiler) is None:
            print(f"Missing host compiler: {compiler}", file=sys.stderr)
            return 1

    if not (LIBRARY / "bme280.c").is_file():
        if shutil.which("pio") is None:
            print("Missing Bosch BME280 library and PlatformIO (pio) to install it", file=sys.stderr)
            return 1
        # Read the exact pinned library spec from the build environment so the
        # host test cannot silently use a different Bosch API revision.
        platformio = (FIRMWARE / "platformio.ini").read_text()
        g2 = platformio.split("[env:station_g2]", 1)[1].split("[env:", 1)[0]
        match = re.search(r"^\s*(https://github\.com/boschsensortec/BME280_SensorAPI\.git#[^\s;]+)", g2, re.M)
        if match is None:
            print("Station G2 has no pinned Bosch BME280 dependency", file=sys.stderr)
            return 1
        run(["pio", "pkg", "install", "-d", str(FIRMWARE), "-e", "station_g2",
             "--no-save", "-l", match.group(1)])
        if not (LIBRARY / "bme280.c").is_file():
            print(f"PlatformIO did not install {LIBRARY / 'bme280.c'}", file=sys.stderr)
            return 1

    with tempfile.TemporaryDirectory(prefix="openhop-environment-test-") as build_dir:
        build = Path(build_dir)
        driver = build / "bme280.o"
        run(["gcc", "-std=c99", "-c", str(LIBRARY / "bme280.c"), "-o", str(driver)])
        failures = []
        for board in BOARDS:
            executable = build / board.lower()
            try:
                run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                     f"-D{board}", "-DARDUINO_ARCH_ESP32",
                     "-I" + str(FIRMWARE / "tests/stubs/environment"),
                     "-I" + str(FIRMWARE / "include"), "-I" + str(LIBRARY),
                     str(FIRMWARE / "tests/environment_sensor_test.cpp"),
                     str(FIRMWARE / "src/environment_sensor.cpp"), str(driver),
                     "-o", str(executable)])
                run([str(executable)])
                print(f"{board}: PASS", flush=True)
            except subprocess.CalledProcessError:
                failures.append(board)
                print(f"{board}: FAIL", file=sys.stderr, flush=True)
        if failures:
            print("Failed: " + ", ".join(failures), file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as exc:
        print(f"Command failed (exit {exc.returncode})", file=sys.stderr)
        sys.exit(1)
