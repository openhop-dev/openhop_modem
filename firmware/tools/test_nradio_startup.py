#!/usr/bin/env python3
"""Actual N=3/4 full startup/Ethernet and OTA-health policy with SDK-only ports."""
import os
from pathlib import Path
import subprocess
import tempfile
from nradio_test_support import compile_main,FW
from test_duo_ethernet_startup import NETWORK,ETHERNET
from test_multi_radio_management import function

def exact(text,before,after):
    assert text.count(before)==1,before
    return text.replace(before,after)

def ports(directory):
    out=Path(directory);stubs=out/'sdk';stubs.mkdir()
    (stubs/'Network.h').write_text(NETWORK);(stubs/'ETH.h').write_text(ETHERNET)
    (stubs/'sdkconfig.h').write_text('#pragma once\n#define CONFIG_ETH_USE_ESP32_EMAC 1\n#define CONFIG_IDF_TARGET_ESP32P4 1\n')
    text=(FW/'tools/radio_runtime_stubs/main_device_ports.cpp').read_text()
    first='namespace EthernetManager {';last='namespace OTAManager'
    assert text.count(first)==text.count(last)==1
    a=text.index(first);b=text.index(last,a);text=text[:a]+text[b:]
    text=exact(text,'void checkResetButton(){} void loadConfigOnly(){} void begin(){} void loop(){}',
               'int beginCalls=0; void checkResetButton(){} void loadConfigOnly(){} void begin(){++beginCalls;} void loop(){}')
    ota=(FW/'src/ota_manager.cpp').read_text();signature='static void attemptMarkValid('
    assert ota.count(signature)==1
    replacement=r'''
#include "radio_settings.h"
using esp_err_t=int;
constexpr int ESP_OK=0;
enum esp_ota_img_states_t{ESP_OTA_IMG_PENDING_VERIFY,ESP_OTA_IMG_VALID};
struct esp_partition_t{};
static esp_partition_t runningPartition;
static bool markedValid=false,sawValidFrame=false;
static uint32_t sanityDeadline=0;
static const esp_partition_t* esp_ota_get_running_partition(){return &runningPartition;}
static esp_err_t esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t* state){*state=ESP_OTA_IMG_PENDING_VERIFY;return ESP_OK;}
namespace OTAManager{int validMarks=0;}
static esp_err_t esp_ota_mark_app_valid_cancel_rollback(){++OTAManager::validMarks;return ESP_OK;}
'''+function(ota,signature)+r'''
namespace OTAManager{
int ticks=0,beginCalls=0;uint32_t healthStart=0;
void begin(const String&,const String&){++beginCalls;healthStart=millis();sanityDeadline=healthStart+120000U;}
void loop(){++ticks;attemptMarkValid();}
void notifyValidFrame(){sawValidFrame=true;}
const char* getHostname(){return "test";}
}
'''
    text=exact(text,'namespace OTAManager {int ticks=0;void begin(const String&,const String&){} void loop(){++ticks;}void notifyValidFrame(){}const char* getHostname(){return "test";}}',replacement)
    source=out/'device_ports.cpp';source.write_text(text)
    return source,stubs

def main():
    failures=[]
    with tempfile.TemporaryDirectory(prefix='nradio-startup-',dir=os.environ.get('TMPDIR')) as d:
        out=Path(d);source,stubs=ports(out)
        for n in (3,4):
            print('COMPILE actual main/runtime/hardware/frontend/settings/TCP/parser/Ethernet N='+str(n),flush=True)
            exe=compile_main(out/str(n),'tests/nradio_startup_test.cpp',n,ports=source,
                             extra_sources=('src/ethernet_manager.cpp',),extra_include=(stubs,))
            for scenario in ('all_off','high_no_host','sparse_no_host','late_link_off','late_dhcp_high','failed_high_no_host'):
                command=[str(exe),scenario];print('RUN',' '.join(command),flush=True)
                result=subprocess.run(command,check=False)
                print('CASE',n,scenario,'exit='+str(result.returncode),flush=True)
                if result.returncode:failures.append((n,scenario,result.returncode))
    if failures:raise SystemExit('FAIL '+repr(failures))
    print('PASS 12 non-Duo N=3/4 full-startup/actual OTA-health scenarios')
if __name__=='__main__':main()
