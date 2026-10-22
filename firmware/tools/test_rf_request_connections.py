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
    queue = main[main.index("// ─── RF requests"):main.index("// ─── Host command dispatch")]
    cases = main[main.index("    case CMD_GET_FEM_STATE:\n"):main.index("    case CMD_STATUS_REQ: {\n")]
    code = r'''
#include "Arduino.h"
#include "WiFi.h"
#include "protocol.h"
#include "rf_request_queue.h"
#include "rx_boost_control.h"
#include <cassert>
#include <iostream>
#include <vector>
#include "TCP_BACKEND"
#include "FRAME_PARSER"
void noteTransportFrameError(uint8_t) {}
static bool receiving = false, radioStandby = false;
static int mutations = 0;
static std::vector<int> replies;
static std::vector<TransportSource> destinations;
static RxBoostControl::State rxBoost;
void pump() { for (int i = 0; i < 12; ++i) TCPServer::loop(); }
bool isReceivingPacket() { return receiving; }
void sendFrame(uint8_t cmd, const uint8_t* payload, uint16_t len, TransportSource src) {
    replies.push_back(cmd);
    destinations.push_back(src);
    if (src == TransportSource::TCP) {
        uint8_t frame[MAX_FRAME_SIZE];
        uint16_t size = 0;
        TCPServer::buildFrame(frame, size, cmd, payload, len);
        TCPServer::write(frame, size);
    }
}
void sendError(uint8_t err, TransportSource src) { sendFrame(CMD_ERROR, &err, 1, src); }
namespace RFFrontEnd {
struct FemState { uint8_t capability = 3, value = 0; };
bool setFemState(uint8_t, uint8_t, bool, FemState&) { ++mutations; return true; }
FemState getFemState() { return {}; }
}
RxBoostControl::Result setRxBoostedGain(bool value, bool) {
    ++mutations; rxBoost.enabled = value; return {};
}
''' + queue + r'''
void processHostCommand(uint8_t cmd, const uint8_t* payload, uint16_t len, TransportSource src) {
    switch (cmd) {
''' + cases + r'''
    default: assert(false);
    }
}
std::shared_ptr<SocketStub> acceptClient() {
    incomingClient.s = std::make_shared<SocketStub>();
    auto socket = incomingClient.s;
    TCPServer::loop();
    return socket;
}
void request(const std::shared_ptr<SocketStub>& socket, uint8_t cmd,
             std::initializer_list<uint8_t> payload) {
    uint8_t frame[MAX_FRAME_SIZE]; uint16_t size = 0;
    std::vector<uint8_t> bytes(payload);
    TCPServer::buildFrame(frame, size, cmd, bytes.data(), bytes.size());
    socket->input.insert(socket->input.end(), frame, frame + size);
    TCPServer::loop();
    pump();
}
void authenticate(const std::shared_ptr<SocketStub>& socket) {
    request(socket, CMD_AUTH, {'t'});
    assert(TCPServer::isClientReady());
    socket->output.clear();
}
std::vector<int> wireCommands(const std::shared_ptr<SocketStub>& socket) {
    std::vector<int> commands;
    const auto& bytes = socket->output;
    for (size_t offset = 0; offset < bytes.size();) {
        assert(offset + 6 <= bytes.size() && bytes[offset] == PROTO_SYNC);
        const size_t length = bytes[offset + 2] | (size_t(bytes[offset + 3]) << 8);
        assert(offset + length + 6 <= bytes.size());
        const uint16_t crc = bytes[offset + length + 4] | (uint16_t(bytes[offset + length + 5]) << 8);
        assert(crc == crc16_ccitt(bytes.data() + offset + 1, length + 3));
        commands.push_back(bytes[offset + 1]);
        offset += length + 6;
    }
    return commands;
}
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; return 1; } } while (0)
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string scenario = argv[1];
    NETWORK_SETUP
    TCPServer::begin(5055, scenario == "tokenless_reconnect" ? "" : "t");
    auto original = acceptClient();
    authenticate(original);
    receiving = true;
    if (scenario == "purge_behind_usb")
        processHostCommand(CMD_SET_FEM_STATE, (const uint8_t*)"\1\1", 2, TransportSource::USB);
    const bool boost = scenario == "reconnect_boost";
    if (boost) request(original, CMD_SET_RX_BOOST, {1});
    else request(original, CMD_SET_FEM_STATE, {1, 1});
    CHECK(mutations == 0 && replies.empty());
    if (scenario == "pending_wifi_usb" || scenario == "pending_wifi_uart") {
        NETWORK_INVALIDATION_SCENARIO
    } else if (scenario == "normal" || scenario == "other_interface") {
        if (scenario == "other_interface") TCPServer::invalidateInterface(IPAddress(192,168,8,1));
        receiving = false;
        processRfRequests();
        pump();
        CHECK(mutations == 1 && replies.size() == 1 && !original->output.empty());
    } else if (scenario == "multiple_tcp_replies") {
        request(original, CMD_SET_RX_BOOST, {1});
        request(original, CMD_GET_RX_BOOST, {});
        request(original, CMD_SET_RX_BOOST, {2});
        receiving = false;
        processRfRequests();
#ifdef TEST_W5100S
        // The second setter must not touch RF until the first reply drains.
        CHECK(mutations == 1 && rfRequests.size() == 3);
#endif
        for (int i = 0; i < 12; ++i) { pump(); processRfRequests(); }
        pump();
        CHECK(TCPServer::isClientReady());
        CHECK(mutations == 2 && rfRequests.size() == 0);
        CHECK((replies == std::vector<int>{CMD_FEM_STATE_RESP, CMD_RX_BOOST_RESP,
                                         CMD_RX_BOOST_RESP, CMD_ERROR}));
        CHECK(wireCommands(original) == replies);
    } else if (scenario == "mixed_fifo") {
        processHostCommand(CMD_GET_RX_BOOST, nullptr, 0, TransportSource::UART);
        processHostCommand(CMD_GET_FEM_STATE, nullptr, 0, TransportSource::UART);
        const uint8_t invalidBoost = 2;
        processHostCommand(CMD_SET_RX_BOOST, &invalidBoost, 1, TransportSource::UART);
        CHECK(rfRequests.size() == 4 && replies.empty());
        receiving = false;
        processRfRequests();
        CHECK(mutations == 1);
        CHECK((replies == std::vector<int>{CMD_FEM_STATE_RESP, CMD_RX_BOOST_RESP,
                                         CMD_FEM_STATE_RESP, CMD_ERROR}));
        CHECK(destinations[2] == TransportSource::UART);
    } else {
        if (scenario == "full_stale") {
            request(original, CMD_GET_FEM_STATE, {});
            request(original, CMD_GET_RX_BOOST, {});
            request(original, CMD_SET_RX_BOOST, {2});
            CHECK(rfRequests.size() == 4);
        }
        if (scenario == "interface_loss") {
#ifdef TEST_W5100S
            usableIP = false; TCPServer::loop(); usableIP = true;
#else
            TCPServer::invalidateInterface(IPAddress(192,168,1,20));
#endif
        }
        else if (scenario == "restart") TCPServer::begin(5055, "t");
        else original->open = false;
        if (scenario == "disconnect_only") {
            processRfRequests(); // no transport-loop cleanup yet
            CHECK(rfRequests.size() == 0 && mutations == 0 && replies.empty());
        } else {
            auto replacement = acceptClient();
            if (scenario != "reconnect_unauthenticated") authenticate(replacement);
            if (scenario == "purge_behind_usb") {
                processRfRequests();
                CHECK(rfRequests.size() == 1); // live USB request still waits
            } else if (scenario == "full_stale") {
                request(replacement, CMD_GET_FEM_STATE, {});
                CHECK(rfRequests.size() == 0);
                CHECK((replies == std::vector<int>{CMD_FEM_STATE_RESP}));
            }
            if (scenario != "full_stale") {
                receiving = false;
                processRfRequests();
                CHECK(mutations == (scenario == "purge_behind_usb" ? 1 : 0));
                CHECK(replacement->output.empty());
                if (scenario == "purge_behind_usb") {
                    CHECK(replies.size() == 1 && destinations[0] == TransportSource::USB);
                } else CHECK(replies.empty());
            }
        }
    }
    std::cout << scenario << ": PASS\n";
}
'''
    if backend == "w5100s":
        transport = (FIRMWARE / "src/w5100s_ethernet_transport.cpp").read_text()
        tcp = transport[transport.index("namespace TCPServer {"):transport.rindex("#endif")]
        frame = function(transport, "static void buildFrame(")
        code = code.replace('#include "TCP_BACKEND"',
                            '#include "w5100s_ethernet_transport.h"\n#include "frame_parser.h"\n'
                            '#include "net_filter.h"\n#include "w5100.h"\n#define TEST_W5100S 1\n'
                            '#define OPENHOP_ETH_TCP_PORT 5055\n'
                            'extern void processHostCommand(uint8_t, const uint8_t*, uint16_t, TransportSource);\n'
                            'extern void noteTransportFrameError(uint8_t);\n' + frame + '\n' + tcp
                            + '\nnamespace TCPServer { using ::buildFrame; }')
        code = code.replace('TCPServer::invalidateInterface(IPAddress(192,168,8,1));', '(void)0;')
        code = code.replace('NETWORK_SETUP', '').replace('NETWORK_INVALIDATION_SCENARIO', 'assert(false);')
    else:
        code = code.replace('#include "TCP_BACKEND"',
                            '#include "config_portal.h"\n'
                            'namespace ConfigPortal { void begin(){} void end(){} void loop(){} bool isActive(){return false;} }\n'
                            '#include "' + str(FIRMWARE / 'src/wifi_manager.cpp') + '"\n'
                            '#include "TCP_BACKEND"')
        loop = main[main.index('void loop() {'):]
        start = loop.index('        handleLoRaRx();\n    }\n') + len('        handleLoRaRx();\n    }\n')
        service = loop[start:loop.index('    if (tcpStarted) TCPServer::loop();')]
        service = '''
FrameParser serialParser, uartParser;
bool uartEnabled = true;
struct InputStream {
    std::deque<uint8_t> input;
    int available(){return input.size();}
    int read(){int b=input.front(); input.pop_front(); return b;}
} PROTO_UART;
void onSerialFrameOk(uint8_t cmd, const uint8_t* p, uint16_t n, TransportSource src) { processHostCommand(cmd,p,n,src); }
void onSerialFrameErr(uint8_t,TransportSource) {}
void serviceHostInputs() {
''' + service + '\n}\n'
        code = code.replace('std::shared_ptr<SocketStub> acceptClient()', service + '\nstd::shared_ptr<SocketStub> acceptClient()')
        code = code.replace('NETWORK_SETUP', '''
    const bool wifiScenario = scenario == "pending_wifi_usb" || scenario == "pending_wifi_uart";
    if (wifiScenario) {
        WifiManager::begin(); WiFi.statusValue = WL_CONNECTED;
        WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP); WifiManager::loop();
    }
''')
        code = code.replace('NETWORK_INVALIDATION_SCENARIO', '''
        // Socket remains connected while the real Wi-Fi manager holds a pending down event.
        WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        receiving = false;
        uint8_t frame[MAX_FRAME_SIZE]; uint16_t size = 0;
        TCPServer::buildFrame(frame, size, CMD_GET_FEM_STATE, nullptr, 0);
        auto& input = scenario == "pending_wifi_usb" ? Serial.input : PROTO_UART.input;
        input.insert(input.end(), frame, frame + size);
        serviceHostInputs();
        CHECK(mutations == 0 && rfRequests.size() == 0);
        CHECK(replies.size() == 1 && destinations[0] != TransportSource::TCP);
        CHECK(original->output.empty());
''')
    return code.replace('TCP_BACKEND', str(FIRMWARE / 'src/tcp_server.cpp')).replace(
        'FRAME_PARSER', str(FIRMWARE / 'src/frame_parser.cpp'))


class RequestConnectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="openhop-rf-connections-")
        work = Path(cls.temp.name)
        stubs = work / "stubs"
        shutil.copytree(FIRMWARE / "tools/wifi_recovery_stubs", stubs)
        wifi = (stubs / "WiFi.h").read_text()
        wifi = wifi.replace("std::deque<uint8_t> input;", "std::deque<uint8_t> input; std::vector<uint8_t> output;")
        wifi = wifi.replace("#include <functional>", "#include <functional>\n#include <vector>")
        wifi = wifi.replace("void setNoDelay(bool)", "uint8_t getSocketNumber(){return s && s->open ? 0 : 4;} void setNoDelay(bool)", 1)
        wifi = wifi.replace("size_t write(const uint8_t*,size_t n){return n;}",
                            "size_t write(const uint8_t* p,size_t n){s->output.insert(s->output.end(),p,p+n);return n;}")
        (stubs / "WiFi.h").write_text(wifi)
        arduino = (stubs / "Arduino.h").read_text()
        arduino = arduino.replace('struct SerialStub {',
                                 'struct SerialStub { std::deque<uint8_t> input; '
                                 'int available(){return input.size();} '
                                 'int read(){int b=input.front(); input.pop_front(); return b;} ')
        (stubs / "Arduino.h").write_text(arduino)
        board = (stubs / "board_config.h").read_text()
        (stubs / "board_config.h").write_text(board.replace('struct BoardStub {', 'struct BoardStub { bool has_wifi = true;'))
        cls.binaries = {}
        for backend in ("esp32", "w5100s"):
            source = work / (backend + ".cpp")
            source.write_text(harness(backend))
            binary = source.with_suffix("")
            flags = ["-DARDUINO_ARCH_ESP32=1"] if backend == "esp32" else []
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags,
                            "-I" + str(stubs), "-I" + str(FIRMWARE / "include"),
                            "-I" + str(FIRMWARE / "tools/rf_request_connection_stubs"),
                            str(source), "-o", str(binary)], check=True)
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
             "pending_wifi_usb", "pending_wifi_uart"):
    setattr(RequestConnectionTests, "test_" + name,
            lambda self, name=name: self.scenario(name))


if __name__ == "__main__":
    unittest.main(verbosity=2)
