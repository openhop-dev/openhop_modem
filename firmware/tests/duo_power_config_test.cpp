#include <Arduino.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <WiFi.h>
#include "protocol.h"
#include "radio_layout.h"
#include "runtime_stats.h"
#include "tcp_server.h"
#include <algorithm>
#include <cassert>
#include <iostream>

void setup();
void loop();
void processHostCommand(uint8_t, const uint8_t*, uint16_t, const ResponseRoute&);
namespace OTAManager { extern int ticks; }

using Socket = std::shared_ptr<RadioSocketStub>;
static Socket sockets[2];
static SX1262* radios[2];

struct Frame { uint8_t command; std::vector<uint8_t> payload; };
static std::vector<Frame> decode(const std::vector<uint8_t>& bytes) {
    std::vector<Frame> result;
    for (size_t offset = 0; offset < bytes.size();) {
        assert(offset + 6 <= bytes.size() && bytes[offset] == PROTO_SYNC);
        const size_t length = bytes[offset + 2] | (bytes[offset + 3] << 8);
        assert(offset + length + 6 <= bytes.size());
        const uint16_t crc = bytes[offset + length + 4] | (bytes[offset + length + 5] << 8);
        assert(crc16_ccitt(bytes.data() + offset + 1, length + 3) == crc);
        result.push_back({bytes[offset + 1], {bytes.begin() + offset + 4, bytes.begin() + offset + 4 + length}});
        offset += length + 6;
    }
    return result;
}
static std::vector<uint8_t> frame(uint8_t command, const uint8_t* payload, uint16_t length) {
    std::vector<uint8_t> bytes{PROTO_SYNC, command, uint8_t(length), uint8_t(length >> 8)};
    if (length) bytes.insert(bytes.end(), payload, payload + length);
    const auto crc = crc16_ccitt(bytes.data() + 1, bytes.size() - 1);
    bytes.push_back(crc); bytes.push_back(crc >> 8);
    return bytes;
}
static void pump(int count = 3) { while (count--) loop(); }
static void clear() { for (auto& socket : sockets) socket->output.clear(); Serial.tx.clear(); }
static void send(size_t id, uint8_t command, const uint8_t* payload = nullptr, uint16_t length = 0) {
    const auto bytes = frame(command, payload, length);
    sockets[id]->input.insert(sockets[id]->input.end(), bytes.begin(), bytes.end());
    pump();
}
static Frame only(size_t id, uint8_t command) {
    const auto frames = decode(sockets[id]->output);
    if (frames.size() != 1 || frames[0].command != command) {
        std::cerr << "expected one command " << unsigned(command) << ", got";
        for (const auto& reply : frames) std::cerr << " " << unsigned(reply.command);
        std::cerr << " on RF" << id + 1 << "\n";
    }
    assert(frames.size() == 1 && frames[0].command == command);
    return frames[0];
}
static void error(size_t id, uint8_t code) {
    assert(only(id, CMD_ERROR).payload == std::vector<uint8_t>{code});
    assert(sockets[1 - id]->output.empty());
}
static RuntimeStats::RadioSnapshot snapshot(size_t id) {
    RuntimeStats::RadioSnapshot result;
    assert(RuntimeStats::captureRadio(id, result));
    return result;
}
static bool sameConfig(const RadioConfig& a, const RadioConfig& b) { return memcmp(&a, &b, sizeof(a)) == 0; }
static void peerUnchanged(size_t id, const RuntimeStats::RadioSnapshot& before) {
    const auto after = snapshot(id);
    assert(sameConfig(before.radio, after.radio));
    assert(before.ready == after.ready && before.txReady == after.txReady);
    assert(before.status.tx_count == after.status.tx_count && before.status.rx_count == after.status.rx_count);
}
static RadioConfig config(size_t id, int8_t power) {
    return RadioConfig{uint32_t(915000000 + id * 100000), 125000, 7, 5, power, 0x12, 16};
}
static void configure(size_t id, int8_t power) {
    clear(); const auto cfg = config(id, power);
    send(id, CMD_SET_CONFIG, reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg));
    const auto reply = only(id, CMD_CONFIG_RESP);
    assert(reply.payload.size() == sizeof(cfg));
    RadioConfig echoed; memcpy(&echoed, reply.payload.data(), sizeof(echoed));
    assert(sameConfig(cfg, echoed) && sameConfig(snapshot(id).radio, cfg));
    assert(radios[id]->power == power && snapshot(id).ready && snapshot(id).txReady);
    assert(sockets[1 - id]->output.empty() && Serial.tx.empty());
}
static void txBlocked(size_t id) {
    clear(); const uint8_t payload[] = {0x44, 0x55};
    const int starts = radios[id]->txStarts;
    send(id, CMD_TX_REQUEST, payload, sizeof(payload));
    error(id, ERR_INVALID_CONFIG);
    assert(radios[id]->txStarts == starts && !snapshot(id).txReady);
}
static void txCompletes(size_t id) {
    clear(); const uint8_t payload[] = {0x44, 0x55};
    const int starts = radios[id]->txStarts;
    send(id, CMD_TX_REQUEST, payload, sizeof(payload));
    assert(radios[id]->txStarts == starts + 1 && snapshot(id).busy);
    assert(sockets[id]->output.empty());
    radios[id]->edge(RADIOLIB_SX126X_IRQ_TX_DONE); pump();
    assert(only(id, CMD_TX_DONE).payload.size() == 4);
    assert(!snapshot(id).busy && sockets[1 - id]->output.empty());
}
static void authenticate(size_t id) {
    static const uint8_t token[] = {'s','e','c','r','e','t'};
    clear(); send(id, CMD_AUTH, token, sizeof(token));
    only(id, CMD_AUTH_OK); assert(TCPServer::isClientReady(id)); clear();
}
static void start(const std::string& scenario) {
    preferences["enabled"]={'c',3}; // Explicit dual operation, not first-use policy.
    const bool secondary = scenario.find("_rf2") != std::string::npos;
    const int failingNss = secondary ? 15 : 20;
    if (scenario == "boot_recovery" || scenario == "boot_recovery_rf2") initialFailures[failingNss]["frequency"] = {-17};
    if (scenario == "init_failure" || scenario == "init_failure_rf2") beginErrors[failingNss] = -17;
    if (scenario == "options_failure" || scenario == "options_failure_rf2") initialFailures[failingNss]["dio2"] = {-17};
    setup();
    assert(RadioLayout::count() == 2 && RuntimeStats::radioCount() == 2 && chips.size() == 2);
    assert(radioListeners[5055].listening && radioListeners[5056].listening);
    for (size_t id = 0; id < 2; ++id) {
        radios[id] = chips.at(RadioLayout::at(id).hardware->pin_lora_nss);
        assert(radios[id]->txStarts == 0 && !snapshot(id).txReady);
        sockets[id] = connectRadio(5055 + id);
    }
    pump();
    assert(!TCPServer::isClientReady(0) && !TCPServer::isClientReady(1));
    // Ethernet token must protect the real dispatcher, not merely its status.
    clear(); const auto cfg = config(0, 4); const auto before = radios[0]->calls.size();
    send(0, CMD_SET_CONFIG, reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg));
    error(0, ERR_UNAUTHORIZED);
    assert(radios[0]->calls.size() == before && !snapshot(0).txReady);
    sockets[0] = connectRadio(5055); pump();
    authenticate(0); authenticate(1);
    assert(radios[0] != radios[1] && radios[0]->mod != radios[1]->mod);
    assert(radios[0]->mod->spi != radios[1]->mod->spi);
    assert(radios[0]->mod->spi->bus == FSPI && radios[1]->mod->spi->bus == HSPI);
    assert(radios[0]->mod->spi->sck == 21 && radios[0]->mod->spi->miso == 23 && radios[0]->mod->spi->mosi == 22);
    assert(radios[1]->mod->spi->sck == 18 && radios[1]->mod->spi->miso == 16 && radios[1]->mod->spi->mosi == 17);
    assert(gpio.at(27) == HIGH && gpio.at(19) == HIGH);
}
static void supported() {
    for (size_t id = 0; id < 2; ++id) {
        for (int8_t power : {1, 4, 5, 22}) {
            const auto peer = snapshot(1 - id); const auto peerCalls = radios[1 - id]->calls.size();
            bool checked = false;
            radios[id]->before = [&](const std::string& name) {
                if (name == "standby" || name == "power") {
                    assert(!snapshot(id).txReady && !snapshot(id).ready);
                    checked = true;
                }
            };
            configure(id, power); radios[id]->before = {};
            assert(checked && radios[1 - id]->calls.size() == peerCalls);
            peerUnchanged(1 - id, peer); txCompletes(id); peerUnchanged(1 - id, peer);
            clear(); send(id, CMD_GET_CONFIG); const auto reply = only(id, CMD_CONFIG_RESP);
            RadioConfig echoed; memcpy(&echoed, reply.payload.data(), sizeof(echoed));
            assert(echoed.power_dbm == power);
        }
    }
}
static void invalidPower() {
    configure(0, 4); configure(1, 5);
    for (size_t id = 0; id < 2; ++id) {
        for (int8_t invalid : {23, -10, 127, -128}) {
            configure(id, 4);
            const auto good = snapshot(id); const auto peer = snapshot(1 - id);
            const auto calls = radios[id]->calls.size();
            clear(); auto cfg = config(id, invalid);
            send(id, CMD_SET_CONFIG, reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg));
            error(id, ERR_INVALID_CONFIG);
            assert(radios[id]->calls.size() == calls && sameConfig(snapshot(id).radio, good.radio));
            peerUnchanged(1 - id, peer); txBlocked(id);
            clear(); send(id, CMD_RADIO_RESUME); only(id, CMD_RADIO_RESUME_RESP); txBlocked(id);
            peerUnchanged(1 - id, peer);
        }
    }
}
static void setterFailure() {
    configure(0, 4); configure(1, 5);
    for (size_t id = 0; id < 2; ++id) {
        const auto peer = snapshot(1 - id); const auto good = snapshot(id);
        radios[id]->failures["power"] = {-17};
        clear(); auto cfg = config(id, 22);
        send(id, CMD_SET_CONFIG, reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg));
        error(id, ERR_INVALID_CONFIG);
        assert(!snapshot(id).ready && !snapshot(id).txReady && sameConfig(good.radio, snapshot(id).radio));
        peerUnchanged(1 - id, peer); txBlocked(id);
        const auto starts = radios[id]->rxStarts;
        clear(); send(id, CMD_RX_START); error(id, ERR_RADIO_FAULT);
        assert(radios[id]->rxStarts == starts && !snapshot(id).ready);
        txCompletes(1 - id);
        clear(); send(id, CMD_PING); only(id, CMD_PONG); assert(OTAManager::ticks > 0);
        clear(); send(id, CMD_RADIO_RESUME); only(id, CMD_RADIO_RESUME_RESP);
        assert(snapshot(id).ready); txBlocked(id);
        configure(id, 22); txCompletes(id);
    }
}
static void malformed() {
    configure(0, 4); configure(1, 5);
    for (size_t id = 0; id < 2; ++id) {
        const auto peer = snapshot(1 - id); const auto good = snapshot(id);
        const auto calls = radios[id]->calls.size();
        clear(); const auto cfg = config(id, 22);
        send(id, CMD_SET_CONFIG, reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg) - 1);
        error(id, ERR_INVALID_CONFIG);
        assert(radios[id]->calls.size() == calls && sameConfig(snapshot(id).radio, good.radio));
        txBlocked(id); clear(); send(id, CMD_RADIO_RESUME); only(id, CMD_RADIO_RESUME_RESP); txBlocked(id);
        peerUnchanged(1 - id, peer); configure(id, 22);
    }
}
static void staleGeneration() {
    configure(0, 4); configure(1, 5);
    const ResponseRoute old{TransportSource::TCP, 0, TCPServer::connectionGeneration()};
    const auto before = snapshot(0); const auto peer = snapshot(1); const auto cfg = config(0, 22);
    // A partial old parser frame must not be completed in its replacement.
    auto partial = frame(CMD_SET_CONFIG, reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg));
    sockets[0]->input.insert(sockets[0]->input.end(), partial.begin(), partial.end() - 1); pump();
    sockets[0]->open = false; pump(); sockets[0] = connectRadio(5055); pump();
    const auto calls = radios[0]->calls.size(); clear();
    processHostCommand(CMD_SET_CONFIG, reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg), old);
    assert(radios[0]->calls.size() == calls && sameConfig(before.radio, snapshot(0).radio));
    authenticate(0); assert(TCPServer::connectionGeneration() != old.generation);
    clear(); processHostCommand(CMD_SET_CONFIG, reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg), old);
    assert(radios[0]->calls.size() == calls && sameConfig(before.radio, snapshot(0).radio));
    assert(sockets[0]->output.empty() && sockets[1]->output.empty());
    // Auto-CAD pending TX must not transmit after ownership expires.
    uint8_t enabled = 1; send(0, CMD_SET_AUTO_CAD, &enabled, 1); clear();
    const uint8_t payload = 0x42; send(0, CMD_TX_REQUEST, &payload, 1);
    assert(snapshot(0).busy); const auto starts = radios[0]->txStarts;
    sockets[0]->open = false; pump(); sockets[0] = connectRadio(5055); pump();
    radios[0]->edge(RADIOLIB_SX126X_IRQ_CAD_DONE); pump();
    assert(radios[0]->txStarts == starts && sockets[0]->output.empty());
    peerUnchanged(1, peer); authenticate(0);
}
static void recovery(bool permanentFailure, size_t failed = 0) {
    const size_t healthy = 1 - failed;
    assert(!snapshot(failed).ready && snapshot(healthy).ready);
    clear(); send(failed, CMD_PING); only(failed, CMD_PONG); assert(OTAManager::ticks > 0);
    configure(healthy, 5); txCompletes(healthy);
    if (permanentFailure) {
        clear(); const auto cfg = config(failed, 4);
        send(failed, CMD_SET_CONFIG, reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg));
        error(failed, ERR_NO_RADIO);
        assert(!snapshot(failed).ready && !snapshot(failed).txReady && radios[failed]->txStarts == 0);
        return;
    }
    clear(); send(failed, CMD_RADIO_RESUME); only(failed, CMD_RADIO_RESUME_RESP);
    assert(snapshot(failed).ready); txBlocked(failed);
    clear(); send(failed, CMD_CAD_REQUEST); clockMs += 501; pump(); error(failed, ERR_CAD_FAILED); txBlocked(failed);
    configure(failed, 4); radios[failed]->receive({0xa5}); pump();
    assert(snapshot(failed).status.rx_count == 1); txCompletes(failed);
}
static void noAutomaticBridge() {
    configure(0, 4); configure(1, 5);
    for (size_t id = 0; id < 2; ++id) {
        clear(); const int firstStarts = radios[0]->txStarts, secondStarts = radios[1]->txStarts;
        const auto before = snapshot(id); const auto peer = snapshot(1 - id);
        radios[id]->receive({uint8_t(0xa0 + id), 0x5a}); pump();
        const auto event = only(id, CMD_RX_PACKET);
        assert(event.payload.size() == 8 && event.payload[6] == uint8_t(0xa0 + id) && event.payload[7] == 0x5a);
        assert(sockets[1 - id]->output.empty());
        assert(radios[0]->txStarts == firstStarts && radios[1]->txStarts == secondStarts);
        assert(snapshot(id).status.rx_count == before.status.rx_count + 1);
        peerUnchanged(1 - id, peer);
        if (id == 1) assert(Serial.tx.empty());
    }
}
static void bootBand() {
    for (size_t id = 0; id < 2; ++id) {
        assert(radios[id]->beginFrequency == 915.0f);
        assert(snapshot(id).radio.freq_hz == 915000000 && radios[id]->freq == 915.0f);
        assert(snapshot(id).ready && !snapshot(id).txReady && radios[id]->txStarts == 0);
        clear(); send(id, CMD_RADIO_STANDBY); only(id, CMD_RADIO_STANDBY_RESP);
        clear(); send(id, CMD_RADIO_RESUME); only(id, CMD_RADIO_RESUME_RESP); txBlocked(id);
        clear(); send(id, CMD_CAD_REQUEST); clockMs += 501; pump(); error(id, ERR_CAD_FAILED); txBlocked(id);
    }
}
static void identity() {
    const std::string expected = "v1.4.5-ethermesh_duo";
    assert(RuntimeStats::capture().firmwareVersion == expected.c_str());
    for (size_t id = 0; id < 2; ++id) {
        clear(); send(id, CMD_GET_VERSION); const auto reply = only(id, CMD_VERSION_RESP);
        assert(std::string(reply.payload.begin(), reply.payload.end()) == expected);
    }
}
int main(int argc, char** argv) {
    std::cout.setf(std::ios::unitbuf);
    assert(argc == 2); const std::string scenario = argv[1]; start(scenario);
    if (scenario == "supported") supported();
    else if (scenario == "invalid_power") invalidPower();
    else if (scenario == "setter_failure") setterFailure();
    else if (scenario == "malformed") malformed();
    else if (scenario == "stale_generation") staleGeneration();
    else if (scenario == "boot_recovery") recovery(false);
    else if (scenario == "boot_recovery_rf2") recovery(false, 1);
    else if (scenario == "init_failure") recovery(true);
    else if (scenario == "init_failure_rf2") recovery(true, 1);
    else if (scenario == "options_failure") recovery(true);
    else if (scenario == "options_failure_rf2") recovery(true, 1);
    else if (scenario == "no_auto_bridge") noAutomaticBridge();
    else if (scenario == "boot_band") bootBand();
    else if (scenario == "identity") identity();
    else assert(false);
    std::cout << "PASS " << scenario << " (production main/TCP/parser/per-owner config/readiness)\n";
}
