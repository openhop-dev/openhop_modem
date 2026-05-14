// =============================================================
// ota_manager.cpp — OTA via ArduinoOTA + HTTP /update, with
// dual-bank rollback guarded by a sanity watchdog.
// =============================================================
#include "ota_manager.h"
#include "net_filter.h"
#include "wifi_manager.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

namespace OTAManager {

static constexpr uint32_t SANITY_TIMEOUT_MS = 120000;  // mark firmware valid after 2 min of health
static constexpr uint16_t HTTP_PORT         = 80;
static constexpr const char* NVS_NAMESPACE  = "lora_modem";
static constexpr const char* HTTP_PASS_KEY  = "http_pass";
static constexpr const char* HTTP_AUTH_USER = "admin";
static constexpr const char* DEFAULT_HTTP_PASSWORD = "password";
static constexpr uint8_t MAX_HTTP_PASSWORD_LEN = 64;
static constexpr uint8_t MAX_TCP_TOKEN_LEN = 64;

static String      hostname;
static String      token;
static String      httpPassword;
static WebServer*  httpServer       = nullptr;
static bool        started          = false;
static bool        markedValid      = false;
static uint32_t    sanityDeadline   = 0;
static bool        sawValidFrame    = false;

static String modemTitle() {
    return String(BOARD.name) + " LoRa Modem";
}

static String currentIPString() {
    if (EthernetManager::hasIP()) return String(EthernetManager::getIPString());
    return WiFi.localIP().toString();
}

static void loadHttpPassword() {
    httpPassword = DEFAULT_HTTP_PASSWORD;
    Preferences p;
    if (p.begin(NVS_NAMESPACE, true)) {
        httpPassword = p.getString(HTTP_PASS_KEY, DEFAULT_HTTP_PASSWORD);
        p.end();
    }
    if (httpPassword.length() == 0) {
        httpPassword = DEFAULT_HTTP_PASSWORD;
    }
}

static bool saveHttpPassword(const String& password) {
    Preferences p;
    if (!p.begin(NVS_NAMESPACE, false)) return false;
    size_t written = p.putString(HTTP_PASS_KEY, password);
    p.end();
    if (written == 0) return false;
    httpPassword = password;
    return true;
}

static void sendSimplePage(const __FlashStringHelper* title,
                           const __FlashStringHelper* heading,
                           const __FlashStringHelper* message) {
    String body = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<title>");
    body += String(title);
    body += F("</title></head>"
              "<body style='font-family:system-ui,sans-serif;max-width:540px;"
              "margin:2em auto;padding:0 1em;color:#222'>"
              "<h2>");
    body += String(heading);
    body += F("</h2><p>");
    body += String(message);
    body += F("</p><p><a href='/'>Back to OTA page</a></p></body></html>");
    httpServer->send(200, "text/html; charset=utf-8", body);
}
// ─── Rollback sanity check ──────────────────────────────────
//
// When ESP-IDF boots from a newly-written slot, otadata marks it as
// "PENDING_VERIFY". On the NEXT reboot the bootloader auto-reverts to
// the previous slot unless we call esp_ota_mark_app_valid_cancel_rollback()
// first. We only call it once we've proven:
//   1. the radio came up (checked in main.cpp before OTAManager::begin)
//   2. a valid host frame was parsed — proves USB-CDC + frame parser work
//   3. we've been running for SANITY_TIMEOUT_MS without crashing
static void attemptMarkValid() {
    if (markedValid) return;
    if (!sawValidFrame) return;
    if ((int32_t)(millis() - sanityDeadline) < 0) return;

    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        markedValid = true;   // nothing we can do; stop retrying
        return;
    }
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        Serial.printf("[OTA] marked running app valid: %s\n",
                      err == ESP_OK ? "OK" : "FAIL");
    } else {
        Serial.printf("[OTA] running app state=%d (no rollback needed)\n", (int)state);
    }
    markedValid = true;
}

// ─── HTTP auth + handlers ───────────────────────────────────
static bool checkAuth() {
    // LAN-only policy first: drop any client whose source IP is
    // outside RFC1918 / link-local / loopback. Same rule as the
    // TCP protocol server (see net_filter.h). NAT-forwarded /
    // tunneled requests with a public source IP get a 403 and
    // never reach the auth check or the upload handler.
    IPAddress addr = httpServer->client().remoteIP();
    if (!isLanAddress(addr)) {
        Serial.printf("[OTA] reject non-LAN client %u.%u.%u.%u\n",
                      addr[0], addr[1], addr[2], addr[3]);
        httpServer->send(403, "text/plain",
                         "Forbidden: pymc_usb modem accepts LAN clients only.\n");
        return false;
    }
    if (httpPassword.length() == 0) httpPassword = DEFAULT_HTTP_PASSWORD;
    if (!httpServer->authenticate(HTTP_AUTH_USER, httpPassword.c_str())) {
        httpServer->requestAuthentication(BASIC_AUTH, modemTitle().c_str());
        return false;
    }
    return true;
}

