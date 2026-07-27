#include "rak4631_web_handlers.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

using namespace Rak4631WebHandlers;

namespace {

struct Store {
    int calls = 0;
    Rak4631Config::Config saved{};
    bool succeeds = true;
};

bool save(const Rak4631Config::Config& config, void* context) {
    auto& store = *static_cast<Store*>(context);
    ++store.calls;
    store.saved = config;
    return store.succeeds;
}

Rak4631Config::Config defaults() {
    auto config = Rak4631Config::makeDefaults("rak-old", 5055, "existing-token");
    std::strcpy(config.httpPassword, "old-password");
    config.gpsEnabled = false;
    return config;
}

Response request(const char* route, const char* contentType, const std::string& body,
                 Store& store, bool gpsSupported = true,
                 const char* origin = nullptr, const char* host = nullptr) {
    Request input{};
    input.route = route;
    input.contentType = contentType;
    input.body = body.data();
    input.bodyLength = body.size();
    input.origin = origin;
    input.host = host;
    input.current = defaults();
    input.gpsSupported = gpsSupported;
    return handlePost(input, save, &store);
}

void assertSecretFree(const Response& response) {
    assert(response.body.find("old-password") == std::string::npos);
    assert(response.body.find("existing-token") == std::string::npos);
    assert(response.body.find("new-password") == std::string::npos);
    assert(response.body.find("new-token") == std::string::npos);
}

void testHostnameSanitizeBlankAndLimits() {
    Store store;
    auto response = request("/hostname", "application/x-www-form-urlencoded",
                            "hostname=Bad+HOST_name%21", store);
    assert(response.status == 200);
    assert(response.transition == Transition::REBOOT);
    assert(store.calls == 1);
    assert(std::strcmp(store.saved.hostname, "bad-host-name") == 0);
    assertSecretFree(response);

    store = {};
    response = request("/hostname", "application/x-www-form-urlencoded", "hostname=", store);
    assert(response.status == 200);
    assert(store.saved.hostname[0] == '\0');

    store = {};
    response = request("/hostname", "application/x-www-form-urlencoded",
                       "hostname=" + std::string(65, 'a'), store);
    assert(response.status == 400);
    assert(store.calls == 0);

    store = {};
    response = request("/hostname", "application/x-www-form-urlencoded",
                       "hostname=one&hostname=two", store);
    assert(response.status == 400);
    assert(store.calls == 0);
}

void testNetworkTransitionsAndPortValidation() {
    Store store;
    auto response = request(
        "/network", "application/x-www-form-urlencoded",
        "static=1&ip=192.168.4.20&sn=255.255.255.0&gw=192.168.4.1&dns1=1.1.1.1&dns2=&port=6000",
        store);
    assert(response.status == 200);
    assert(store.saved.useStaticIP);
    assert(store.saved.staticIP.octets[3] == 20);
    assert(store.saved.tcpPort == 6000);

    store = {};
    response = request("/network", "application/x-www-form-urlencoded", "port=5055", store);
    assert(response.status == 200);
    assert(!store.saved.useStaticIP);
    assert(store.saved.staticIP.isZero());

    const char* invalid[] = {
        "static=1&ip=192.168.4.20&sn=255.255.255.0&gw=&port=5055",
        "static=1&ip=192.168.4.0&sn=255.255.255.0&gw=192.168.4.1&port=5055",
        "port=80", "port=0", "port=65536", "port=nope",
    };
    for (const char* body : invalid) {
        store = {};
        response = request("/network", "application/x-www-form-urlencoded", body, store);
        assert(response.status == 400);
        assert(store.calls == 0);
    }
}

void testTokenPasswordConfirmationAndContentTypes() {
    Store store;
    auto response = request("/token", "application/x-www-form-urlencoded",
                            "token=new-token&confirm=new-token", store);
    assert(response.status == 200);
    assert(std::strcmp(store.saved.tcpToken, "new-token") == 0);
    assertSecretFree(response);

    store = {};
    response = request("/token", "application/x-www-form-urlencoded", "token=&confirm=", store);
    assert(response.status == 200);
    assert(store.saved.tcpToken[0] == '\0');

    const char* badToken[] = {"token=a&confirm=b", "token=a", "token=a&confirm=a&extra=1"};
    for (const char* body : badToken) {
        store = {};
        response = request("/token", "application/x-www-form-urlencoded", body, store);
        assert(response.status == 400);
    }

    store = {};
    response = request("/auth", "application/x-www-form-urlencoded",
                       "password=new-password&confirm=new-password", store);
    assert(response.status == 200);
    assert(std::strcmp(store.saved.httpPassword, "new-password") == 0);
    assert(response.transition == Transition::NONE);
    assert(response.body.find("\"rebooting\":false") != std::string::npos);
    assertSecretFree(response);

    store = {};
    response = request("/auth", "application/x-www-form-urlencoded",
                       "password=&confirm=", store);
    assert(response.status == 400);

    store = {};
    response = request("/auth", "application/json", "{}", store);
    assert(response.status == 415);
    assert(store.calls == 0);
}

void testGpsAndNoArbitraryHardwareFields() {
    Store store;
    auto response = request("/gps", "application/x-www-form-urlencoded",
                            "gps_enabled=1", store, true);
    assert(response.status == 200);
    assert(store.saved.gpsEnabled);

    store = {};
    response = request("/gps", "application/x-www-form-urlencoded", "", store, true);
    assert(response.status == 200);
    assert(!store.saved.gpsEnabled);

    const char* unsupported[] = {"gpio=4", "wifi_external_antenna=1", "lna=1", "gps_mode=i2c"};
    for (const char* body : unsupported) {
        store = {};
        response = request("/gps", "application/x-www-form-urlencoded", body, store, true);
        assert(response.status == 400);
    }

    store = {};
    response = request("/gps", "application/x-www-form-urlencoded", "gps_enabled=1", store, false);
    assert(response.status == 400);
}

void testJsonConfigStrictPartialUpdates() {
    Store store;
    auto response = request(
        "/api/config", "application/json",
        R"({"hostname":"RAK JSON","tcp_port":6001,"tcp_token":"new-token","gps_enabled":true,"network":{"use_static_ip":true,"static_ip":"10.1.2.3","subnet":"255.255.255.0","gateway":"10.1.2.1","dns1":"1.1.1.1","dns2":""}})",
        store);
    assert(response.status == 200);
    assert(response.transition == Transition::REBOOT);
    assert(std::strcmp(store.saved.hostname, "rak-json") == 0);
    assert(store.saved.tcpPort == 6001);
    assert(store.saved.gpsEnabled);
    assert(store.saved.useStaticIP);
    assertSecretFree(response);
    assert(response.body.find("tcp_token_set") != std::string::npos);
    assert(response.body.find("\"hostname\":\"rak-json\"") != std::string::npos);
    assert(response.body.find("tcp_token\"") == std::string::npos);
    assert(response.body.find("httpPassword") == std::string::npos);

    const char* invalid[] = {
        R"({"hostname":"a","hostname":"b"})",
        R"({"unknown":true})",
        R"({"wifi_external_antenna":true})",
        R"({"heltec_v43_external_lna_enabled":true})",
        R"({"gpio":4})",
        R"({"network":{"use_static_ip":true,"static_ip":"10.0.0.2","subnet":"255.255.255.0","gateway":"10.0.0.1","gateway":"10.0.0.3"}})",
        R"({"tcp_port":80})",
        R"({"tcp_port":"5055"})",
        R"({"gps_enabled":"true"})",
        R"({"hostname":"unterminated})",
    };
    for (const char* body : invalid) {
        store = {};
        response = request("/api/config", "application/json", body, store);
        assert(response.status == 400);
        assert(store.calls == 0);
        assertSecretFree(response);
    }
}

