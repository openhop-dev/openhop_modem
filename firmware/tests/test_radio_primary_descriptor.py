#!/usr/bin/env python3
"""Primary RF facade must use descriptor zero, not device-wide legacy pins."""
from pathlib import Path
import os
import subprocess
import tempfile

FW=Path(__file__).resolve().parents[1]

def main():
    with tempfile.TemporaryDirectory(prefix='radio-primary-descriptor-',dir=os.environ.get('TMPDIR')) as temp:
        out=Path(temp)
        # Preserve actual production structure/API; substitute only selected
        # immutable test-board data, not frontend behavior.
        board=(FW/'include/board_config.h').read_text()
        board=board[:board.index('// ─── Board selection')]
        board+='''
inline const BoardConfig RADIO_ZERO = [](){
 BoardConfig hw{};hw.name="RF hardware";
 hw.rf_frontend.pa_mode_pin=9;hw.rf_frontend.pa_user_selectable=true;
 hw.rf_frontend.pa_high_active_high=true;
 return hw;
}();
inline const RadioDescriptor RADIO_LIST[]={{"custom-primary",&RADIO_ZERO,5055,-1}};
inline const BoardConfig BOARD=[](){
 BoardConfig device=RADIO_ZERO;
 device.rf_frontend.pa_mode_pin=42;device.rf_frontend.pa_user_selectable=false;
 device.radios=RADIO_LIST;device.radio_count=1;return device;
}();
'''
        (out/'board_config.h').write_text(board)
        (out/'rf_frontend.h').write_bytes((FW/'include/rf_frontend.h').read_bytes())
        (out/'radio_layout.h').write_bytes((FW/'include/radio_layout.h').read_bytes())
        cpp=out/'test.cpp'
        cpp.write_text('''#include "rf_frontend.h"
#include <cassert>
#include <iostream>
int main(){
 assert(RFFrontEnd::primary().hasPaModeControl());
 RFFrontEnd::begin();
 assert(gpio.count(9));
 assert(!gpio.count(42));
 std::cout<<"PASS actual primary frontend uses explicit descriptor-zero policy\\n";
}
''')
        exe=out/'test'
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',
                        '-DARDUINO_ARCH_ESP32','-I'+str(out),'-I'+str(FW/'tools/radio_runtime_stubs'),
                        '-I'+str(FW/'include'),str(FW/'src/rf_frontend.cpp'),str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

if __name__=='__main__':main()