static void handleRoot() {
    if (!checkAuth()) return;
    String title = modemTitle();
    const auto& cfg = WifiManager::getConfig();
    String body;
    body.reserve(3072);
    body += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>");
    body += title;
    body += F("</title>"
              "<style>body{font-family:system-ui,sans-serif;max-width:540px;"
              "margin:1em auto;padding:0 1em;color:#222}"
              "input{width:100%;padding:.5em;box-sizing:border-box;font-size:1em}"
              "input[type=file]{margin:1em 0;padding:.5em 0}"
              "button{margin-top:.8em;padding:.6em 1em;background:#3a7;color:#fff;border:0;border-radius:4px}"
              ".m{color:#666;font-size:.9em}"
              "hr{margin:2em 0;border:0;border-top:1px solid #ddd}"
              "label{display:block;margin-top:.9em;font-weight:600}</style></head><body>");
    body += "<h2>" + title + "</h2>";
    body += "<p class='m'>mDNS: <b>" + hostname + ".local</b> &nbsp; IP: <b>" +
            WiFi.localIP().toString() + "</b></p>";
    body += F("<h3>OTA update</h3>"
              "<form method='POST' action='/update' enctype='multipart/form-data'>"
              "<input type='file' name='firmware' accept='.bin' required>"
              "<br><button type='submit'>Upload firmware.bin</button>"
              "</form>"
              "<p class='m'>CLI alternative: "
              "<code>curl -u admin:&lt;password&gt; -F firmware=@firmware.bin http://");
    body += hostname + ".local/update</code></p>"
            "<hr>"
            "<h3>Hostname</h3>"
            "<form method='POST' action='/hostname'>"
            "<label>mDNS / OTA hostname</label>"
            "<input type='text' name='hostname' autocomplete='off' maxlength='32' value='" +
            cfg.hostname +
            "' placeholder='leave blank for default'>"
            "<button type='submit'>Save hostname</button>"
            "</form>"
            "<p class='m'>Blank resets to the board default. Reboot required.</p>"
            "<hr>"
            "<h3>pyMC TCP password</h3>"
            "<form method='POST' action='/token'>"
            "<label>New TCP password</label>"
            "<input type='password' name='token' autocomplete='new-password' maxlength='64'>"
            "<label>Confirm TCP password</label>"
            "<input type='password' name='confirm' autocomplete='new-password' maxlength='64'>"
            "<button type='submit'>Save TCP password</button>"
            "</form>";
    body += "<p class='m'>Current mode: <b>";
    body += cfg.tcpToken.length() > 0 ? "protected" : "open";
    body += F("</b>. Leave both fields blank to clear the TCP password. Reboot required.</p>"
              "<hr>"
            "<h3>HTTP password</h3>"
            "<form method='POST' action='/auth'>"
            "<label>New password</label>"
            "<input type='password' name='password' autocomplete='new-password' "
            "required minlength='1' maxlength='64'>"
            "<label>Confirm password</label>"
            "<input type='password' name='confirm' autocomplete='new-password' "
            "required minlength='1' maxlength='64'>"
            "<button type='submit'>Save password</button>"
            "</form>"
            "<p class='m'>Username: <b>admin</b>. Password changes take effect on the next request.</p>");
    body += F("</body></html>");
    httpServer->send(200, "text/html; charset=utf-8", body);
}

static void handleHostnameSave() {
    if (!checkAuth()) return;

    WifiManager::Config cfg = WifiManager::getConfig();
    String requested = httpServer->arg("hostname");
    requested.trim();
    cfg.hostname = requested;
    WifiManager::saveConfig(cfg);

    Serial.printf("[OTA] hostname updated by %s -> '%s'\n",
                  httpServer->client().remoteIP().toString().c_str(),
                  requested.c_str());

    String body = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                    "<title>Hostname saved</title></head>"
                    "<body style='font-family:system-ui,sans-serif;max-width:540px;"
                    "margin:2em auto;padding:0 1em;color:#222'>"
                    "<h2>Hostname saved</h2>"
                    "<p>The modem will reboot now and come back with the updated hostname.</p>"
                    "<p><a href='/'>Back to OTA page</a></p>"
                    "</body></html>");
    httpServer->send(200, "text/html; charset=utf-8", body);
    delay(500);
    ESP.restart();
}

static void handleTokenSave() {
    if (!checkAuth()) return;

    WifiManager::Config cfg = WifiManager::getConfig();
    String requested = httpServer->arg("token");
    String confirm   = httpServer->arg("confirm");

    if (requested.length() > MAX_TCP_TOKEN_LEN) {
        httpServer->send(400, "text/plain", "TCP password must be 0-64 characters.\n");
        return;
    }
    if (requested != confirm) {
        httpServer->send(400, "text/plain", "TCP password confirmation does not match.\n");
        return;
    }

    cfg.tcpToken = requested;
    WifiManager::saveConfig(cfg);

    Serial.printf("[OTA] TCP password updated by %s -> %s\n",
                  httpServer->client().remoteIP().toString().c_str(),
                  requested.length() > 0 ? "set" : "cleared");

    sendSimplePage(F("TCP password saved"),
                   F("TCP password saved"),
                   requested.length() > 0
                       ? F("The modem will reboot now and require the new pyMC TCP password.")
                       : F("The modem will reboot now and allow open TCP access again."));
    delay(500);
    ESP.restart();
}

