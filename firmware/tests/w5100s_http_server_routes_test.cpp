#include "../include/w5100s_http_server.h"

#include <cassert>
#include <iostream>

using HttpRequest::Method;
using W5100sHttpServer::RouteAction;

int main() {
    assert(W5100sHttpServer::classifyRoute(Method::GET, "/") == RouteAction::ROOT_HTML);
    assert(W5100sHttpServer::classifyRoute(Method::GET, "/stats") == RouteAction::STATS_HTML);
    assert(W5100sHttpServer::classifyRoute(Method::GET, "/api/temp") == RouteAction::TEMP_JSON);
    assert(W5100sHttpServer::classifyRoute(Method::GET, "/api/system") == RouteAction::SYSTEM_JSON);
    assert(W5100sHttpServer::classifyRoute(Method::GET, "/api/radio") == RouteAction::RADIO_JSON);
    assert(W5100sHttpServer::classifyRoute(Method::GET, "/api/network") == RouteAction::NETWORK_JSON);
    assert(W5100sHttpServer::classifyRoute(Method::GET, "/api/stats") == RouteAction::STATS_JSON);
    assert(W5100sHttpServer::classifyRoute(Method::GET, "/api/config") == RouteAction::CONFIG_JSON);
    assert(W5100sHttpServer::classifyRoute(Method::GET, "/api/gps") == RouteAction::GPS_JSON);
    assert(W5100sHttpServer::classifyRoute(Method::GET, "/update") == RouteAction::NOT_FOUND);
    assert(W5100sHttpServer::classifyRoute(Method::POST, "/update") == RouteAction::NOT_FOUND);
    assert(W5100sHttpServer::classifyRoute(Method::POST, "/api/config") == RouteAction::MANAGEMENT_POST);
    assert(W5100sHttpServer::classifyRoute(Method::POST, "/api/reboot") == RouteAction::MANAGEMENT_POST);
    assert(W5100sHttpServer::classifyRoute(Method::POST, "/reboot") == RouteAction::MANAGEMENT_POST);
    assert(W5100sHttpServer::classifyRoute(Method::POST, "/hostname") == RouteAction::MANAGEMENT_POST);
    assert(W5100sHttpServer::classifyRoute(Method::POST, "/network") == RouteAction::MANAGEMENT_POST);
    assert(W5100sHttpServer::classifyRoute(Method::POST, "/token") == RouteAction::MANAGEMENT_POST);
    assert(W5100sHttpServer::classifyRoute(Method::POST, "/auth") == RouteAction::MANAGEMENT_POST);
    assert(W5100sHttpServer::classifyRoute(Method::POST, "/gps") == RouteAction::MANAGEMENT_POST);
    // BLE DFU remains gated until the exact installed bootloader has produced
    // and recovered from a real BLE DFU session on hardware.
    assert(W5100sHttpServer::classifyRoute(Method::POST, "/dfu/ble") == RouteAction::NOT_FOUND);
    assert(W5100sHttpServer::classifyRoute(Method::POST, "/api/dfu/ble") == RouteAction::NOT_FOUND);
    assert(W5100sHttpServer::classifyRoute(Method::POST, "/ble-dfu") == RouteAction::NOT_FOUND);
    assert(W5100sHttpServer::classifyRoute(Method::GET, "/dfu/ble") == RouteAction::NOT_FOUND);
    assert(W5100sHttpServer::classifyRoute(Method::POST, "/missing") == RouteAction::NOT_FOUND);
    assert(W5100sHttpServer::classifyRoute(Method::GET, "/api/stat") == RouteAction::NOT_FOUND);
    std::cout << "w5100s_http_server_routes_test: OK\n";
}