void testExactDfuAndRebootContracts() {
    Store store;
    auto response = request("/dfu/ble", "", "", store);
    assert(response.status == 200);
    assert(response.transition == Transition::BLE_DFU);
    assert(response.body == "{\"status\":\"entering_ble_dfu\",\"advertises_as\":\"bootloader-defined\"}");
    assert(store.calls == 0);

    response = request("/dfu/ble", "application/json", "{}", store);
    assert(response.status == 400);
    response = request("/api/dfu/ble", "", "", store);
    assert(response.status == 404);
    response = request("/ble-dfu", "", "", store);
    assert(response.status == 404);

    response = request("/reboot", "", "", store);
    assert(response.status == 200 && response.transition == Transition::REBOOT);
    response = request("/api/reboot", "", "", store);
    assert(response.status == 200 && response.transition == Transition::REBOOT);
    response = request("/reboot", "application/x-www-form-urlencoded", "", store);
    assert(response.status == 200 && response.transition == Transition::REBOOT);
    response = request("/reboot", "application/x-www-form-urlencoded", "x=1", store);
    assert(response.status == 400);
}

void testPersistenceFailureDoesNotScheduleTransition() {
    Store store;
    store.succeeds = false;
    const auto response = request("/hostname", "application/x-www-form-urlencoded",
                                  "hostname=new", store);
    assert(response.status == 500);
    assert(response.transition == Transition::NONE);
}

void testBrowserOriginMustMatchHost() {
    Store store;
    auto response = request("/hostname", "application/x-www-form-urlencoded",
                            "hostname=new", store, true,
                            "http://attacker.example", "192.168.1.20");
    assert(response.status == 403);
    assert(store.calls == 0);
    response = request("/hostname", "application/x-www-form-urlencoded",
                       "hostname=new", store, true,
                       "http://192.168.1.20", "192.168.1.20");
    assert(response.status == 200);
    assert(store.calls == 1);
}

}  // namespace

int main() {
    testHostnameSanitizeBlankAndLimits();
    testNetworkTransitionsAndPortValidation();
    testTokenPasswordConfirmationAndContentTypes();
    testGpsAndNoArbitraryHardwareFields();
    testJsonConfigStrictPartialUpdates();
    testExactDfuAndRebootContracts();
    testPersistenceFailureDoesNotScheduleTransition();
    testBrowserOriginMustMatchHost();
    std::cout << "rak4631 web handler tests passed\n";
}
