#include "webui_shared.h"
#include <cassert>
#include <iostream>

int main() {
    for (const char* board : {"Station G2", "Station G3"}) {
        WebUiShared::Model model;
        model.board = board;
        model.capabilities.writableManagement = true;
        model.capabilities.stationAgcControls = true;
        model.config.agcResetIntervalSec = 4;
        const auto html = WebUiShared::renderRootPage(model);
        assert(html.find("<summary>Station AGC Recovery</summary>") != std::string::npos);
        assert(html.find("action='/agc-reset'") != std::string::npos);
        assert(html.find("name='agc_reset_interval_sec'") != std::string::npos);
        assert(html.find("value='4'") != std::string::npos);
    }
    // Board names must not grant capabilities; unrelated boards stay unchanged.
    for (const char* board : {"Station G2", "Heltec V3", "RAK4631"}) {
        WebUiShared::Model model;
        model.board = board;
        model.capabilities.writableManagement = true;
        const auto html = WebUiShared::renderRootPage(model);
        assert(html.find("action='/agc-reset'") == std::string::npos);
    }
    WebUiShared::Model model;
    model.capabilities.stationAgcControls = true;
    assert(WebUiShared::renderRootPage(model).find("action='/agc-reset'") == std::string::npos);
    model.capabilities.writableManagement = true;
    for (uint16_t interval : {0, 4, 3600}) {
        model.config.agcResetIntervalSec = interval;
        const auto html = WebUiShared::renderRootPage(model);
        const auto start = html.find("<input name='agc_reset_interval_sec'");
        assert(start != std::string::npos);
        const auto input = html.substr(start, html.find('>', start) - start);
        assert(input.find("value='" + std::to_string(interval) + "'") != std::string::npos);
        assert(input.find("min='0' max='3600' step='1' required") != std::string::npos);
    }
    model.capabilities.stationAgcControls = false;
    model.capabilities.heltecV43Controls = true;
    const auto heltec = WebUiShared::renderRootPage(model);
    assert(heltec.find("action='/agc-reset'") == std::string::npos);
    assert(heltec.find("action='/rf-lna'") != std::string::npos);
    assert(heltec.find("name='agc_reset_interval_sec'") != std::string::npos);
    std::cout << "Station AGC rendered WebUI: PASS\n";
}
