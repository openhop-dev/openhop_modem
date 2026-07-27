#include "webui_shared.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace WebUiShared;

namespace {

void assertContains(const std::string& text, const std::string& expected) {
    if (text.find(expected) == std::string::npos) {
        std::cerr << "missing expected substring: " << expected << "\nfrom: " << text << "\n";
        assert(false);
    }
}

void assertNotContains(const std::string& text, const std::string& unexpected) {
    if (text.find(unexpected) != std::string::npos) {
        std::cerr << "unexpected substring: " << unexpected << "\nin: " << text << "\n";
        assert(false);
    }
}

Model makeEspModel() {
    Model model;
    model.board = "Heltec WiFi LoRa 32 V3";
    model.firmware = "1.2.3";
    model.hostname = "heltec-test";
    model.capabilities.wifi = true;
    model.capabilities.mdns = true;
    model.capabilities.wifiReset = true;
    model.capabilities.radio = true;
    model.capabilities.updateAvailable = true;
    model.capabilities.httpFirmwareUpload = true;
    model.capabilities.writableManagement = true;
    model.capabilities.exposeTcpToken = true;
    model.network.interfaceName = "Wi-Fi";
    model.network.live = true;
    model.network.currentIp = "192.168.1.42";
    model.network.subnet = "255.255.255.0";
    model.network.gateway = "192.168.1.1";
    model.network.dns1 = "1.1.1.1";
    model.network.dns2 = "8.8.8.8";
    model.network.hasWifiRssi = true;
    model.network.wifiRssiDbm = -51;
    model.config.tcpPort = 5055;
    model.radio.available = true;
    model.radio.state = "RX/Idle";
    model.radio.frequencyHz = 869618000;
    model.radio.bandwidthHz = 62500;
    model.radio.spreadingFactor = 8;
    model.radio.codingRate = 8;
    model.radio.txPowerDbm = 22;
    model.radio.syncword = 0x12;
    model.radio.preambleLength = 16;
    return model;
}

void testEscapingHelpers() {
    assert(htmlEscape("<&>\"'") == "&lt;&amp;&gt;&quot;&#39;");
    assert(jsonEscape("\"\\\n\r\t\b\f\x01") == "\\\"\\\\\\n\\r\\t\\b\\f\\u0001");
}

void testSetupPageEscapesUserAndScanValues() {
    SetupModel setup;
    setup.savedSsid = "A'&<";
    setup.password = "p'\"<&";
    setup.hostname = "h'\"<&";
    setup.tcpPort = 5055;
    setup.networks.push_back({"A'&<", -42});

    const std::string html = renderSetupPage(setup);
    assertContains(html, "value='A&#39;&amp;&lt;' selected");
    assertContains(html, "value='p&#39;&quot;&lt;&amp;'");
    assertContains(html, "value='h&#39;&quot;&lt;&amp;'");
    assertNotContains(html, "value='A'&<'");
}

void testJsonUsesNullForUnavailableValuesAndEscapesStrings() {
    Model model = makeEspModel();
    model.board = "board\"\\\n";
    model.battery.available = true;
    model.battery.voltageValid = false;
    model.connectedClientIp.clear();
    model.network.subnet.clear();
    model.uptimeSec = 90061;

    const std::string system = renderSystemJson(model);
    assertContains(system, "\"board\":\"board\\\"\\\\\\n\"");
    assertContains(system, "\"connected_client_ip\":null");
    assertContains(system, "\"battery_voltage_mv\":null");
    assertContains(system, "\"uptime\":\"1d 01:01:01\"");

    const std::string network = renderNetworkJson(model);
    assertContains(network, "\"subnet\":null");
}

void testEspRootPreservesCurrentControls() {
    Model model = makeEspModel();
    model.capabilities.wifiAntennaSelection = true;
    model.capabilities.gps = true;
    model.gps.available = true;

    const std::string html = renderRootPage(model);
    assertContains(html, "firmware.bin");
    assertContains(html, "action='/update'");
    assertContains(html, "Wi-Fi Setup Mode");
    assertContains(html, "wifi_ant_ext");
    assertContains(html, "<summary>GPS</summary>");
    assertContains(html, "heltec-test.local");
    assertNotContains(html, "Ethernet OTA unavailable");
}

void testEthernetRootHasOnlyEthernetControlsAndHonestOtaStatus() {
    Model model = makeEspModel();
    model.board = "RAK4631 WisMesh Ethernet";
    model.hostname = "rak-gateway";
    model.capabilities = {};
    model.capabilities.ethernet = true;
    model.capabilities.radio = true;
    model.capabilities.battery = true;
    model.capabilities.gps = false;
    model.capabilities.updateAvailable = false;
    model.capabilities.httpFirmwareUpload = false;
    model.capabilities.writableManagement = true;
    model.capabilities.bleDfu = true;
    model.updateUnavailableReason =
        "Ethernet firmware update is unavailable until the installed bootloader handoff is verified.";
    model.network.interfaceName = "Ethernet";
    model.network.linkState = "up";
    model.network.mac = "AA:BB:CC:DD:EE:FF";
    model.network.tcpStatus = "connected";
    model.network.currentIp = "10.0.0.20";
    model.network.subnet = "255.255.255.0";
    model.network.gateway = "10.0.0.1";
    model.network.dns1 = "10.0.0.1";
    model.network.dns2.clear();
    model.config.useStaticIp = false;
    model.battery.available = true;
    model.battery.voltageValid = false;

    const std::string html = renderRootPage(model);
    assertContains(html, "Ethernet OTA unavailable");
    assertContains(html, model.updateUnavailableReason);
    assertContains(html, "DHCP mode");
    assertContains(html, "10.0.0.20");
    assertContains(html, "255.255.255.0");
    assertContains(html, "10.0.0.1");
    assertContains(html, "AA:BB:CC:DD:EE:FF");
    assertContains(html, "rak-gateway");
    assertContains(html, "connected");
    assertContains(html, "Battery</span><span class='v'>unknown");
    assertNotContains(html, "Wi-Fi Setup Mode");
    assertNotContains(html, "wifi_ant_ext");
    assertNotContains(html, "Wi-Fi signal");
    assertNotContains(html, "rak-gateway.local");
    assertNotContains(html, "type='file'");
    assertNotContains(html, "<summary>GPS</summary>");
    assertContains(html, "action='/hostname'");
    assertContains(html, "action='/network'");
    assertContains(html, "name='port'");
    assertContains(html, "value='5055'");
    assertContains(html, "action='/token'");
    assertContains(html, "action='/auth'");
    assertContains(html, "action='/reboot'");
    assertContains(html, "action='/dfu/ble'");
    assertContains(html, "name is defined by the installed bootloader");
    assertContains(html, "Ethernet and port 5055 will disconnect");
    assertContains(html, "return confirm(");

    model.config.tcpToken = "do-not-export";
    model.config.tcpTokenSet = true;
    const std::string config = renderConfigJson(model);
    assertNotContains(config, "do-not-export");
    assertNotContains(config, "\"tcp_token\"");
    assertContains(config, "\"tcp_token_set\":true");
}

void testCapabilitiesControlOptionalSectionsAndFields() {
    Model model = makeEspModel();
    model.capabilities.radio = false;
    model.radio.available = false;
    model.capabilities.battery = false;
    model.capabilities.gps = false;
    model.capabilities.heltecV43Controls = false;

    const std::string stats = renderStatsPage(model);
    assertNotContains(stats, "<h3>Radio</h3>");
    assertNotContains(stats, "Battery</span>");
    assertNotContains(stats, "GPS fix");

    const std::string config = renderConfigJson(model);
    assertNotContains(config, "wifi_external_antenna");
    assertNotContains(config, "heltec_v43_external_lna_enabled");
    assertContains(config, "\"gps_available\":false");
}

}  // namespace

int main() {
    testEscapingHelpers();
    testSetupPageEscapesUserAndScanValues();
    testJsonUsesNullForUnavailableValuesAndEscapesStrings();
    testEspRootPreservesCurrentControls();
    testEthernetRootHasOnlyEthernetControlsAndHonestOtaStatus();
    testCapabilitiesControlOptionalSectionsAndFields();
    std::cout << "webui shared renderer tests passed\n";
    return 0;
}
