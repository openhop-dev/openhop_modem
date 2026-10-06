#!/usr/bin/env python3
"""Check PlatformIO's real post-unflags USB definitions (run builds serially)."""
from pathlib import Path
import re
import subprocess

FW = Path(__file__).resolve().parents[1]
for env, expected in (('lilygo_tbeam_1w', ['0']), ('heltec_v3', ['1', '1'])):
    result = subprocess.run(['pio', 'run', '-d', str(FW), '-e', env, '-t', 'envdump'],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        print(result.stdout)
        raise SystemExit(result.returncode)
    definitions = result.stdout.split("'CPPDEFINES':", 1)[1].split("'CPPDEFPREFIX':", 1)[0]
    modes = re.findall(r"\('ARDUINO_USB_MODE',\s*(\d+)\)", definitions)
    # The existing V3 repeats the same value in board/project flags; preserve it.
    assert modes == expected, (env, modes, definitions)
    print(f'PASS {env}: ARDUINO_USB_MODE definitions={modes}; no conflicting values', flush=True)
