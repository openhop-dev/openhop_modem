#!/usr/bin/env python3
"""Execute checked IRQ decisions and timeouts in the full production owner."""
import os
import subprocess
import tempfile
from pathlib import Path
FW=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='radio-irq-integrity-',dir=os.environ.get('TMPDIR')) as root:
 exe=Path(root)/'test'
 command=['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',
  '-DBOARD_STATION_G3','-DARDUINO_ARCH_ESP32','-I'+str(FW/'tools/radio_runtime_stubs'),'-I'+str(FW/'include'),
  str(FW/'src/rf_frontend.cpp'),str(FW/'src/radio_hardware.cpp'),str(FW/'src/radio_runtime.cpp'),
  str(FW/'tests/radio_irq_integrity_test.cpp'),'-o',str(exe)]
 subprocess.run(command,check=True)
 subprocess.run([str(exe)],check=True)
