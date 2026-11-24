#!/usr/bin/env python3
"""Execute the production primary frontend's existing persistence and GPIO policy."""
from pathlib import Path
import os,subprocess,tempfile
FW=Path(__file__).resolve().parents[1]
def run():
 with tempfile.TemporaryDirectory(prefix='frontend-owners-',dir=os.environ.get('TMPDIR')) as d:
  for board,test in [('STATION_G3','radio_frontend_owners_test'),('STATION_G3','radio_frontend_persistence_test'),('HELTEC_V43','radio_frontend_persistence_test')]:
   exe=Path(d)/(board+test)
   args=['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-DARDUINO_ARCH_ESP32','-DBOARD_'+board,'-I'+str(FW/'tools/radio_runtime_stubs'),'-I'+str(FW/'include'),str(FW/'src/rf_frontend.cpp'),str(FW/'tests'/f'{test}.cpp'),'-o',str(exe)]
   print(' '.join(args),flush=True);subprocess.run(args,check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':run()
