// =============================================================
// nrf_web_manager.cpp — lightweight HTTP management UI for the
// RAK4631 + RAK13800/W5100S nRF52 target.
//
// Deliberately excludes network OTA: nRF52 firmware updates remain
// USB/DFU or protocol-based. The UI covers status, Ethernet/TCP
// configuration, authentication, and reboot.
// =============================================================
#if defined(PYMC_ETHERNET_W5100S)

#include "ota_manager.h"
#include "board_config.h"
#include "net_filter.h"
#include "nrf_settings.h"
#include "runtime_stats.h"
#include "wifi_manager.h"
#include "w5100s_ethernet_transport.h"

#include <RAK13800_W5100S.h>
#include <nrf.h>

namespace OTAManager {

static constexpr uint16_t HTTP_PORT = 80;
static constexpr const char* HTTP_AUTH_USER = "admin";
static constexpr size_t MAX_HEADER_BYTES = 3072;
static constexpr size_t MAX_BODY_BYTES = 1024;
static constexpr uint32_t CLIENT_IDLE_TIMEOUT_MS = 3000;
static constexpr size_t RESPONSE_CHUNK_BYTES = 384;

static EthernetServer* server = nullptr;
static EthernetClient client;
static String hostname;
static bool started = false;
static bool lastIpUsable = false;

static String requestBuffer;
static size_t headerEnd = 0;
static size_t contentLength = 0;
static uint32_t lastClientActivityMs = 0;
static bool responseReady = false;
static String responseBuffer;
static size_t responseOffset = 0;
static bool rebootAfterResponse = false;
static uint32_t rebootAtMs = 0;

static const char* statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 303: return "See Other";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default:  return "Error";
    }
}

static String htmlEscape(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        switch (value[i]) {
            case '&': out += F("&amp;"); break;
            case '<': out += F("&lt;"); break;
            case '>': out += F("&gt;"); break;
            case '"': out += F("&quot;"); break;
            case '\'': out += F("&#39;"); break;
            default: out += value[i]; break;
        }
    }
    return out;
}

static String jsonEscape(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        switch (value[i]) {
            case '"': out += F("\\\""); break;
            case '\\': out += F("\\\\"); break;
            case '\n': out += F("\\n"); break;
            case '\r': out += F("\\r"); break;
            case '\t': out += F("\\t"); break;
            default: out += value[i]; break;
        }
    }
    return out;
}

static String formatUptime(uint32_t uptimeSec) {
    uint32_t days = uptimeSec / 86400UL;
    uint32_t hours = (uptimeSec % 86400UL) / 3600UL;
    uint32_t minutes = (uptimeSec % 3600UL) / 60UL;
    uint32_t seconds = uptimeSec % 60UL;
    char buf[32];
    if (days > 0) {
        snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
                 (unsigned long)days, (unsigned long)hours,
                 (unsigned long)minutes, (unsigned long)seconds);
    } else {
        snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                 (unsigned long)hours, (unsigned long)minutes,
                 (unsigned long)seconds);
    }
    return String(buf);
}

static String currentIPString() {
    return EthernetManager::hasIP() ? String(EthernetManager::getIPString()) : String("---");
}

static String base64Encode(const String& input) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String out;
    out.reserve(((input.length() + 2) / 3) * 4);
    for (size_t i = 0; i < input.length(); i += 3) {
        uint32_t value = (uint32_t)(uint8_t)input[i] << 16;
        bool haveSecond = i + 1 < input.length();
        bool haveThird = i + 2 < input.length();
        if (haveSecond) value |= (uint32_t)(uint8_t)input[i + 1] << 8;
        if (haveThird) value |= (uint32_t)(uint8_t)input[i + 2];
        out += table[(value >> 18) & 0x3F];
        out += table[(value >> 12) & 0x3F];
        out += haveSecond ? table[(value >> 6) & 0x3F] : '=';
        out += haveThird ? table[value & 0x3F] : '=';
    }
    return out;
}

