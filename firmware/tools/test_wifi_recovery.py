#!/usr/bin/env python3
"""Compile real Wi-Fi manager/TCP sources against recording hardware stubs.
No network, MCU, flash or RF access. Each scenario runs in a fresh process.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from tcp_radio_stubs.adapt_wifi_port import stage_socket_port

ROOT = Path(__file__).resolve().parents[1]
STUBS = ROOT / 'tools' / 'wifi_recovery_stubs'

def primary_session_harness(source):
    # The legacy API still owns radio zero. Preserve every original lifecycle
    # assertion while adapting the fixture's private singleton field accesses.
    return (source.replace('"../../src/', '"' + str(ROOT / 'src') + '/')
            .replace('TCPServer::authenticated', 'TCPServer::sessions[0].authenticated')
            .replace('TCPServer::parser', 'TCPServer::sessions[0].parser'))

class RecoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='openhop-74-tests-')
        cls.network_stubs = Path(cls.tmp.name) / 'network_stubs'
        stage_socket_port(cls.network_stubs)
        cls.binary = Path(cls.tmp.name) / 'recovery'
        source = Path(cls.tmp.name) / 'recovery.cpp'
        source.write_text(primary_session_harness((STUBS / 'recovery.cpp').read_text()))
        subprocess.run(['g++', '-std=c++17', '-DARDUINO_ARCH_ESP32=1', '-DRECOVERY_IMPLEMENTED', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(cls.network_stubs), '-I' + str(STUBS), '-I' + str(ROOT / 'include'),
                        str(source), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_portal_real_lifecycle(self):
        # Compile the actual lifecycle functions (page/form rendering excluded).
        source = (ROOT / 'src/config_portal.cpp').read_text()
        lifecycle = source[source.index('void begin() {'):source.index('} // namespace ConfigPortal')]
        harness = r'''
#include "WiFi.h"
#include <cassert>
constexpr int HTTP_GET=0, HTTP_POST=1;
int stops=0, destroyed=0, handled=0;
uint32_t bound=0;
struct WebServer {
 WebServer(int) {bound=0;}
 WebServer(IPAddress ip,int) {bound=ip;}
 ~WebServer(){destroyed++;}
 template<class... T> void on(T...){}
 template<class T> void onNotFound(T){}
 void send(int,const char*,const char*){}
 void begin(){} void stop(){stops++;} void handleClient(){handled++;}
};
namespace ConfigPortal {
WebServer* server=nullptr; bool active=false;
void handleRoot(){} void handleSave(){}
bool admitRequest(){return true;} // Admission is exercised by the complete-handler test.
''' + lifecycle + r'''
}
int main(){
 ConfigPortal::begin(); assert(bound==uint32_t(WiFi.softAPIP()));
 ConfigPortal::begin(); ConfigPortal::loop(); assert(handled==1);
 ConfigPortal::end(); ConfigPortal::end();
 assert(!ConfigPortal::isActive() && stops==1 && destroyed==1);
 ConfigPortal::loop(); assert(handled==1);
 ConfigPortal::begin(); assert(ConfigPortal::isActive()); ConfigPortal::end();
 assert(stops==2 && destroyed==2);
}
'''
        path = Path(self.tmp.name) / 'portal.cpp'
        path.write_text(harness)
        binary = path.with_suffix('')
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(STUBS), str(path), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

    def test_portal_request_admission(self):
        binary = Path(self.tmp.name) / 'portal_admission'
        # The portal uses the recording RF functions below, not a hardware
        # Controller. Keep its production declarations, but forward-declare the
        # opaque constructor type instead of selecting real board GPIO policy.
        rf_api = Path(self.tmp.name) / 'portal_rf_api.h'
        rf_api.write_text((ROOT / 'include/rf_frontend.h').read_text().replace(
            '#include "board_config.h"', 'struct BoardConfig;'))
        portal = Path(self.tmp.name) / 'portal_source.cpp'
        # The portal's network-admission fixture has a small BoardStub, while
        # test_radio_layout.py separately compiles the actual board/layout policy.
        layout_api = Path(self.tmp.name) / 'portal_layout_api.h'
        layout_api.write_text('#pragma once\n#include <cstdint>\n#include <stddef.h>\nnamespace RadioLayout {\ninline bool fixedPorts(){return false;}\ninline uint16_t port(size_t,uint16_t configured=5055){return configured?configured:5055;}\ninline bool acceptsPrimaryPort(uint16_t value){return value!=0 && value!=80;}\n}\n')
        portal.write_text((ROOT / 'src/config_portal.cpp').read_text().replace(
            '#include "rf_frontend.h"', '#include "' + str(rf_api) + '"').replace(
            '#include "radio_layout.h"', '#include "' + str(layout_api) + '"'))
        fixture = Path(self.tmp.name) / 'portal_admission.cpp'
        fixture.write_text((STUBS / 'portal.cpp').read_text().replace(
            '"../../src/config_portal.cpp"', '"' + str(portal) + '"'))
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(STUBS), '-I' + str(ROOT / 'include'),
                        str(fixture), '-o', str(binary)], check=True)
        for case in ['station', 'ipv6', 'missing', 'zero', 'ap_off', 'stale_ap', 'ap']:
            with self.subTest(case=case):
                result = subprocess.run([str(binary), case], text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def scenario(self, name):
        result = subprocess.run([str(self.binary), name], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_main_preserves_real_outage_invalidation_but_not_gateway_only_renewal(self):
        # Execute the actual main-loop consumer with real Wi-Fi manager and
        # TCP server, not a direct call to invalidateInterface in the fixture.
        main = (ROOT / 'src/main.cpp').read_text()
        start = main.index('    const uint32_t invalidSTA = WifiManager::consumeSTAInvalidation();')
        consumer = main[start:main.index('\n#endif', start)]
        preamble = (STUBS / 'recovery.cpp').read_text().split('int main(', 1)[0]
        harness = preamble + '\nvoid consumeFromMain(){\n' + consumer + r'''
}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 const std::string scenario=argv[1];
 WifiManager::begin(); connected(); consumeFromMain();
 TCPServer::begin(5055,"secret");
 incomingClient.s=std::make_shared<SocketStub>(); auto socket=incomingClient.s;
 TCPServer::loop(); TCPServer::authenticated=true;
 TCPServer::parser.state=FrameParser::READ_PAYLOAD;
 if(scenario=="same_ip_after_loss"){
  WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP,0,true);
 }else if(scenario=="gateway_only_renewal"){
  // SDK ip_changed includes netmask/gateway changes, not only local IP.
  WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP,0,true);
 }else if(scenario=="transient_address_change"){
  auto old=WiFi.ip; WiFi.ip=IPAddress(192,168,1,99);
  WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP,0,true);
  WiFi.ip=old; WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP,0,true);
 }else return 2;
 WifiManager::loop(); consumeFromMain();
 const bool preserve=scenario=="gateway_only_renewal";
 CHECK(socket->open==preserve);
 CHECK(TCPServer::authenticated==preserve);
 CHECK((TCPServer::parser.state==FrameParser::READ_PAYLOAD)==preserve);
 return 0;
}
'''
        path = Path(self.tmp.name) / 'main_invalidation.cpp'
        path.write_text(primary_session_harness(harness))
        binary = path.with_suffix('')
        subprocess.run(['g++', '-std=c++17', '-DARDUINO_ARCH_ESP32=1', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(self.network_stubs), '-I' + str(STUBS), '-I' + str(ROOT / 'include'),
                        str(path), '-o', str(binary)], check=True)
        for case in ['same_ip_after_loss', 'gateway_only_renewal', 'transient_address_change']:
            with self.subTest(case=case):
                result = subprocess.run([str(binary), case], text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

for name in ['nonblocking_boot', 'startup_fallback_retry', 'no_config_ap',
             'outage_retry', 'auth_failure', 'lost_ip', 'ip_refresh',
             'short_outage', 'backoff_wrap', 'persistent_restart',
             'api_failures', 'portal_teardown', 'tcp_cleanup', 'ethernet_preserved', 'ap_start_failure', 'lost_ip_stale_status', 'socket_address_loss', 'ap_stop_failure', 'retry_cap', 'renewal_preserved', 'new_client_auth', 'static_policy']:
    setattr(RecoveryTests, 'test_' + name, lambda self, name=name: self.scenario(name))

if __name__ == '__main__':
    unittest.main(verbosity=2)
