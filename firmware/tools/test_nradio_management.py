#!/usr/bin/env python3
"""Host N=3/4: actual HTTP handlers, ArduinoJson, store, model binding and renderer.

Only SDK boundaries and snapshot/IP producers are ports. The board selector and
BoardConfig are actual production headers with immutable synthetic board DATA.
"""
import os
from pathlib import Path
import subprocess
import tempfile
from nradio_test_support import prepare_headers
from test_multi_radio_management import function
FW = Path(__file__).resolve().parents[1]


CPP = r'''
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "radio_settings.h"
#include "radio_layout.h"
#include "runtime_stats.h"
#include "webui_shared.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
struct Http {
 std::vector<std::pair<String,String>> fields;
 String body;
 int args(){return fields.size();}
 String argName(int i){return fields.at(i).first;}
 String arg(int i){return fields.at(i).second;}
 String arg(const char* name){if(std::string(name)=="plain")return body;for(auto& f:fields)if(f.first==name)return f.second;return "";}
} http, *httpServer=&http;
static bool authorized=false;
static int status=0,replies=0,reads=0;
static String response;
static std::vector<RuntimeStats::RadioSnapshot> snapshots;
static std::vector<String> ips;
bool checkAuth(){return authorized;}
void sendJson(int s,const String& b){status=s;response=b;++replies;}
void sendJsonError(int s,const String& b){sendJson(s,b);}
namespace RuntimeStats {
size_t radioCount(){return snapshots.size();}
bool captureRadio(size_t i,RadioSnapshot& out){++reads;if(i>=snapshots.size())return false;out=snapshots[i];return true;}
}
namespace TCPServer {String getClientIP(uint8_t i){return ips.at(i);}}
// PRODUCTION_FUNCTIONS
static void reset(const std::string& body=""){
 http.body=body;http.fields.clear();status=0;response="";replies=0;ESP.restarts=0;
}
static std::string jsonFor(unsigned mask,size_t count=NRADIO_COUNT){
 std::string s="{\"enabled\":[";
 for(size_t i=0;i<count;++i){if(i)s+=",";s+=(mask&(1U<<i))?"true":"false";}
 return s+"]}";
}
static void unchanged(unsigned active,unsigned saved,int expected){
 assert(status==expected&&ESP.restarts==0);
 assert(RadioSettings::activeMask()==active&&preferences["enabled"].value==saved);
}
static void saved(unsigned mask,unsigned active){
 assert(status==200&&replies==1&&ESP.restarts==1);
 assert(RadioSettings::activeMask()==active&&"save must not change active owners before load/reboot");
 assert(preferences["enabled"].type=='c'&&preferences["enabled"].value==mask);
 JsonDocument doc;assert(!deserializeJson(doc,response.c_str()));
 assert(doc["status"]=="saved"&&doc["rebooting"].as<bool>());
 auto a=doc["enabled"].as<JsonArrayConst>();assert(a.size()==NRADIO_COUNT);
 for(size_t i=0;i<a.size();++i)assert(a[i].is<bool>()&&a[i].as<bool>()==bool(mask&(1U<<i)));
 RadioSettings::load();assert(RadioSettings::activeMask()==mask);
}
static void httpCases(){
 assert(RadioLayout::valid()&&RadioLayout::count()==NRADIO_COUNT);
 assert(RadioSettings::supported()&&"non-Duo explicit-capability multi-owner board must support selection");
 RadioSettings::load();assert(RadioSettings::activeMask()==NRADIO_DEFAULT_MASK);
 const unsigned high=1U<<(NRADIO_COUNT-1),all=(1U<<NRADIO_COUNT)-1,sparse=1U|high;
 preferences["enabled"]={'c',NRADIO_DEFAULT_MASK};
 authorized=false;reset(jsonFor(all));const auto beforeReads=preferenceReads;handleApiRadiosPost();
 unchanged(NRADIO_DEFAULT_MASK,NRADIO_DEFAULT_MASK,0);assert(replies==0&&preferenceReads==beforeReads);
 reset();http.fields={{"radio_form","1"},{String("rf")+String(unsigned(NRADIO_COUNT-1)),"1"}};handleRadioSave();
 unchanged(NRADIO_DEFAULT_MASK,NRADIO_DEFAULT_MASK,0);assert(replies==0&&preferenceReads==beforeReads);
 authorized=true;
 for(unsigned mask:{high,0U,sparse,all}){
  unsigned active=RadioSettings::activeMask();reset(jsonFor(mask));handleApiRadiosPost();saved(mask,active);
  active=RadioSettings::activeMask();reset();http.fields={{"radio_form","1"}};
  for(unsigned i=0;i<NRADIO_COUNT;++i)if(mask&(1U<<i))http.fields.push_back({String("rf")+String(i),"1"});
  handleRadioSave();saved(mask,active);
 }
 const unsigned active=RadioSettings::activeMask(),stored=preferences["enabled"].value;
 std::vector<std::string> bad={"{}","[]","null","","bad",jsonFor(all,NRADIO_COUNT-1),jsonFor(all,NRADIO_COUNT+1),"{\"enabled\":true}","{\"enabled\":[],\"typo\":1}"};
 for(const char* value:{"1","\"true\"","null","{}","[]"}){
  std::string s="{\"enabled\":[";for(unsigned i=0;i<NRADIO_COUNT-1;++i)s+="false,";s+=value;s+="]}";bad.push_back(s);
 }
 auto unknown=jsonFor(all);unknown.pop_back();bad.push_back(unknown+",\"typo\":1}");
 for(const auto& s:bad){reset(s);handleApiRadiosPost();unchanged(active,stored,400);}
 using Fields=std::vector<std::pair<String,String>>;
 std::vector<Fields> invalid={ {},{{"rf0","1"}},{{"radio_form","1"},{String("rf")+String(unsigned(NRADIO_COUNT)),"1"}},{{"radio_form","1"},{"rf0","true"}},{{"radio_form","1"},{"typo","1"}},{{"radio_form","1"},{"rf0","1"},{"rf0","1"}},{{"radio_form","1"},{"radio_form","1"}} };
 for(const auto& fields:invalid){reset();http.fields=fields;handleRadioSave();unchanged(active,stored,400);}
 for(bool open:{false,true}){
  preferenceOpen=open;preferenceWrite=false;reset(jsonFor(0));handleApiRadiosPost();unchanged(active,stored,500);
  reset();http.fields={{"radio_form","1"},{"rf2","1"}};handleRadioSave();unchanged(active,stored,500);
 }
 preferenceOpen=true;preferenceWrite=true;
 JsonDocument config;assert(!deserializeJson(config,jsonFor(high)));assert(!knownConfigFields(config.as<JsonObjectConst>()));
 std::cout<<"PASS HTTP N="<<NRADIO_COUNT<<": exact typed JSON/form, highest/all-off/sparse, auth, failed open/write, active load boundary\n";
}
static WebUiShared::Model modelCases(){
 const unsigned mask=1U|(1U<<(NRADIO_COUNT-1));
 for(unsigned i=0;i<NRADIO_COUNT;++i){
  RuntimeStats::RadioSnapshot s{};s.id=i;s.label=RadioLayout::at(i).label;s.tcpPort=5055+i;s.present=true;
  s.enabled=bool(mask&(1U<<i));s.ready=s.enabled&&i!=NRADIO_COUNT-1;s.txReady=i==0;s.autoCadEnabled=i%2;
  s.radio.freq_hz=910000000+1000000*i;s.radio.syncword=0x1200+i;s.status.rx_count=11*(i+1);
  snapshots.push_back(s);ips.push_back(String("192.0.2.")+String(10U+i));
 }
 authorized=false;reads=0;replies=0;handleApiRadios();assert(reads==0&&replies==0);
 authorized=true;handleApiRadios();assert(reads==NRADIO_COUNT&&replies==1&&status==200);
 JsonDocument doc;assert(!deserializeJson(doc,response.c_str()));assert(doc.as<JsonArrayConst>().size()==NRADIO_COUNT);
 WebUiShared::Model model;populateRadioModels(model);assert(model.radios.size()==NRADIO_COUNT);
 for(unsigned i=0;i<NRADIO_COUNT;++i){
  const auto& e=model.radios[i];assert(e.id==i&&e.tcpPort==5055+i&&e.label=="RF"+std::to_string(i+1));
  assert(e.enabled==snapshots[i].enabled&&e.ready==snapshots[i].ready&&e.txReady==snapshots[i].txReady);
  assert(e.connectedClientIp==ips[i].c_str()&&e.radio.frequencyHz==910000000+1000000*i);
  assert(e.counters.rxPackets==11*(i+1)&&e.radio.syncword==0x1200+i);
  assert(e.radio.state==(i==0?"RX/Idle":i==NRADIO_COUNT-1?"Error":"Disabled"));
 }
 const auto firstIP=ips.front();std::swap(ips.front(),ips.back());WebUiShared::Model swapped;populateRadioModels(swapped);
 assert(swapped.radios.back().connectedClientIp==firstIP.c_str()&&swapped.radios.front().connectedClientIp==ips.front().c_str());
 ips.back()="";WebUiShared::Model disconnected;populateRadioModels(disconnected);
 assert(disconnected.radios.back().connectedClientIp.empty()&&!disconnected.radios.front().connectedClientIp.empty());
 model.capabilities.radioSelection=true;model.capabilities.writableManagement=true;model.capabilities.radio=true;
 model.connectedClientIp="203.0.113.254";
 const auto html=WebUiShared::renderRootPage(model);
 for(unsigned i=0;i<NRADIO_COUNT;++i){
  const auto field="name='rf"+std::to_string(i)+"' value='1'";
  const auto at=html.find(field);assert(at!=std::string::npos);
  assert((html.substr(at+field.size(),8).find("checked")!=std::string::npos)==model.radios[i].enabled);
  assert(html.find(model.radios[i].connectedClientIp)!=std::string::npos);
 }
 assert(html.find("203.0.113.254")==std::string::npos&&"multi-owner page must not show legacy primary-only client IP");
 assert(html.find("action='/radios'")!=std::string::npos);
 std::cout<<"PASS models/render N="<<NRADIO_COUNT<<": per-owner config/state/counters/IP, rf2/rf3, swap/disconnect\n";
 return model;
}
int main(int argc,char**argv){
 assert(argc>=2);const std::string mode=argv[1];
 if(mode=="http"){httpCases();return 0;}
 auto model=modelCases();
 const auto html=WebUiShared::renderRootPage(model);
 if(mode=="wording")assert(html.find("Both radios may be disabled")==std::string::npos&&"N>2 radio selection help must be count neutral");
 if(mode=="emit"){assert(argc==3);std::ofstream(argv[2])<<html;}
}
'''