static String headerValue(const String& headers, const char* name) {
    String prefix(name);
    prefix.toLowerCase();
    size_t lineStart = 0;
    while (lineStart < headers.length()) {
        int lineEndRaw = headers.indexOf("\r\n", lineStart);
        size_t lineEnd = lineEndRaw < 0 ? headers.length() : (size_t)lineEndRaw;
        int colonRaw = headers.indexOf(':', lineStart);
        if (colonRaw >= 0 && (size_t)colonRaw < lineEnd) {
            String key = headers.substring(lineStart, (size_t)colonRaw);
            key.toLowerCase();
            key.trim();
            if (key == prefix) {
                String value = headers.substring((size_t)colonRaw + 1, lineEnd);
                value.trim();
                return value;
            }
        }
        if (lineEndRaw < 0) break;
        lineStart = lineEnd + 2;
    }
    return String();
}

static bool isAuthorized(const String& headers) {
    const String supplied = headerValue(headers, "authorization");
    const String expected = String("Basic ") +
        base64Encode(String(HTTP_AUTH_USER) + ":" + NrfSettings::getHttpPassword());
    return supplied == expected;
}

static bool parseContentLength(const String& value, size_t& parsed) {
    if (value.length() == 0) {
        parsed = 0;
        return true;
    }
    uint32_t result = 0;
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (c < '0' || c > '9') return false;
        const uint32_t digit = (uint32_t)(c - '0');
        if (result > (MAX_BODY_BYTES - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    parsed = (size_t)result;
    return true;
}

static bool urlDecode(const String& encoded, String& decoded) {
    decoded = "";
    decoded.reserve(encoded.length());
    for (size_t i = 0; i < encoded.length(); ++i) {
        char c = encoded[i];
        if (c == '+') {
            decoded += ' ';
        } else if (c == '%') {
            if (i + 2 >= encoded.length()) return false;
            auto hex = [](char x) -> int {
                if (x >= '0' && x <= '9') return x - '0';
                if (x >= 'a' && x <= 'f') return x - 'a' + 10;
                if (x >= 'A' && x <= 'F') return x - 'A' + 10;
                return -1;
            };
            int hi = hex(encoded[i + 1]);
            int lo = hex(encoded[i + 2]);
            if (hi < 0 || lo < 0) return false;
            char value = (char)((hi << 4) | lo);
            if (value == '\0') return false;
            decoded += value;
            i += 2;
        } else {
            decoded += c;
        }
    }
    return true;
}

static bool formValue(const String& body, const char* key, String& value) {
    size_t start = 0;
    while (start <= body.length()) {
        int ampRaw = body.indexOf('&', start);
        size_t end = ampRaw < 0 ? body.length() : (size_t)ampRaw;
        int equalRaw = body.indexOf('=', start);
        size_t equal = (equalRaw >= 0 && (size_t)equalRaw < end) ? (size_t)equalRaw : end;
        String rawKey = body.substring(start, equal);
        String decodedKey;
        if (!urlDecode(rawKey, decodedKey)) return false;
        if (decodedKey == key) {
            String rawValue = equal < end ? body.substring(equal + 1, end) : String();
            return urlDecode(rawValue, value);
        }
        if (ampRaw < 0) break;
        start = end + 1;
    }
    value = "";
    return true;
}

static bool hasFormKey(const String& body, const char* key) {
    size_t start = 0;
    while (start <= body.length()) {
        int ampRaw = body.indexOf('&', start);
        size_t end = ampRaw < 0 ? body.length() : (size_t)ampRaw;
        int equalRaw = body.indexOf('=', start);
        size_t equal = (equalRaw >= 0 && (size_t)equalRaw < end) ? (size_t)equalRaw : end;
        String rawKey = body.substring(start, equal);
        String decodedKey;
        if (urlDecode(rawKey, decodedKey) && decodedKey == key) return true;
        if (ampRaw < 0) break;
        start = end + 1;
    }
    return false;
}

static bool validHostname(const String& hostnameValue) {
    if (hostnameValue.length() == 0 || hostnameValue.length() > 32) return false;
    if (hostnameValue[0] == '-' || hostnameValue[hostnameValue.length() - 1] == '-') return false;
    for (size_t i = 0; i < hostnameValue.length(); ++i) {
        const char c = hostnameValue[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    return true;
}

static bool parseIPAddress(const String& value, IPAddress& address) {
    address = IPAddress((uint32_t)0);
    return value.length() > 0 && address.fromString(value) && (uint32_t)address != 0;
}

static String pageShellStart(const String& title) {
    String body;
    body.reserve(4096);
    body += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>");
    body += htmlEscape(title);
    body += F("</title><style>"
              ":root{font-family:system-ui,-apple-system,sans-serif;color:#202124;background:#f5f7f8}"
              "*{box-sizing:border-box}body{max-width:760px;margin:0 auto;padding:24px 16px 48px}"
              "h1{font-size:1.45rem;margin:.2rem 0}h2{font-size:1.05rem;margin:0 0 .7rem}"
              "p{line-height:1.45}.muted{color:#5f6368;font-size:.92rem}.top{margin-bottom:18px}"
              ".card{background:#fff;border:1px solid #dfe3e6;border-radius:12px;padding:16px;margin:12px 0;box-shadow:0 1px 2px rgba(0,0,0,.04)}"
              ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:10px}"
              ".kv{border:1px solid #e1e5e8;border-radius:9px;padding:10px;background:#fafbfb}"
              ".k{display:block;color:#667078;font-size:.8rem;margin-bottom:3px}.v{font-weight:650;overflow-wrap:anywhere}"
              "label{display:block;font-weight:650;margin-top:12px}input{width:100%;padding:10px;border:1px solid #b9c1c7;border-radius:7px;font:inherit;margin-top:5px}"
              ".check{display:flex;gap:9px;align-items:center;margin:12px 0}.check input{width:auto;margin:0}.check label{margin:0}"
              "button{width:100%;margin-top:14px;padding:11px 14px;border:0;border-radius:8px;background:#176b52;color:#fff;font:inherit;font-weight:700;cursor:pointer}"
              "button.secondary{background:#48545c}.note{padding:10px 12px;border-radius:8px;background:#eef5f2;color:#28443a}"
              "code{background:#eef0f1;padding:2px 5px;border-radius:4px}details summary{cursor:pointer;font-weight:750}details .inside{padding-top:4px}"
              "a{color:#0b654c}@media(max-width:520px){body{padding:16px 10px 36px}.card{padding:13px}}"
              "</style></head><body>");
    return body;
}

static String buildRootPage() {
    const auto& cfg = WifiManager::getConfig();
    RuntimeStats::Snapshot snap = RuntimeStats::capture();
    String tcpClient = TCPServer::getClientIP();

    String body = pageShellStart(String(BOARD.name) + " openHop Modem");
    body += "<div class='top'><h1>" + htmlEscape(String(BOARD.name)) + " openHop Modem</h1>";
    body += F("<p class='muted'>nRF52 Ethernet management</p></div>");

    body += F("<section class='card'><h2>Status</h2><div class='grid'>");
    body += "<div class='kv'><span class='k'>Firmware</span><span class='v'>" + htmlEscape(snap.firmwareVersion) + "</span></div>";
    body += "<div class='kv'><span class='k'>IP address</span><span class='v'>" + currentIPString() + "</span></div>";
    body += "<div class='kv'><span class='k'>Uptime</span><span class='v'>" + formatUptime(snap.status.uptime_sec) + "</span></div>";
    body += "<div class='kv'><span class='k'>TCP client</span><span class='v'>" + (tcpClient.length() ? htmlEscape(tcpClient) : String("none")) + "</span></div>";
    body += "<div class='kv'><span class='k'>Radio</span><span class='v'>" + String(snap.radioStandby ? "Standby" : (snap.status.radio_state == 1 ? "TX" : "RX/Idle")) + "</span></div>";
    body += "<div class='kv'><span class='k'>RX / TX</span><span class='v'>" + String(snap.status.rx_count) + " / " + String(snap.status.tx_count) + "</span></div>";
    body += "<div class='kv'><span class='k'>Last RSSI / SNR</span><span class='v'>" + String(snap.status.last_rssi) + " dBm / " + String(snap.status.last_snr / 10.0f, 1) + " dB</span></div>";
    body += "<div class='kv'><span class='k'>Noise floor</span><span class='v'>" + String(snap.status.noise_floor_x10 / 10.0f, 1) + " dBm</span></div>";
    body += F("</div><p class='muted'><a href='/api/stats'>JSON status API</a></p></section>");

    body += F("<section class='card'><details open><summary>Ethernet and openHop TCP</summary><div class='inside'>"
              "<form method='POST' action='/network'>");
    body += "<label>Hostname<input name='hostname' maxlength='32' required value='" + htmlEscape(cfg.hostname) + "'></label>";
    body += "<label>TCP port<input name='port' type='number' min='1' max='65535' required value='" + String(cfg.tcpPort) + "'></label>";
    body += F("<div class='check'><input id='static' name='static' value='1' type='checkbox'");
    if (cfg.useStaticIP) body += F(" checked");
    body += F("><label for='static'>Use static IPv4 settings instead of DHCP</label></div>");
    body += "<div class='grid'><label>IP address<input name='ip' value='" + cfg.staticIP.toString() + "' placeholder='192.168.1.50'></label>";
    body += "<label>Subnet mask<input name='sn' value='" + cfg.subnet.toString() + "' placeholder='255.255.255.0'></label>";
    body += "<label>Gateway<input name='gw' value='" + cfg.gateway.toString() + "' placeholder='192.168.1.1'></label>";
    body += "<label>DNS 1<input name='dns1' value='" + cfg.dns1.toString() + "' placeholder='1.1.1.1'></label>";
    body += "<label>DNS 2<input name='dns2' value='" + cfg.dns2.toString() + "' placeholder='8.8.8.8'></label></div>";
    body += F("<button type='submit'>Save network settings and reboot</button></form>"
              "<p class='muted'>The W5100S library does not publish the hostname through DHCP or mDNS; it is retained for modem status and configuration.</p>"
              "</div></details></section>");

    body += F("<section class='card'><details><summary>openHop TCP token</summary><div class='inside'>"
              "<p class='muted'>Must match the token configured in openHop. Leave both fields blank to disable token authentication.</p>"
              "<form method='POST' action='/token'>"
              "<label>New token<input type='password' name='token' maxlength='64' autocomplete='new-password'></label>"
              "<label>Confirm token<input type='password' name='confirm' maxlength='64' autocomplete='new-password'></label>"
              "<button type='submit'>Save token and reboot</button></form><p class='muted'>Current mode: <strong>");
    body += cfg.tcpToken.length() ? "Protected" : "Open";
    body += F("</strong></p></div></details></section>");

    body += F("<section class='card'><details><summary>HTTP password</summary><div class='inside'>"
              "<p class='muted'>Protects this management page and API. Username: <code>admin</code>.</p>"
              "<form method='POST' action='/auth'>"
              "<label>New password<input type='password' name='password' minlength='1' maxlength='64' required autocomplete='new-password'></label>"
              "<label>Confirm password<input type='password' name='confirm' minlength='1' maxlength='64' required autocomplete='new-password'></label>"
              "<button type='submit'>Save HTTP password</button></form>"
              "</div></details></section>");

    body += F("<section class='card'><h2>Firmware and restart</h2>"
              "<p class='note'>Network OTA is intentionally unavailable on this nRF52 target. Update it through USB/DFU or the modem protocol OTA commands.</p>"
              "<form method='POST' action='/reboot'><button class='secondary' type='submit'>Reboot modem</button></form>"
              "</section></body></html>");
    return body;
}

static String buildStatsJson() {
    RuntimeStats::Snapshot snap = RuntimeStats::capture();
    const auto& cfg = WifiManager::getConfig();
    String clientIP = TCPServer::getClientIP();
    String body;
    body.reserve(900);
    body += F("{");
    body += "\"board\":\"" + jsonEscape(BOARD.name) + "\",";
    body += "\"firmware\":\"" + jsonEscape(snap.firmwareVersion) + "\",";
    body += "\"uptime_sec\":" + String(snap.status.uptime_sec) + ",";
    body += "\"network\":{";
    body += "\"interface\":\"ethernet\",\"ip\":\"" + jsonEscape(currentIPString()) + "\",";
    body += "\"hostname\":\"" + jsonEscape(cfg.hostname) + "\",";
    body += "\"dhcp\":" + String(cfg.useStaticIP ? "false" : "true") + ",";
    body += "\"tcp_port\":" + String(cfg.tcpPort) + ",";
    body += "\"tcp_auth\":" + String(cfg.tcpToken.length() ? "true" : "false") + ",";
    body += "\"tcp_client\":" + (clientIP.length() ? String("\"") + jsonEscape(clientIP) + "\"" : String("null")) + "},";
    body += "\"radio\":{";
    body += "\"standby\":" + String(snap.radioStandby ? "true" : "false") + ",";
    body += "\"rx_count\":" + String(snap.status.rx_count) + ",";
    body += "\"tx_count\":" + String(snap.status.tx_count) + ",";
    body += "\"crc_errors\":" + String(snap.status.crc_errors) + ",";
    body += "\"last_rssi_dbm\":" + String(snap.status.last_rssi) + ",";
    body += "\"last_snr_db\":" + String(snap.status.last_snr / 10.0f, 1) + ",";
    body += "\"noise_floor_dbm\":" + String(snap.status.noise_floor_x10 / 10.0f, 1) + ",";
    body += "\"frequency_hz\":" + String(snap.radio.freq_hz) + ",";
    body += "\"bandwidth_hz\":" + String(snap.radio.bandwidth_hz) + ",";
    body += "\"sf\":" + String(snap.radio.sf) + ",";
    body += "\"cr\":" + String(snap.radio.cr) + ",";
    body += "\"power_dbm\":" + String(snap.radio.power_dbm) + "}}";
    return body;
}

static void queueResponse(int code, const String& contentType, const String& body,
                          const char* extraHeaders = nullptr) {
    responseBuffer = "HTTP/1.1 " + String(code) + " " + statusText(code) + "\r\n";
    responseBuffer += "Content-Type: " + contentType + "\r\n";
    responseBuffer += "Content-Length: " + String(body.length()) + "\r\n";
    responseBuffer += F("Connection: close\r\nCache-Control: no-store\r\n"
                        "X-Content-Type-Options: nosniff\r\n"
                        "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; form-action 'self'\r\n");
    if (extraHeaders) responseBuffer += extraHeaders;
    responseBuffer += F("\r\n");
    responseBuffer += body;
    responseOffset = 0;
    responseReady = true;
}

static void queueText(int code, const String& message) {
    queueResponse(code, "text/plain; charset=utf-8", message);
}

static void queueSimplePage(const String& title, const String& message, bool reboot = false) {
    String body = pageShellStart(title);
    body += "<section class='card'><h1>" + htmlEscape(title) + "</h1><p>" + htmlEscape(message) + "</p>";
    if (!reboot) body += F("<p><a href='/'>Back to management page</a></p>");
    body += F("</section></body></html>");
    queueResponse(200, "text/html; charset=utf-8", body);
    rebootAfterResponse = reboot;
}

static void handleNetworkSave(const String& body) {
    WifiManager::Config cfg = WifiManager::getConfig();
    String host, portText, ipText, subnetText, gatewayText, dns1Text, dns2Text;
    if (!formValue(body, "hostname", host) || !formValue(body, "port", portText) ||
        !formValue(body, "ip", ipText) || !formValue(body, "sn", subnetText) ||
        !formValue(body, "gw", gatewayText) || !formValue(body, "dns1", dns1Text) ||
        !formValue(body, "dns2", dns2Text)) {
        queueText(400, "Malformed form encoding.\n");
        return;
    }
    host.trim();
    if (!validHostname(host)) {
        queueText(400, "Hostname must be 1-32 characters using only letters, digits, and interior hyphens.\n");
        return;
    }
    long port = portText.toInt();
    if (port < 1 || port > 65535) {
        queueText(400, "TCP port must be between 1 and 65535.\n");
        return;
    }

    cfg.hostname = host;
    cfg.tcpPort = (uint16_t)port;
    cfg.useStaticIP = hasFormKey(body, "static");
    if (cfg.useStaticIP) {
        if (!parseIPAddress(ipText, cfg.staticIP) ||
            !parseIPAddress(subnetText, cfg.subnet) ||
            !parseIPAddress(gatewayText, cfg.gateway)) {
            queueText(400, "Static IP, subnet mask, and gateway must be valid non-zero IPv4 addresses.\n");
            return;
        }
        if (dns1Text.length() > 0 && !parseIPAddress(dns1Text, cfg.dns1)) {
            queueText(400, "DNS 1 must be a valid IPv4 address.\n");
            return;
        }
        if (dns2Text.length() > 0 && !parseIPAddress(dns2Text, cfg.dns2)) {
            queueText(400, "DNS 2 must be a valid IPv4 address.\n");
            return;
        }
        if (dns1Text.length() == 0) cfg.dns1 = cfg.gateway;
        if (dns2Text.length() == 0) cfg.dns2 = IPAddress((uint32_t)0);
    }

    if (!WifiManager::saveConfig(cfg)) {
        queueText(500, "Failed to save network settings.\n");
        return;
    }
    queueSimplePage("Network settings saved",
                    "The modem will reboot and apply the new Ethernet and TCP settings.", true);
}

static void handleTokenSave(const String& body) {
    String token, confirm;
    if (!formValue(body, "token", token) || !formValue(body, "confirm", confirm)) {
        queueText(400, "Malformed form encoding.\n");
        return;
    }
    if (token.length() > 64) {
        queueText(400, "openHop token must be 0-64 characters.\n");
        return;
    }
    if (token != confirm) {
        queueText(400, "openHop token confirmation does not match.\n");
        return;
    }
    WifiManager::Config cfg = WifiManager::getConfig();
    cfg.tcpToken = token;
    if (!WifiManager::saveConfig(cfg)) {
        queueText(500, "Failed to save openHop token.\n");
        return;
    }
    queueSimplePage("openHop token saved",
                    token.length() ? "The modem will reboot and require the new token."
                                   : "The modem will reboot with TCP token authentication disabled.",
                    true);
}

static void handleAuthSave(const String& body) {
    String password, confirm;
    if (!formValue(body, "password", password) || !formValue(body, "confirm", confirm)) {
        queueText(400, "Malformed form encoding.\n");
        return;
    }
    if (password.length() == 0 || password.length() > 64) {
        queueText(400, "HTTP password must be 1-64 characters.\n");
        return;
    }
    if (password != confirm) {
        queueText(400, "HTTP password confirmation does not match.\n");
        return;
    }
    if (!NrfSettings::saveHttpPassword(password)) {
        queueText(500, "Failed to save HTTP password.\n");
        return;
    }
    queueSimplePage("HTTP password saved",
                    "Use the new password on the next request.");
}

static void dispatchRequest() {
    const String headers = requestBuffer.substring(0, headerEnd);
    const String body = requestBuffer.substring(headerEnd + 4);
    int firstLineEnd = headers.indexOf("\r\n");
    String firstLine = firstLineEnd >= 0 ? headers.substring(0, firstLineEnd) : headers;
    int firstSpace = firstLine.indexOf(' ');
    int secondSpace = firstSpace >= 0 ? firstLine.indexOf(' ', firstSpace + 1) : -1;
    if (firstSpace <= 0 || secondSpace <= firstSpace + 1) {
        queueText(400, "Malformed HTTP request line.\n");
        return;
    }
    String method = firstLine.substring(0, firstSpace);
    String path = firstLine.substring(firstSpace + 1, secondSpace);
    int query = path.indexOf('?');
    if (query >= 0) path.remove(query);

    if (!isAuthorized(headers)) {
        queueResponse(401, "text/plain; charset=utf-8", "Authentication required.\n",
                      "WWW-Authenticate: Basic realm=\"openHop Modem\", charset=\"UTF-8\"\r\n");
        return;
    }

    if (method == "GET" && path == "/") {
        queueResponse(200, "text/html; charset=utf-8", buildRootPage());
    } else if (method == "GET" && path == "/api/stats") {
        queueResponse(200, "application/json; charset=utf-8", buildStatsJson());
    } else if (method == "POST" && path == "/network") {
        handleNetworkSave(body);
    } else if (method == "POST" && path == "/token") {
        handleTokenSave(body);
    } else if (method == "POST" && path == "/auth") {
        handleAuthSave(body);
    } else if (method == "POST" && path == "/reboot") {
        queueSimplePage("Rebooting", "The modem is restarting now.", true);
    } else if (method != "GET" && method != "POST") {
        queueText(405, "Method not allowed.\n");
    } else {
        queueText(404, "Not found.\n");
    }
}

static void resetClientState() {
    requestBuffer = "";
    headerEnd = 0;
    contentLength = 0;
    responseBuffer = "";
    responseOffset = 0;
    responseReady = false;
    rebootAfterResponse = false;
}

static void closeClient() {
    if (client) client.stop();
    resetClientState();
}

static void acceptClient() {
    if (!server || (client && client.connected())) return;
    if (client) closeClient();
    EthernetClient incoming = server->accept();
    if (!incoming) return;
    IPAddress remote = incoming.remoteIP();
    if (!isLanAddress(remote)) {
        Serial.printf("[HTTP/ETH] rejecting non-LAN client %u.%u.%u.%u\n",
                      remote[0], remote[1], remote[2], remote[3]);
        incoming.stop();
        return;
    }
    client = incoming;
    resetClientState();
    requestBuffer.reserve(1024);
    lastClientActivityMs = millis();
}

static void readRequest() {
    if (!client || !client.connected() || responseReady) return;
    size_t budget = 512;
    while (budget-- > 0 && client.available()) {
        int raw = client.read();
        if (raw < 0) break;
        requestBuffer += (char)raw;
        lastClientActivityMs = millis();
        if (headerEnd == 0) {
            int found = requestBuffer.indexOf("\r\n\r\n");
            if (found >= 0) {
                headerEnd = (size_t)found;
                const String headers = requestBuffer.substring(0, headerEnd);
                if (headerValue(headers, "transfer-encoding").length()) {
                    queueText(400, "Transfer-Encoding is not supported.\n");
                    return;
                }
                const String contentLengthHeader = headerValue(headers, "content-length");
                if (!parseContentLength(contentLengthHeader, contentLength)) {
                    queueText(413, "Invalid or oversized Content-Length.\n");
                    return;
                }
            } else if (requestBuffer.length() > MAX_HEADER_BYTES) {
                queueText(413, "Request headers too large.\n");
                return;
            }
        }
        if (headerEnd > 0 &&
            requestBuffer.length() >= headerEnd + 4 + contentLength) {
            break;
        }
    }

    if (headerEnd > 0) {
        const size_t receivedBody = requestBuffer.length() - (headerEnd + 4);
        if (receivedBody >= contentLength) dispatchRequest();
    }
}

static void writeResponse() {
    if (!responseReady || !client || !client.connected()) return;
    if (responseOffset < responseBuffer.length()) {
        size_t remaining = responseBuffer.length() - responseOffset;
        size_t chunk = remaining > RESPONSE_CHUNK_BYTES ? RESPONSE_CHUNK_BYTES : remaining;
        size_t written = client.write(
            reinterpret_cast<const uint8_t*>(responseBuffer.c_str() + responseOffset), chunk);
        responseOffset += written;
        if (written > 0) lastClientActivityMs = millis();
        return;
    }

    client.flush();
    client.stop();
    responseReady = false;
    responseBuffer = "";
    if (rebootAfterResponse) {
        rebootAtMs = millis() + 250;
        rebootAfterResponse = false;
    } else {
        resetClientState();
    }
}

void begin(const String& requestedHostname, const String&) {
    if (started) return;
    hostname = requestedHostname;
    NrfSettings::begin();
    server = new EthernetServer(HTTP_PORT);
    server->begin();
    requestBuffer.reserve(1024);
    responseBuffer.reserve(4096);
    started = true;
    lastIpUsable = EthernetManager::hasIP();
    Serial.printf("[HTTP/ETH] management UI ready on %s:%u (auth user: %s)\n",
                  currentIPString().c_str(), (unsigned)HTTP_PORT, HTTP_AUTH_USER);
}

void loop() {
    if (!started || !server) return;

    if (rebootAtMs != 0 && (int32_t)(millis() - rebootAtMs) >= 0) {
        NVIC_SystemReset();
        while (true) delay(1000);
    }

    const bool ipUsable = EthernetManager::hasIP();
    if (!ipUsable) {
        if (client) closeClient();
        lastIpUsable = false;
        return;
    }
    if (!lastIpUsable) {
        server->begin();
        lastIpUsable = true;
        Serial.println("[HTTP/ETH] IP restored; listener restarted");
    }

    acceptClient();
    readRequest();
    writeResponse();

    if (client && client.connected() &&
        (uint32_t)(millis() - lastClientActivityMs) > CLIENT_IDLE_TIMEOUT_MS) {
        closeClient();
    }
}

void notifyValidFrame() {}

const char* getHostname() {
    return hostname.c_str();
}

}  // namespace OTAManager

#endif  // PYMC_ETHERNET_W5100S
