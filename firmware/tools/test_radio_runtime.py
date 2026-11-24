#!/usr/bin/env python3
"""Compile and execute complete production RF runtime at the SDK boundary."""
from pathlib import Path
import os
import subprocess
import tempfile
FW = Path(__file__).resolve().parents[1]
def run():
    with tempfile.TemporaryDirectory(prefix='radio-runtime-', dir=os.environ.get('TMPDIR')) as d:
        exe=Path(d)/'runtime'
        command=['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-DBOARD_STATION_G3','-DARDUINO_ARCH_ESP32',
                 '-I'+str(FW/'tools/radio_runtime_stubs'),'-I'+str(FW/'include'),
                 str(FW/'src/rf_frontend.cpp'),str(FW/'src/radio_hardware.cpp'),str(FW/'src/radio_runtime.cpp'),str(FW/'tests/radio_runtime_test.cpp'),'-o',str(exe)]
        print(' '.join(command),flush=True)
        subprocess.run(command,check=True)
        subprocess.run([str(exe)],check=True)
if __name__=='__main__':run()
