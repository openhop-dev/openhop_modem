#!/usr/bin/env python3
"""Compile real Wi-Fi manager/TCP sources against recording hardware stubs.
No network, MCU, flash or RF access. Each scenario runs in a fresh process.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUBS = ROOT / 'tools' / 'wifi_recovery_stubs'

class RecoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='openhop-74-tests-')
        cls.binary = Path(cls.tmp.name) / 'recovery'
        subprocess.run(['g++', '-std=c++17', '-DRECOVERY_IMPLEMENTED', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(STUBS), '-I' + str(ROOT / 'include'),
                        str(STUBS / 'recovery.cpp'), '-o', str(cls.binary)], check=True)

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
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(STUBS), '-I' + str(ROOT / 'include'),
                        str(STUBS / 'portal.cpp'), '-o', str(binary)], check=True)
        for case in ['station', 'ipv6', 'missing', 'zero', 'ap_off', 'stale_ap', 'ap']:
            with self.subTest(case=case):
                result = subprocess.run([str(binary), case], text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def scenario(self, name):
        result = subprocess.run([str(self.binary), name], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

for name in ['nonblocking_boot', 'startup_fallback_retry', 'no_config_ap',
             'outage_retry', 'auth_failure', 'lost_ip', 'ip_refresh',
             'short_outage', 'backoff_wrap', 'persistent_restart',
             'api_failures', 'portal_teardown', 'tcp_cleanup', 'ethernet_preserved', 'ap_start_failure', 'lost_ip_stale_status', 'socket_address_loss', 'ap_stop_failure', 'retry_cap', 'renewal_preserved', 'new_client_auth', 'static_policy']:
    setattr(RecoveryTests, 'test_' + name, lambda self, name=name: self.scenario(name))

if __name__ == '__main__':
    unittest.main(verbosity=2)
