#include "webui_shared.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <string>
using namespace WebUiShared;

int main(int argc, char** argv) {
    Model m;
    m.board="Test dual radio";
    m.capabilities.writableManagement=true;
    m.capabilities.radioSelection=true;
    m.capabilities.httpFirmwareUpload=true;
    m.capabilities.updateAvailable=true;
    m.config.tcpPort=5055;
    m.config.tcpPortReadOnly=true;
    m.config.tcpToken="secret-test-fixture";
    m.connectedClientIp="198.51.100.99"; // Legacy primary projection must not summarize two radios.
    RadioEndpointModel first;
    first.id=0; first.label="RF1"; first.tcpPort=5055;
    first.ready=true; first.txReady=true;
    first.connectedClientIp="192.0.2.10";
    first.radio.available=true; first.radio.frequencyHz=910525000;
    first.counters.rxPackets=11;
    RadioEndpointModel second=first;
    second.id=1; second.label="RF2<&\""; second.tcpPort=5056;
    second.ready=false; second.txReady=false;
    second.connectedClientIp="192.0.2.20";
    second.radio.frequencyHz=915000000;
    second.counters.rxPackets=22;
    m.radios={first,second};
    const auto html=renderRootPage(m);
    if (argc>1) std::ofstream(argv[1]) << html;
    assert(html.find("action='/radios'")!=std::string::npos);
    assert(html.find("name='rf0'")!=std::string::npos);
    assert(html.find("name='rf1'")!=std::string::npos);
    assert(html.find("Save &amp; Reboot")!=std::string::npos);
    assert(html.find("does not cut electrical power")!=std::string::npos);
    const auto stats=renderStatsPage(m);
    for (const auto* text : {&html,&stats}) {
        assert(text->find("Forwarding is handled by openHop.")==std::string::npos);
        assert(text->find("RF2&lt;&amp;&quot;")!=std::string::npos);
        assert(text->find("5056")!=std::string::npos);
        assert(text->find("192.0.2.10")!=std::string::npos);
        assert(text->find("192.0.2.20")!=std::string::npos);
        assert(text->find("198.51.100.99")==std::string::npos);
        assert(text->find("Not ready")!=std::string::npos);
        assert(text->find(".k{display:block;")!=std::string::npos);
        assert(text->find("overflow-wrap:anywhere")!=std::string::npos);
        assert(text->find("minmax(min(100%,220px),1fr)")!=std::string::npos);
    }
    assert(html.find("Radio status")!=std::string::npos);
    assert(html.find("class='kv radio-status'")!=std::string::npos);
    assert(html.find("<details class='radio-endpoint' open>")==std::string::npos);
    for (const auto* label : {"Frequency", "Bandwidth", "Spreading factor", "RX packets", "TX packets", "CRC errors"}) {
        assert(html.find(label)==std::string::npos);
        assert(stats.find(label)!=std::string::npos);
    }
    assert(stats.find("Radio endpoints")!=std::string::npos);
    assert(stats.find("<details class='radio-endpoint' open>")!=std::string::npos);
    assert(stats.find(".radio-endpoint .inside{margin-top:.75em}")!=std::string::npos);
    assert(html.find("class='cli-example'")!=std::string::npos);
    assert(html.find("value='5055' readonly")!=std::string::npos);
    assert(html.find("Ports are assigned by the board")!=std::string::npos);
    SetupModel setup;
    setup.tcpPort=5055; setup.tcpPortReadOnly=true;
    assert(renderSetupPage(setup).find("value='5055' readonly")!=std::string::npos);
    const auto json=renderRadiosJson(m);
    assert(json.find("secret-test-fixture")==std::string::npos);
    const auto aggregate=renderStatsJson(m);
    assert(aggregate.find("\"radios\":[")!=std::string::npos);
    std::cout << json << '\n';
    m.radios[1].enabled=false;
    const auto disabled=renderRootPage(m);
    assert(disabled.find("Disabled")!=std::string::npos);
    assert(disabled.find("name='rf1' value='1' checked")==std::string::npos);
    assert(disabled.find("name='rf0' value='1' checked")!=std::string::npos);
    assert(renderRadiosJson(m).find("\"enabled\":false")!=std::string::npos);
    // Match production capability/primary projection wiring: a disabled RF1
    // keeps the legacy Error projection, but multi-radio Stats must not show it
    // as an additional unqualified single-radio failure.
    m.capabilities.radio=true;
    m.radio.available=true;
    for (const unsigned mask : {0U, 2U, 1U}) {
        m.radio.state=(mask & 1U) ? "RX/Idle" : "Error";
        for (size_t slot=0; slot<m.radios.size(); ++slot) {
            auto& endpoint=m.radios[slot];
            endpoint.enabled=(mask & (1U << slot)) != 0;
            endpoint.ready=endpoint.enabled;
            endpoint.radio.state=endpoint.enabled ? "RX/Idle" : "Disabled";
        }
        const auto selectionStats=renderStatsPage(m);
        assert(selectionStats.find("<h3>Radio</h3>")==std::string::npos &&
               "multi-radio Stats must not display a duplicate legacy primary error");
        assert(selectionStats.find("Radio endpoints")!=std::string::npos);
        if (mask != 3U) assert(selectionStats.find("Disabled")!=std::string::npos);
    }
    m.radios.clear(); m.config.tcpPortReadOnly=false;
    assert(renderStatsPage(m).find("<h3>Radio</h3>")!=std::string::npos); // Legacy section retained.
    assert(renderRootPage(m).find("Radio endpoints")==std::string::npos);
    assert(renderRootPage(m).find("Radio status")==std::string::npos);
    assert(renderRootPage(m).find("value='5055' readonly")==std::string::npos);
    assert(renderStatsJson(m).find("\"radios\"")==std::string::npos);
}
