#!/usr/bin/env python3
"""Execute real board/layout headers; no SDK, network or physical hardware."""
from pathlib import Path
import os
import subprocess
import tempfile

FW = Path(__file__).resolve().parents[1]

def main():
    with tempfile.TemporaryDirectory(prefix="openhop-radio-layout-", dir=os.environ.get("TMPDIR")) as out:
        exe=Path(out)/"radio-layout"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers",
                        "-DBOARD_HELTEC_V3", f"-I{FW/'include'}", str(FW/'tests/radio_layout_test.cpp'),
                        "-o", str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

if __name__ == "__main__":
    main()
