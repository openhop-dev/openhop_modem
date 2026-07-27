// =============================================================
// config_portal.cpp — HTML setup form served in AP mode
// =============================================================
#include "config_portal.h"
#include "rf_frontend.h"
#include "wifi_manager.h"
#include "webui_shared.h"

#include <WebServer.h>
#include <WiFi.h>

namespace ConfigPortal {

static WebServer* server = nullptr;
static bool       active = false;

static void handleRoot() {
    const auto& cfg = WifiManager::getConfig();

    // Fresh scan for SSID list (blocks ~1–2 s). Many routers/mesh APs broadcast
    // the same SSID from multiple BSSIDs, so we dedupe by name (keep max RSSI)
    // and sort strongest first.
    int n = WiFi.scanNetworks(false, true);

    static const int MAX_UNIQUE = 32;
    String uniq_ssid[MAX_UNIQUE];
    int    uniq_rssi[MAX_UNIQUE];
    int    uniq_count = 0;
    for (int i = 0; i < n; i++) {
        String s = WiFi.SSID(i);
        int    r = WiFi.RSSI(i);
        if (s.length() == 0) continue;
        int found = -1;
        for (int j = 0; j < uniq_count; j++) {
            if (uniq_ssid[j] == s) { found = j; break; }
        }
        if (found >= 0) {
            if (r > uniq_rssi[found]) uniq_rssi[found] = r;
        } else if (uniq_count < MAX_UNIQUE) {
            uniq_ssid[uniq_count] = s;
            uniq_rssi[uniq_count] = r;
            uniq_count++;
        }
    }
    for (int i = 0; i < uniq_count - 1; i++) {
        for (int j = i + 1; j < uniq_count; j++) {
            if (uniq_rssi[j] > uniq_rssi[i]) {
                int    tr = uniq_rssi[i]; uniq_rssi[i] = uniq_rssi[j]; uniq_rssi[j] = tr;
                String ts = uniq_ssid[i]; uniq_ssid[i] = uniq_ssid[j]; uniq_ssid[j] = ts;
            }
        }
    }

    WebUiShared::SetupModel model;
    model.savedSsid = cfg.ssid.c_str();
    model.password = cfg.password.c_str();
    for (int i = 0; i < uniq_count; i++) {
        model.networks.push_back({uniq_ssid[i].c_str(), uniq_rssi[i]});
    }
    model.wifiAntennaSelection = WifiManager::hasWifiAntennaSwitch();
    model.wifiExternalAntenna = cfg.wifiExternalAntenna;
    model.heltecV43Controls = RFFrontEnd::hasHeltecV43LnaControl();
    model.heltecV43ExternalLnaEnabled = RFFrontEnd::isExternalLnaEnabled();
    model.agcResetIntervalSec = RFFrontEnd::getAgcResetIntervalSec();
    model.useStaticIp = cfg.useStaticIP;
    model.staticIp = cfg.staticIP.toString().c_str();
    model.gateway = cfg.gateway.toString().c_str();
    model.subnet = cfg.subnet.toString().c_str();
    model.dns1 = cfg.dns1.toString().c_str();
    model.dns2 = cfg.dns2.toString().c_str();
    model.hostname = cfg.hostname.c_str();
    model.tcpPort = cfg.tcpPort;
    model.tcpToken = cfg.tcpToken.c_str();

    const std::string html = WebUiShared::renderSetupPage(model);
    server->send(200, "text/html; charset=utf-8", html.c_str());
    WiFi.scanDelete();
}

static IPAddress parseIP(const String& s) {
    IPAddress ip;
    if (!ip.fromString(s)) ip = IPAddress((uint32_t)0);
    return ip;
}

static void handleSave() {
    WifiManager::Config newCfg = WifiManager::getConfig();

    String ssidSel = server->arg("ssid");
    String ssidMan = server->arg("ssid_manual");
    ssidMan.trim();
    ssidSel.trim();
    newCfg.ssid        = ssidMan.length() > 0 ? ssidMan : ssidSel;
    newCfg.password    = server->arg("password");
    newCfg.wifiExternalAntenna = WifiManager::hasWifiAntennaSwitch()
                                    ? server->hasArg("wifi_ant_ext")
                                    : false;
    if (RFFrontEnd::hasHeltecV43LnaControl()) {
        RFFrontEnd::setFemLnaBypassed(!server->hasArg("v43_lna_on"), true);
        uint32_t sec = (uint32_t)server->arg("agc_reset_interval_sec").toInt();
        if (sec > 3600U) sec = 3600U;
        RFFrontEnd::setAgcResetIntervalSec((uint16_t)sec, true);
    }
    newCfg.useStaticIP = server->hasArg("static");
    newCfg.staticIP    = parseIP(server->arg("ip"));
    newCfg.gateway     = parseIP(server->arg("gw"));
    newCfg.subnet      = parseIP(server->arg("sn"));
    newCfg.dns1        = parseIP(server->arg("dns1"));
    newCfg.dns2        = parseIP(server->arg("dns2"));
    newCfg.tcpToken    = server->arg("token");
    newCfg.hostname    = server->arg("hostname");
    newCfg.hostname.trim();

    int port = server->arg("port").toInt();
    if (port < 1 || port > 65535) port = 5055;
    newCfg.tcpPort = (uint16_t)port;

    Serial.printf("[Portal] POST /save: ssid_sel='%s' ssid_manual='%s' -> ssid='%s' "
                  "host='%s' password_len=%u static=%d port=%u token_len=%u\n",
                  ssidSel.c_str(), ssidMan.c_str(), newCfg.ssid.c_str(),
                  newCfg.hostname.c_str(),
                  (unsigned)newCfg.password.length(),
                  (int)newCfg.useStaticIP,
                  (unsigned)newCfg.tcpPort,
                  (unsigned)newCfg.tcpToken.length());

    if (newCfg.ssid.length() == 0) {
        Serial.println("[Portal] Save rejected: empty SSID");
        String err = F("<!DOCTYPE html><html><body style='font-family:system-ui,sans-serif;max-width:480px;margin:2em auto;padding:0 1em'>"
                       "<h2>SSID missing</h2>"
                       "<p>Select a network from the dropdown (not the '-- select --' entry) "
                       "or type one in the manual field. Password alone is not enough.</p>"
                       "<p><a href='/'>&larr; Back to form</a></p>"
                       "</body></html>");
        server->send(400, "text/html; charset=utf-8", err);
        return;
    }

    WifiManager::saveConfig(newCfg);
    Serial.printf("[Portal] Saved to NVS, rebooting into STA for '%s'\n",
                  newCfg.ssid.c_str());

    String body = F("<!DOCTYPE html><html><body style='font-family:system-ui,sans-serif;text-align:center;margin-top:3em'>"
                    "<h2>Saved. Rebooting…</h2>"
                    "<p>Device will attempt to join <b>");
    body += WebUiShared::htmlEscape(newCfg.ssid.c_str()).c_str();
    body += F("</b>.</p></body></html>");
    server->send(200, "text/html; charset=utf-8", body);

    delay(1000);
    ESP.restart();
}

void begin() {
    if (server) return;
    server = new WebServer(80);
    server->on("/",     HTTP_GET,  handleRoot);
    server->on("/save", HTTP_POST, handleSave);
    server->onNotFound([]() { server->send(404, "text/plain", "Not found"); });
    server->begin();
    active = true;
}

void loop() {
    if (server) server->handleClient();
}

bool isActive() { return active; }

} // namespace ConfigPortal
