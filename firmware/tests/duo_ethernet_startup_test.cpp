#include <Arduino.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <ETH.h>
#include "protocol.h"
#include "radio_layout.h"
#include "runtime_stats.h"
#include "ethernet_manager.h"
#include <cassert>
#include <iostream>

void setup();
void loop();
namespace WifiManager { extern int beginCalls; }
namespace OTAManager { extern int beginCalls, validMarks; }

static std::vector<uint8_t> frame(uint8_t cmd, const uint8_t* payload = nullptr, uint16_t len = 0) {
    std::vector<uint8_t> bytes{PROTO_SYNC, cmd, uint8_t(len), uint8_t(len >> 8)};
    if (len) bytes.insert(bytes.end(), payload, payload + len);
    const auto crc = crc16_ccitt(bytes.data() + 1, bytes.size() - 1);
    bytes.push_back(crc); bytes.push_back(crc >> 8);
    return bytes;
}
static bool has(const std::vector<uint8_t>& bytes, uint8_t command) {
    for (size_t offset = 0; offset + 6 <= bytes.size();) {
        assert(bytes[offset] == PROTO_SYNC);
        const size_t len = bytes[offset + 2] | (bytes[offset + 3] << 8);
        assert(offset + len + 6 <= bytes.size());
        if (bytes[offset + 1] == command) return true;
        offset += len + 6;
    }
    return false;
}
static void pump(int count = 4) { while (count--) loop(); }
static void endpoints(bool noHostFrames = false, bool managementOnly = false) {
    assert(EthernetManager::hasIP() && ETH.active && ETH.ends == 0 && ETH.begins == 1);
    assert(radioListeners[5055].listening && radioListeners[5056].listening);
    assert(OTAManager::beginCalls == 1 && WifiManager::beginCalls == 0);
    RuntimeStats::RadioSnapshot first{}, second{};
    assert(RuntimeStats::captureRadio(0, first) && RuntimeStats::captureRadio(1, second));
    assert(first.ready == (first.enabled && !beginErrors[20]) && second.ready == second.enabled);
    assert(!first.txReady && !second.txReady);
    if (noHostFrames) {
        assert(OTAManager::validMarks == 0);
        clockMs += 60000U; pump();
        assert(OTAManager::validMarks == 0); // Never bypass the healthy-time gate.
        clockMs += 60001U; pump();
        assert(OTAManager::validMarks == (managementOnly ? 1 : 0) &&
               "both-disabled management-only OTA must become valid without modem traffic");
        for (const auto& entry : chips) assert(entry.second->txStarts == 0);
        return;
    }
    auto one = connectRadio(5055), two = connectRadio(5056); pump();
    static const uint8_t token[] = {'s','e','c','r','e','t'};
    for (auto socket : {one, two}) {
        auto auth = frame(CMD_AUTH, token, sizeof(token));
        socket->input.insert(socket->input.end(), auth.begin(), auth.end());
    }
    pump(); assert(has(one->output, CMD_AUTH_OK) && has(two->output, CMD_AUTH_OK));
    one->output.clear(); two->output.clear();
    for (auto socket : {one, two}) {
        auto ping = frame(CMD_PING); socket->input.insert(socket->input.end(), ping.begin(), ping.end());
    }
    pump(); assert(has(one->output, CMD_PONG) && has(two->output, CMD_PONG));
    clockMs += 120001U; pump();
    assert(OTAManager::validMarks == 1); // Actual production rollback check, no radio readiness gate.
    for (const auto& entry : chips) assert(entry.second->txStarts == 0);
}
int main(int argc, char** argv) {
    std::cout.setf(std::ios::unitbuf);
    assert(argc == 2); const std::string scenario = argv[1];
    preferences["enabled"]={'c',(scenario=="both_off"||scenario=="both_off_http_only")?0U:(scenario=="rf1_only"||scenario=="failed_rf1")?1U:3U};
    if (scenario=="failed_rf1") beginErrors[20]=-1;
    ETH.cable = scenario != "late_link" && scenario != "wifi_fallback";
    ETH.leaseReady = scenario != "late_dhcp";
    setup();
    assert(ETH.begins == 1);
    if (scenario == "wifi_fallback") {
        assert(BOARD.has_wifi && !ETH.active && ETH.ends == 1 && WifiManager::beginCalls == 1);
        std::cout << "PASS existing Wi-Fi-capable board retains no-link fallback\n";
        return 0;
    }
    assert(!BOARD.has_wifi && RadioLayout::count() == 2);
    if (scenario == "late_link" || scenario == "late_dhcp") {
        assert(!EthernetManager::hasIP() && radioListeners.empty());
        const auto ping = frame(CMD_PING); Serial.rx.insert(Serial.rx.end(), ping.begin(), ping.end());
        pump(); assert(has(Serial.tx, CMD_PONG));
        ETH.cable = true; ETH.leaseReady = true;
        for (int i = 0; i < 60; ++i) { clockMs += 1000; loop(); }
    }
    endpoints(scenario=="both_off_http_only" || scenario=="enabled_no_host",
              scenario=="both_off_http_only");
    std::cout << "PASS " << scenario << ": real main/EthernetManager, both endpoints/auth/PONG, RX-only radios\n";
}
