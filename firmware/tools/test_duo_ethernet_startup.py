#!/usr/bin/env python3
"""Real setup/loop + EthernetManager late-link/DHCP and Wi-Fi fallback gates."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

FW = Path(__file__).resolve().parents[1]
NETWORK = '''#pragma once
#include <Arduino.h>
using arduino_event_id_t = int;
struct arduino_event_info_t {};
using network_event_handle_t = int;
constexpr int ARDUINO_EVENT_ETH_START = 1;
struct NetworkStub {
 network_event_handle_t onEvent(void(*)(arduino_event_id_t, arduino_event_info_t), int) { return 1; }
};
inline NetworkStub Network;
'''
ETHERNET = '''#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include "Network.h"
enum eth_phy_type_t { ETH_PHY_IP101 = 1, ETH_PHY_MAX = 99 };
using eth_clock_mode_t = int;
constexpr int EMAC_CLK_EXT_IN = 1, EMAC_CLK_OUT = 2;
struct ETHStub {
 bool active = false, cable = true, leaseReady = true;
 unsigned begins = 0, ends = 0;
 String hostname;
 bool begin(eth_phy_type_t, int, int, int, int, eth_clock_mode_t) { active = true; ++begins; return true; }
 void end() { active = false; ++ends; }
 bool setHostname(const char* n) { hostname = n; return true; }
 const char* getHostname() { return hostname.c_str(); }
 bool config(const IPAddress&, const IPAddress&, const IPAddress&, const IPAddress&, const IPAddress&) { return true; }
 bool config(const IPAddress&, const IPAddress&, const IPAddress&, const IPAddress&) { return true; }
 bool linkUp() { return active && cable; }
 bool hasIP() { return active && cable && leaseReady; }
 IPAddress localIP() { return IPAddress(192,168,1,20); }
 String macAddress() { return "00:00:00:00:00:01"; }
};
inline ETHStub ETH;
'''

def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix='duo-ethernet-startup-', dir=os.environ.get('TMPDIR')) as directory:
        out = Path(directory)
        stubs = out / 'stubs'
        shutil.copytree(FW / 'tools/radio_runtime_stubs', stubs)
        (stubs / 'Network.h').write_text(NETWORK)
        (stubs / 'ETH.h').write_text(ETHERNET)
        (stubs / 'sdkconfig.h').write_text('#pragma once\n#define CONFIG_ETH_USE_ESP32_EMAC 1\n#define CONFIG_IDF_TARGET_ESP32P4 1\n')
        ports = (stubs / 'main_device_ports.cpp').read_text()
        start = 'namespace EthernetManager {'
        end = 'namespace OTAManager'
        assert ports.count(start) == ports.count(end) == 1
        first = ports.index(start); last = ports.index(end, first)
        ports = ports[:first] + ports[last:]
        before = 'void checkResetButton(){} void loadConfigOnly(){} void begin(){} void loop(){}'
        after = 'int beginCalls=0; void checkResetButton(){} void loadConfigOnly(){} void begin(){++beginCalls;} void loop(){}'
        assert ports.count(before) == 1
        ports = ports.replace(before, after)
        from test_multi_radio_management import function
        ota = (FW / 'src/ota_manager.cpp').read_text()
        validity = function(ota, 'static void attemptMarkValid(')
        before = 'namespace OTAManager {int ticks=0;void begin(const String&,const String&){} void loop(){++ticks;}void notifyValidFrame(){}const char* getHostname(){return "test";}}'
        after = r'''
// Actual production rollback sanity logic; only ESP-IDF partition APIs are ports.
#include "radio_settings.h"
using esp_err_t = int;
constexpr int ESP_OK = 0;
enum esp_ota_img_states_t { ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID };
struct esp_partition_t {};
static esp_partition_t runningPartition;
static bool markedValid=false, sawValidFrame=false;
static uint32_t sanityDeadline=0;
static const esp_partition_t* esp_ota_get_running_partition(){return &runningPartition;}
static esp_err_t esp_ota_get_state_partition(const esp_partition_t*,esp_ota_img_states_t* state){*state=ESP_OTA_IMG_PENDING_VERIFY;return ESP_OK;}
namespace OTAManager {int validMarks=0;}
static esp_err_t esp_ota_mark_app_valid_cancel_rollback(){++OTAManager::validMarks;return ESP_OK;}
''' + validity + r'''
namespace OTAManager {
int ticks=0,beginCalls=0;
void begin(const String&,const String&){++beginCalls;sanityDeadline=millis()+120000U;}
void loop(){++ticks;attemptMarkValid();}
void notifyValidFrame(){sawValidFrame=true;}
const char* getHostname(){return "test";}
}
'''
        assert ports.count(before) == 1
        ports = ports.replace(before, after)
        (out / 'device_ports.cpp').write_text(ports)
        sources = ['src/main.cpp', 'src/radio_settings.cpp', 'src/radio_runtime.cpp', 'src/radio_hardware.cpp', 'src/rf_frontend.cpp', 'src/tcp_server.cpp', 'src/frame_parser.cpp', 'src/ethernet_manager.cpp', 'tests/duo_ethernet_startup_test.cpp']
        for board, scenarios in [('BOARD_ETHERMESH_DUO', ('online', 'late_link', 'late_dhcp', 'both_off', 'rf1_only', 'failed_rf1', 'both_off_http_only', 'enabled_no_host')), ('BOARD_ESP32_P4_NANO', ('wifi_fallback',))]:
            binary = out / board
            cmd = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers', '-DARDUINO_ARCH_ESP32', '-D' + board, '-I' + str(stubs), '-I' + str(FW / 'include'), *[str(FW / source) for source in sources], str(out / 'device_ports.cpp'), '-o', str(binary)]
            print(' '.join(cmd), flush=True)
            subprocess.run(cmd, check=True)
            for scenario in scenarios:
                result = subprocess.run([str(binary), scenario], check=False)
                print('CASE', scenario, 'exit=' + str(result.returncode), flush=True)
                if result.returncode:
                    failures.append(scenario)
    if failures:
        raise SystemExit('FAIL: ' + ', '.join(failures))
    print('PASS: real Ethernet startup/late-link/DHCP, both-off HTTP-only OTA, enabled no-host control, single/failing RF1, preserved Wi-Fi fallback (9 cases)')

if __name__ == '__main__':
    main()
