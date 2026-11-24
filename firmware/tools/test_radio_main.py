#!/usr/bin/env python3
"""Full main.cpp + real radio runtime/TCP/parser; unavailable SDK/device ports faked."""
from pathlib import Path
import subprocess,os,tempfile
FW=Path(__file__).resolve().parents[1]
def compile_main_test(directory, test_source):
 exe=Path(directory)/Path(test_source).stem
 sources=['src/main.cpp','src/radio_settings.cpp','src/radio_runtime.cpp','src/radio_hardware.cpp','src/rf_frontend.cpp','src/tcp_server.cpp','src/frame_parser.cpp','tools/radio_runtime_stubs/main_device_ports.cpp',test_source]
 args=['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-DARDUINO_ARCH_ESP32','-DBOARD_ETHERMESH_DUO','-I'+str(FW/'tools/radio_runtime_stubs'),'-I'+str(FW/'include'),*[str(FW/s) for s in sources],'-o',str(exe)]
 print(' '.join(args),flush=True)
 subprocess.run(args,check=True)
 return exe

def run():
 with tempfile.TemporaryDirectory(prefix='radio-main-',dir=os.environ.get('TMPDIR')) as d:
  exe=compile_main_test(d,'tests/radio_main_test.cpp')
  subprocess.run([str(exe)],check=True)
if __name__=='__main__':run()
