#!/usr/bin/env python3
"""Exercise the production management renderer with independent radio state."""
from pathlib import Path
import json
import os
import subprocess
import tempfile
import re

FW=Path(__file__).resolve().parents[1]

def main():
    with tempfile.TemporaryDirectory(prefix="openhop-webui-radios-",dir=os.environ.get("TMPDIR")) as out:
        for test in ("webui_radio_endpoints_test","webui_shared_test"):
            exe=Path(out)/test
            subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",f"-I{FW/'include'}",
                            str(FW/"src/webui_shared.cpp"),str(FW/f"tests/{test}.cpp"),"-o",str(exe)],check=True)
            html=Path(out)/'root.html'
            result=subprocess.check_output([str(exe),str(html)],text=True)
            if test=="webui_radio_endpoints_test":
                scripts=re.findall(r'<script>(.*?)</script>',html.read_text(),re.S)
                assert scripts, 'save/reboot client script missing'
                for index, script in enumerate(scripts):
                    js=Path(out)/f'client-{index}.js';js.write_text(script)
                    subprocess.run(['node','--check',str(js)],check=True)
                data=json.loads(result)
                assert len(data)==2
                assert [r["tcp_port"] for r in data]==[5055,5056]
                assert [r["connected_client_ip"] for r in data]==["192.0.2.10","192.0.2.20"]
                assert [r["counters"]["rx_packets"] for r in data]==[11,22]
                assert [r["radio"]["frequency_hz"] for r in data]==[910525000,915000000]
                assert data[0]["ready"] is True and data[1]["ready"] is False
                assert data[1]["label"]=='RF2<&"'
                print("PASS: real renderer two-radio HTML/JSON, isolation, readiness, escaping, fixed-port and legacy behavior")
            else: print(result.strip())

if __name__=="__main__": main()
