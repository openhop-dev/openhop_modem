#!/usr/bin/env python3
"""Execute production RF queue/dispatch and TCP lifecycle with socket stubs.

No MCU, network or RF access. The ESP32 backend compiles the complete TCP
server and frame parser; only Arduino sockets and radio operations are faked.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from tcp_radio_stubs.adapt_wifi_port import socket_source

FIRMWARE = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def harness(backend):
    main = (FIRMWARE / "src/main.cpp").read_text()
    code = (FIRMWARE / "tests/radio_request_connections_test.cpp").read_text()
    output = main[main.index("static ResponseRoute primaryRoute("):main.index("// HWCDC (arduino-esp32")]
    output = output.replace("#ifdef ARDUINO_ARCH_ESP32", "#ifndef TEST_W5100S")
    code = code.replace("FRAME_OUTPUT", output)
    if backend == "w5100s":
        transport = (FIRMWARE / "src/w5100s_ethernet_transport.cpp").read_text()
        tcp = transport[transport.index("namespace TCPServer {"):transport.rindex("#endif")]
        frame = function(transport, "static void buildFrame(")
        code = code.replace('#include "TCP_BACKEND"',
            '#include "w5100s_ethernet_transport.h"\n#include "net_filter.h"\n#include "w5100.h"\n'
            '#define TEST_W5100S 1\n#define OPENHOP_ETH_TCP_PORT 5055\n'
            'extern void processHostCommand(uint8_t,const uint8_t*,uint16_t,TransportSource);\n'
            'extern void noteTransportFrameError(uint8_t);\n'+frame+'\n'+tcp+
            '\nnamespace TCPServer {using ::buildFrame;}')
        code = code.replace('TCPServer::invalidateInterface(IPAddress(192,168,8,1));','(void)0;')
        code = code.replace('NETWORK_SETUP','').replace('NETWORK_INVALIDATION_SCENARIO','assert(false);')
    else:
        code = code.replace('#include "TCP_BACKEND"',
            '#include "config_portal.h"\nnamespace ConfigPortal {void begin(){}void end(){}void loop(){}bool isActive(){return false;}}\n'
            '#include "'+str(FIRMWARE/'src/wifi_manager.cpp')+'"\n#include "'+str(FIRMWARE/'src/tcp_server.cpp')+'"')
        loop=main[main.index('void loop() {'):]
        service=loop[loop.index('    // Invalidate stale TCP ownership'):loop.index('    if (tcpStarted) TCPServer::loop();')]
        watchdog=function(main,'static void kickStalledUsbSerialTx()')
        protocol=main[main.index('template <typename Stream>'):main.index('// ─── Setup')]
        service=watchdog+'\nFrameParser serialParser,uartParser;\n'+r'''
void serviceRadioIrqs(){applyReception();owner->serviceIrq();}
void onSerialFrameOk(uint8_t c,const uint8_t* p,uint16_t n,TransportSource src){processHostCommand(c,p,n,src);}
void onSerialFrameErr(uint8_t,TransportSource){}
'''+protocol+'\nvoid serviceHostInputs(){\n'+service+'\n}\n'
        code=code.replace('std::shared_ptr<SocketStub> acceptClient()',service+'\nstd::shared_ptr<SocketStub> acceptClient()')
        code=code.replace('NETWORK_SETUP',r'''
    const bool wifiScenario=scenario=="pending_wifi_usb"||scenario=="pending_wifi_uart";
    if(wifiScenario){WifiManager::begin();WiFi.statusValue=WL_CONNECTED;WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP);WifiManager::loop();}
''')
        code=code.replace('NETWORK_INVALIDATION_SCENARIO',r'''
    WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED);receiving=false;
    uint8_t frame[MAX_FRAME_SIZE];uint16_t size=0;TCPServer::buildFrame(frame,size,CMD_GET_FEM_STATE,nullptr,0);
    auto& input=scenario=="pending_wifi_usb"?Serial.rx:PROTO_UART.rx;
    input.insert(input.end(),frame,frame+size);serviceHostInputs();syncMutations();
    CHECK(mutations==0&&owner->queuedRequests()==0);
    CHECK(replies.size()==1&&destinations[0]!=TransportSource::TCP);
    CHECK(original->output.empty());
''')
    return code.replace('FRAME_PARSER',str(FIRMWARE/'src/frame_parser.cpp'))


class RequestConnectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="openhop-rf-connections-")
        work = Path(cls.temp.name)
        stubs = work / "stubs"
        shutil.copytree(FIRMWARE / "tools/radio_runtime_stubs", stubs)
        # Real Wi-Fi recovery event producer, recording SDK only; never fake RF.
        wifi=socket_source((FIRMWARE / 'tools/wifi_recovery_stubs/WiFi.h').read_text())
        wifi=wifi.replace('void setNoDelay(bool)', 'uint8_t getSocketNumber(){return s && s->open ? 0 : 4;} void setNoDelay(bool)',1)
        (stubs/'WiFi.h').write_text(wifi)
        prefs=(stubs/'Preferences.h').read_text().replace('class Preferences {','inline bool savedNetwork=true;\nclass Preferences {')
        prefs=prefs.replace(' void end(){}', ' String getString(const char* k,const char* d){return strcmp(k,"ssid")==0&&savedNetwork?"test-network":d;}\n uint32_t getUInt(const char*,uint32_t d){return d;}\n template<class T> size_t putString(const char*,T){return 1;}\n template<class T> size_t putUInt(const char*,T){return 1;}\n void clear(){}\n void end(){}')
        (stubs/'Preferences.h').write_text(prefs)
        cls.binaries = {}
        for backend in ("esp32", "w5100s"):
            source = work / (backend + ".cpp")
            source.write_text(harness(backend))
            binary = source.with_suffix("")
            flags = ["-DARDUINO_ARCH_ESP32=1", "-DBOARD_STATION_G3", "-Wno-missing-field-initializers"]
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags,
                            "-I" + str(stubs), "-I" + str(FIRMWARE / "include"),
                            "-I" + str(FIRMWARE / "tools/rf_request_connection_stubs"),
                            str(source), str(FIRMWARE/'src/radio_runtime.cpp'),
                            str(FIRMWARE/'src/radio_hardware.cpp'),str(FIRMWARE/'src/rf_frontend.cpp'),
                            "-o", str(binary)], check=True)
            cls.binaries[backend] = binary

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def scenario(self, name):
        for backend, binary in self.binaries.items():
            if name.startswith("pending_wifi_") and backend != "esp32":
                continue
            with self.subTest(backend=backend):
                result = subprocess.run([str(binary), name], text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


for name in ("reconnect_authenticated", "reconnect_unauthenticated", "reconnect_boost", "tokenless_reconnect",
             "normal", "other_interface", "mixed_fifo", "interface_loss", "restart",
             "disconnect_only", "purge_behind_usb", "full_stale", "multiple_tcp_replies",
             "pending_wifi_usb", "pending_wifi_uart", "async_capacity", "async_stale_capacity"):
    setattr(RequestConnectionTests, "test_" + name,
            lambda self, name=name: self.scenario(name))


if __name__ == "__main__":
    unittest.main(verbosity=2)
