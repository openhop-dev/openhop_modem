#!/usr/bin/env python3
"""Compile and run the host-side nRF52 loop watchdog test."""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import tempfile


def main() -> int:
    firmware_dir = pathlib.Path(__file__).resolve().parents[1]
    compiler = shutil.which("g++")
    if compiler is None:
        raise SystemExit("g++ is required for the host-side nRF52 loop watchdog test")

    with tempfile.TemporaryDirectory(prefix="openhop-nrf52-wdt-") as temp_dir:
        executable = pathlib.Path(temp_dir) / "nrf52_loop_watchdog_test"
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
            f"-I{firmware_dir / 'include'}",
            str(firmware_dir / "tests" / "nrf52_loop_watchdog_test.cpp"),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
    print("nrf52 loop watchdog test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