def build(directory, count):
    out=Path(directory);out.mkdir(parents=True,exist_ok=True)
    include,flags=prepare_headers(out,count,1|(1<<(count-1)))
    ota=(FW/'src/ota_manager.cpp').read_text()
    signatures=('static void saveRadioSelection(', 'static void handleRadioSave(', 'static void handleApiRadiosPost(', 'static bool knownConfigFields(', 'static void populateRadioModels(', 'static void handleApiRadios(')
    extracted=[]
    for signature in signatures:
        assert ota.count(signature)==1,signature
        extracted.append(function(ota,signature))
    for path,method,handler in (('/radios','POST','handleRadioSave'),('/api/radios','POST','handleApiRadiosPost'),('/api/radios','GET','handleApiRadios')):
        assert f'httpServer->on("{path}", HTTP_{method}, {handler});' in ota
    assert 'populateRadioModels(model);' in function(ota,'static WebUiShared::Model buildWebUiModel(')
    assert 'knownConfigFields(obj)' in function(ota,'static bool applyConfigPatch(')
    source=out/'management.cpp';source.write_text(CPP.replace('// PRODUCTION_FUNCTIONS','\n'.join(extracted)))
    dependency=Path(os.environ.get('OPENHOP_ARDUINOJSON_INCLUDE',
                    str(FW/'.pio/libdeps/ethermesh_duo_v3/ArduinoJson/src')))
    assert (dependency/'ArduinoJson.h').exists(),'real locally resolved ArduinoJson required; no install permitted'
    print('ArduinoJson provenance (read only):',dependency,flush=True)
    exe=out/'management'
    command=['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',*flags,
             '-DARDUINOJSON_ENABLE_ARDUINO_STRING=0','-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0','-DARDUINOJSON_ENABLE_PROGMEM=0',
             '-I'+str(FW/'tools/radio_runtime_stubs'),'-I'+str(include),'-I'+str(dependency),str(source),str(FW/'src/radio_settings.cpp'),str(FW/'src/webui_shared.cpp'),'-o',str(exe)]
    print(' '.join(command),flush=True);subprocess.run(command,check=True)
    return exe

def main():
    failures=[]
    with tempfile.TemporaryDirectory(prefix='nradio-management-',dir=os.environ.get('TMPDIR')) as d:
        for n in (3,4):
            exe=build(Path(d)/str(n),n)
            for mode in ('models','http','wording'):
                command=[str(exe),mode];print('RUN',' '.join(command),flush=True)
                result=subprocess.run(command,check=False)
                print('CASE',n,mode,'exit='+str(result.returncode),flush=True)
                if result.returncode:failures.append((n,mode,result.returncode))
    if failures:raise SystemExit('FAIL '+repr(failures))
    print('PASS all six N=3/4 management cases')
if __name__=='__main__':main()
