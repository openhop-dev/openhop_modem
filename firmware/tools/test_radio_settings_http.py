#!/usr/bin/env python3
"""Execute production radio HTTP handlers, real JSON parser/store/renderer; SDK-only ports."""
from pathlib import Path
import os, subprocess, tempfile
from test_multi_radio_management import function
FW=Path(__file__).resolve().parents[1]

def main():
    text=(FW/'src/ota_manager.cpp').read_text()
    code=r'''
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "radio_settings.h"
#include "radio_layout.h"
#include "webui_shared.h"
#include <cassert>
#include <iostream>
#include <vector>
struct Http {
    std::vector<std::pair<String,String>> fields;
    String body;
    int args() { return fields.size(); }
    String argName(int i) { return fields.at(i).first; }
    String arg(int i) { return fields.at(i).second; }
    String arg(const char* name) { if(std::string(name)=="plain") return body; for(auto& f:fields) if(f.first==name)return f.second; return ""; }
} http, *httpServer=&http;
static bool authorized=false;
static int status=0, replies=0;
static String response;
bool checkAuth(){return authorized;}
void sendJson(int s,const String& b){status=s;response=b;++replies;}
void sendJsonError(int s,const String& b){sendJson(s,b);}

'''
    for signature in ('static void saveRadioSelection(', 'static void handleRadioSave(', 'static void handleApiRadiosPost('):
        if signature in text:
            assert text.count(signature)==1
            code+=function(text,signature)+'\n'
    assert 'knownConfigFields(obj)' in function(text, 'static bool applyConfigPatch('), 'config endpoint must reject ignored radio/unknown writes'
    code+=function(text,'static bool knownConfigFields(')+'\n'
    # Unregistered HTTP routes use the SDK's normal not-found behavior, not a fake save.
    for path, handler in (('/radios','handleRadioSave'),('/api/radios','handleApiRadiosPost')):
        registration=f'httpServer->on("{path}", HTTP_POST, {handler});'
        name='formRequest' if path=='/radios' else 'jsonRequest'
        code+=f'void {name}(){{'+(f'{handler}();' if registration in text else 'sendJsonError(404,"Not found");')+'}\n'
    code+=r'''
void reset(const char* body="") { http.body=body; http.fields.clear(); status=0; response=""; replies=0; ESP.restarts=0; }
void rejectedJson(const char* raw) {
    reset(raw); const auto before=preferences["enabled"].value; jsonRequest();
    assert(status==400 && ESP.restarts==0 && preferences["enabled"].value==before);
    assert(RadioSettings::activeMask()==1);
}
int main(){
    if (!RadioSettings::supported()) {
        preferences["enabled"]={'c',0};const auto reads=preferenceReads;
        RadioSettings::load();
        assert(RadioSettings::activeMask()==RadioSettings::declaredMask());
        assert(preferenceReads==reads && !RadioSettings::save(0));
        authorized=true;reset("{\"enabled\":[false]}");jsonRequest();
        assert(status==400 && ESP.restarts==0);
        reset();http.fields={{"radio_form","1"}};formRequest();
        assert(status==400 && ESP.restarts==0 && preferences["enabled"].value==0);
        std::cout<<"PASS single-radio default unchanged; unsupported selection writes rejected\n";
        return 0;
    }
    RadioSettings::load();
    JsonDocument config;
    deserializeJson(config,"{\"enabled\":[true,false]}");
    assert(!knownConfigFields(config.as<JsonObjectConst>()) && "config endpoint must not silently ignore radio selection");
    for(const char* raw:{"{\"radios\":[]}","{\"rf0\":true}","{\"enabeld\":true}","{\"hostname\":\"test-modem\",\"enabled\":[false,false]}"}) {
        deserializeJson(config,raw); assert(!knownConfigFields(config.as<JsonObjectConst>()));
    }
    deserializeJson(config,"{\"hostname\":\"test-modem\",\"tcp_port\":5055,\"network\":{}}");
    assert(knownConfigFields(config.as<JsonObjectConst>()));
    authorized=true; reset("{\"enabled\":[false,false]}"); jsonRequest();
    assert(status==200 && "authenticated JSON selection route must save successfully");
    assert(ESP.restarts==1 && RadioSettings::activeMask()==1);
    assert(preferences["enabled"].value==0);
    assert(std::string(response.c_str()).find("saved")!=std::string::npos && std::string(response.c_str()).find("rebooting")!=std::string::npos);
    assert(std::string(response.c_str()).find("[false,false]")!=std::string::npos);
    authorized=false; reset("{\"enabled\":[true,true]}"); jsonRequest();
    assert(ESP.restarts==0 && replies==0 && preferences["enabled"].value==0);
    reset(); http.fields={{"radio_form","1"},{"rf0","1"}}; formRequest();
    assert(ESP.restarts==0 && replies==0 && preferences["enabled"].value==0);
    authorized=true;
    for(const char* body:{"{}","[]","null","", "bad", "{\"enabled\":[true]}","{\"enabled\":[true,false,true]}","{\"enabled\":[1,false]}","{\"enabled\":[\"true\",false]}","{\"enabled\":[null,false]}","{\"enabled\":[true,false],\"typo\":1}","{\"enabeld\":[true,false]}"}) rejectedJson(body);
    for(auto fields:std::vector<std::vector<std::pair<String,String>>>{ {}, {{"rf0","1"}}, {{"radio_form","1"},{"rf2","1"}}, {{"radio_form","1"},{"rf0","true"}}, {{"radio_form","1"},{"typo","1"}}, {{"radio_form","1"},{"rf0","1"},{"rf0","1"}}, {{"radio_form","1"},{"radio_form","1"}} }) {
        reset(); http.fields=fields; formRequest(); assert(status==400 && ESP.restarts==0 && preferences["enabled"].value==0);
    }
    reset(); http.fields={{"radio_form","1"}}; formRequest();
    assert(status==200 && ESP.restarts==1 && preferences["enabled"].value==0);
    reset(); http.fields={{"radio_form","1"},{"rf1","1"}}; formRequest();
    assert(status==200 && ESP.restarts==1 && preferences["enabled"].value==2);
    preferenceWrite=false; reset("{\"enabled\":[true,true]}"); jsonRequest();
    assert(status==500 && ESP.restarts==0 && RadioSettings::activeMask()==1 && preferences["enabled"].value==2);
    reset(); http.fields={{"radio_form","1"},{"rf0","1"}}; formRequest();
    assert(status==500 && ESP.restarts==0 && preferences["enabled"].value==2);
    preferenceWrite=true;
    std::cout<<"PASS actual authenticated radio form/JSON handlers: typed exact schema, both-off, routing, checked failure, saved/reboot reply, active unchanged\n";
}
'''
    dependency=FW/'.pio/libdeps/ethermesh_duo_v3/ArduinoJson/src'
    assert (dependency/'ArduinoJson.h').exists(), 'resolved local ArduinoJson dependency required (do not download)'
    with tempfile.TemporaryDirectory(prefix='radio-http-',dir=os.environ.get('TMPDIR')) as d:
        source=Path(d)/'http.cpp'; source.write_text(code); exe=Path(d)/'http'
        cmd=['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-DARDUINO_ARCH_ESP32','-DBOARD_ETHERMESH_DUO','-DARDUINOJSON_ENABLE_ARDUINO_STRING=0','-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0','-DARDUINOJSON_ENABLE_PROGMEM=0','-I'+str(FW/'tools/radio_runtime_stubs'),'-I'+str(FW/'include'),'-I'+str(dependency),str(source),str(FW/'src/radio_settings.cpp'),str(FW/'src/webui_shared.cpp'),'-o',str(exe)]
        for board in ('BOARD_ETHERMESH_DUO','BOARD_ESP32_P4_NANO'):
            args=[('-D'+board if arg=='-DBOARD_ETHERMESH_DUO' else arg) for arg in cmd]
            print(' '.join(args),flush=True); subprocess.run(args,check=True); subprocess.run([str(exe)],check=True)
if __name__=='__main__': main()
