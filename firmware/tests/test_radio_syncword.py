#!/usr/bin/env python3
"""Per-radio management must preserve the protocol's 16-bit syncword field."""
from pathlib import Path
import os
import subprocess
import tempfile

FW=Path(__file__).resolve().parents[1]

def main():
    with tempfile.TemporaryDirectory(prefix='radio-syncword-',dir=os.environ.get('TMPDIR')) as temp:
        out=Path(temp);source=out/'test.cpp';exe=out/'test'
        source.write_text('''#include "webui_shared.h"
#include <cassert>
#include <string>
#include <iostream>
int main(int argc,char**argv){
 assert(argc==2);
 WebUiShared::Model m;
 WebUiShared::RadioEndpointModel r;
 r.id=0;r.label="RF1";r.tcpPort=5055;
 r.radio.syncword=static_cast<uint16_t>(std::stoul(argv[1],nullptr,16));
 m.radios.push_back(r);
 const auto json=WebUiShared::renderRadiosJson(m);
 assert(json.find("\\"syncword\\":\\"0x3444\\"")!=std::string::npos);
 assert(json.find("\\"syncword_value\\":13380")!=std::string::npos);
 std::cout<<"PASS per-radio JSON preserves full 16-bit syncword\\n";
}
''')
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',
                        '-I'+str(FW/'include'),str(FW/'src/webui_shared.cpp'),str(source),'-o',str(exe)],check=True)
        subprocess.run([str(exe),'3444'],check=True)

if __name__=='__main__':main()