static void handleAuthSave() {
    if (!checkAuth()) return;

    String password = httpServer->arg("password");
    String confirm  = httpServer->arg("confirm");
    if (password.length() == 0 || password.length() > MAX_HTTP_PASSWORD_LEN) {
        httpServer->send(400, "text/plain", "Password must be 1-64 characters.\n");
        return;
    }
    if (password != confirm) {
        httpServer->send(400, "text/plain", "Password confirmation does not match.\n");
        return;
    }
    if (!saveHttpPassword(password)) {
        httpServer->send(500, "text/plain", "Failed to save password.\n");
        return;
    }
    if (token.length() == 0) {
        ArduinoOTA.setPassword(httpPassword.c_str());
    }
    Serial.printf("[OTA] HTTP password changed by %s\n",
                  httpServer->client().remoteIP().toString().c_str());

    sendSimplePage(F("Password saved"),
                   F("Password saved"),
                   F("Use the new password the next time this page asks for credentials."));
}

static void handleUpdateResult() {
    if (!checkAuth()) return;
    bool ok = !Update.hasError();
    String body;
    if (ok) {
        body = F("OK — rebooting into new firmware. "
                 "If the new image fails its sanity check within 2 minutes, "
                 "the bootloader will roll back automatically.");
    } else {
        body = String(F("FAIL — ")) + Update.errorString();
    }
    httpServer->send(ok ? 200 : 500, "text/plain", body);
    if (ok) {
        delay(500);
        ESP.restart();
    }
}

static void handleUpdateUpload() {
    if (!checkAuth()) return;
    HTTPUpload& up = httpServer->upload();

    if (up.status == UPLOAD_FILE_START) {
        Serial.printf("[OTA/HTTP] upload start: %s (%u bytes expected)\n",
                      up.filename.c_str(), (unsigned)up.totalSize);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            Update.printError(Serial);
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            Serial.printf("[OTA/HTTP] upload complete: %u bytes\n",
                          (unsigned)up.totalSize);
        } else {
            Update.printError(Serial);
        }
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.end();
        Serial.println("[OTA/HTTP] upload aborted");
    }
}

// ─── ArduinoOTA plumbing ────────────────────────────────────
static void configureArduinoOTA() {
    ArduinoOTA.setHostname(hostname.c_str());
    if (token.length() > 0) {
        ArduinoOTA.setPassword(token.c_str());
    } else {
        ArduinoOTA.setPassword(httpPassword.c_str());
    }
    ArduinoOTA.onStart([]() {
        Serial.println("[OTA/Arduino] start");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("[OTA/Arduino] end");
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[OTA/Arduino] error %u\n", (unsigned)e);
    });
    ArduinoOTA.begin();
}

// ─── Public API ─────────────────────────────────────────────
void begin(const String& hn, const String& tk) {
    if (started) return;
    hostname = hn;
    token    = tk;
    loadHttpPassword();

    if (!MDNS.begin(hostname.c_str())) {
        Serial.println("[OTA] mDNS start failed");
    } else {
        MDNS.addService("http", "tcp", HTTP_PORT);
        MDNS.addService("arduino", "tcp", 3232);
        Serial.printf("[OTA] mDNS advertising %s.local\n", hostname.c_str());
    }

    configureArduinoOTA();

    httpServer = new WebServer(HTTP_PORT);
    httpServer->on("/",       HTTP_GET,  handleRoot);
    httpServer->on("/hostname", HTTP_POST, handleHostnameSave);
    httpServer->on("/token",  HTTP_POST, handleTokenSave);
    httpServer->on("/auth",   HTTP_POST, handleAuthSave);
    httpServer->on("/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);
    httpServer->onNotFound([]() { httpServer->send(404, "text/plain", "Not found"); });
    httpServer->begin();

    sanityDeadline = millis() + SANITY_TIMEOUT_MS;
    sawValidFrame  = false;
    markedValid    = false;
    started        = true;

    Serial.printf("[OTA] HTTP /update + ArduinoOTA ready on %s (http auth: %s, arduino ota: %s)\n",
                  currentIPString().c_str(),
                  HTTP_AUTH_USER,
                  token.length() > 0 ? "tcp token" : "http password");
}

void loop() {
    if (!started) return;
    ArduinoOTA.handle();
    if (httpServer) httpServer->handleClient();
    if (!markedValid) attemptMarkValid();
}

void notifyValidFrame() {
    sawValidFrame = true;
}

const char* getHostname() {
    return hostname.c_str();
}

} // namespace OTAManager
