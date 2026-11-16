#!/usr/bin/env python3
"""Compile and run the host-side HWCDC TX stall watch test."""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import tempfile


def main() -> int:
    firmware_dir = pathlib.Path(__file__).resolve().parents[1]
    compiler = shutil.which("g++")
    if compiler is None:
        raise SystemExit("g++ is required for the host-side USB TX stall watch test")

    with tempfile.TemporaryDirectory(prefix="openhop-usb-tx-stall-") as temp_dir:
        executable = pathlib.Path(temp_dir) / "usb_tx_stall_watch_test"
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
            f"-I{firmware_dir / 'include'}",
            str(firmware_dir / "tests" / "usb_tx_stall_watch_test.cpp"),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
    print("usb tx stall watch test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
