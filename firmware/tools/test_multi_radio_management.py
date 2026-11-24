#!/usr/bin/env python3
"""Run actual radio model binding and authenticated /api/radios handler.

Only runtime snapshots, client-IP lookups and HTTP SDK are fake; mapping,
serialization, admission and handler code come from production.
"""
from pathlib import Path
import json
import os
import subprocess
import tempfile

FW=Path(__file__).resolve().parents[1]

def function(source,signature):
    start=source.index(signature)
    brace=source.index("{",start)
    depth,end=1,brace+1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}")
        end+=1
    return source[start:end]

def main():
    text=(FW/"src/ota_manager.cpp").read_text()
    assert "static void populateRadioModels(" in text, "served management model has no independent radios"
    binding=function(text,"static void populateRadioModels(")
    route=function(text,"static void handleApiRadios(")
    code=r'''
#include "Arduino.h"
#include "runtime_stats.h"
#include "webui_shared.h"
#include <cassert>
#include <iostream>
#include <vector>
static bool authorized=false;
static int reads=0, sends=0;
static String response;
static std::vector<RuntimeStats::RadioSnapshot> values;
namespace RuntimeStats {
size_t radioCount(){return values.size();}
bool captureRadio(size_t i,RadioSnapshot& out){++reads;if(i>=values.size())return false;out=values[i];return true;}
}
namespace TCPServer {String getClientIP(uint8_t i){return i==0?"192.168.1.1":"192.168.1.2";}}
bool checkAuth(){return authorized;}
void sendJson(int status,const String& data){assert(status==200);++sends;response=data;}
''' + binding + r'''
WebUiShared::Model buildWebUiModel(){WebUiShared::Model model;populateRadioModels(model);return model;}
''' + route + r'''
int main(){
RuntimeStats::RadioSnapshot a{}, b{};
a.id=0;a.label="RF1";a.tcpPort=5055;a.present=true;a.ready=true;a.txReady=true;a.autoCadEnabled=true;
a.radio.freq_hz=910525000;a.radio.sf=8;a.status.rx_count=11;
b=a;b.id=1;b.label="RF2";b.tcpPort=5056;b.enabled=false;b.ready=false;b.txReady=false;
b.radio.freq_hz=915000000;b.status.rx_count=22;
values={a,b};handleApiRadios();assert(reads==0&&sends==0);
authorized=true;handleApiRadios();assert(reads==2&&sends==1);
auto model=buildWebUiModel();assert(model.radios.size()==2);
assert(model.radios[0].ready&&!model.radios[1].ready);
assert(model.radios[0].radio.autoCadEnabled);
assert(model.radios[0].counters.rxPackets==11&&model.radios[1].counters.rxPackets==22);
assert(model.radios[1].radio.frequencyHz==915000000);
assert(model.radios[0].connectedClientIp=="192.168.1.1");
assert(model.radios[1].connectedClientIp=="192.168.1.2");
assert(model.radios[1].radio.available && !model.radios[1].enabled);
assert(model.radios[1].radio.state=="Disabled");
values[1].enabled=true;auto failed=buildWebUiModel();
assert(failed.radios[1].enabled && failed.radios[1].radio.state=="Error");
std::cout<<response<<std::endl;
}
'''
    assert 'httpServer->on("/api/radios", HTTP_GET, handleApiRadios);' in text
    model=function(text,"static WebUiShared::Model buildWebUiModel(")
    assert "populateRadioModels(model);" in model
    with tempfile.TemporaryDirectory(prefix="openhop-radio-management-",dir=os.environ.get("TMPDIR")) as temp:
        out=Path(temp); source=out/"test.cpp"; exe=out/"test"; source.write_text(code)
        subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",f"-I{FW/'tools/wifi_recovery_stubs'}",
                        f"-I{FW/'include'}",str(source),str(FW/"src/webui_shared.cpp"),"-o",str(exe)],check=True)
        data=json.loads(subprocess.check_output([str(exe)],text=True))
        assert [x["tcp_port"] for x in data]==[5055,5056]
        assert [x["counters"]["rx_packets"] for x in data]==[11,22]
        assert [x["enabled"] for x in data]==[True,False]
        print("PASS: production model binding, per-radio config/counters/client, authenticated route and JSON")

if __name__=="__main__":main()
