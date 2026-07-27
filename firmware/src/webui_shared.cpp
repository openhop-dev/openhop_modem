#include "webui_shared.h"

#include <cstdio>
#include <type_traits>

namespace WebUiShared {
namespace {

std::string quote(const std::string& value) { return "\"" + jsonEscape(value) + "\""; }
std::string boolean(bool value) { return value ? "true" : "false"; }
std::string nullableString(const std::string& value) { return value.empty() ? "null" : quote(value); }

template <typename T>
std::string number(T value) {
    char buffer[32];
    if constexpr (std::is_signed<T>::value) {
        std::snprintf(buffer, sizeof(buffer), "%lld",
                      static_cast<long long>(value));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llu",
                      static_cast<unsigned long long>(value));
    }
    return buffer;
}

std::string fixed(double value, int precision) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
    return buffer;
}

std::string uptime(uint32_t seconds) {
    const uint32_t days = seconds / 86400;
    const uint32_t hours = (seconds % 86400) / 3600;
    const uint32_t minutes = (seconds % 3600) / 60;
    const uint32_t secs = seconds % 60;
    char buffer[40];
    if (days) {
        std::snprintf(buffer, sizeof(buffer), "%lud %02lu:%02lu:%02lu",
                      static_cast<unsigned long>(days),
                      static_cast<unsigned long>(hours),
                      static_cast<unsigned long>(minutes),
                      static_cast<unsigned long>(secs));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu",
                      static_cast<unsigned long>(hours),
                      static_cast<unsigned long>(minutes),
                      static_cast<unsigned long>(secs));
    }
    return buffer;
}

std::string hexByte(uint8_t value) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%x", static_cast<unsigned>(value));
    return buffer;
}

std::string preferredIp(bool useStatic, const std::string& saved, const std::string& live) {
    if (useStatic && !saved.empty()) return saved;
    if (!useStatic && !live.empty()) return live;
    return saved;
}

std::string display(const std::string& value, const char* unavailable = "unknown") {
    return value.empty() ? unavailable : htmlEscape(value);
}

void appendKv(std::string& body, const char* key, const std::string& value) {
    body += "<div class='kv'><span class='k'>";
    body += key;
    body += "</span><span class='v'>";
    body += value;
    body += "</span></div>";
}

}  // namespace

std::string htmlEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += static_cast<char>(c); break;
        }
    }
    return out;
}

std::string jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    static constexpr char HEX[] = "0123456789abcdef";
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += HEX[c >> 4];
                    out += HEX[c & 0x0f];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string renderSetupPage(const SetupModel& m) {
    std::string body;
    body.reserve(6144);
    body += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>openHop Modem Setup</title><style>body{font-family:system-ui,sans-serif;max-width:480px;margin:1em auto;padding:0 1em;color:#222}h1{font-size:1.3em}label{display:block;margin-top:1em;font-weight:600}input,select{width:100%;padding:.5em;box-sizing:border-box;font-size:1em}input[type=checkbox]{width:auto;margin-right:.5em}button{margin-top:1.5em;padding:.75em;width:100%;background:#3a7;color:#fff;border:0;font-size:1em;border-radius:4px}.hint{color:#666;font-size:.85em;font-weight:400}hr{margin:2em 0;border:0;border-top:1px solid #ddd}</style></head><body><h1>openHop Modem Setup</h1><form method='POST' action='/save'><label>Wi-Fi SSID</label><select name='ssid'><option value=''>-- select --</option>";
    for (const auto& network : m.networks) {
        const std::string ssid = htmlEscape(network.ssid);
        body += "<option value='" + ssid + "'";
        if (network.ssid == m.savedSsid) body += " selected";
        body += ">" + ssid + " (" + number(network.rssiDbm) + " dBm)</option>";
    }
    body += "</select><label>or manual SSID <span class='hint'>(overrides dropdown)</span></label><input type='text' name='ssid_manual' autocomplete='off'><label>Wi-Fi password</label><input type='password' name='password' value='" + htmlEscape(m.password) + "'>";
    if (m.wifiAntennaSelection) {
        body += "<label><input type='checkbox' name='wifi_ant_ext' value='1'";
        if (m.wifiExternalAntenna) body += " checked";
        body += "> Use external Wi-Fi antenna</label>";
    }
    if (m.heltecV43Controls) {
        body += "<label><input type='checkbox' name='v43_lna_on' value='1'";
        if (m.heltecV43ExternalLnaEnabled) body += " checked";
        body += "> Enable Heltec V4.3 external FEM RX LNA <span class='hint'>(RX only; TX always bypasses LNA)</span></label><label>agc.reset.interval seconds <span class='hint'>(0 disables; periodically resets AGC during long idle periods)</span><input name='agc_reset_interval_sec' type='number' min='0' max='3600' step='1' value='" + number(m.agcResetIntervalSec) + "'></label>";
    }
    body += "<label><input type='checkbox' name='static' value='1'";
    if (m.useStaticIp) body += " checked";
    body += "> Use static IP (otherwise DHCP)</label><label>Static IP</label><input type='text' name='ip' value='" + htmlEscape(m.staticIp) + "' placeholder='192.168.1.42'><label>Gateway</label><input type='text' name='gw' value='" + htmlEscape(m.gateway) + "' placeholder='192.168.1.1'><label>Subnet mask</label><input type='text' name='sn' value='" + htmlEscape(m.subnet) + "' placeholder='255.255.255.0'><label>DNS 1</label><input type='text' name='dns1' value='" + htmlEscape(m.dns1) + "' placeholder='1.1.1.1'><label>DNS 2</label><input type='text' name='dns2' value='" + htmlEscape(m.dns2) + "' placeholder='8.8.8.8'><hr><label>Hostname <span class='hint'>(optional; blank = default mDNS name)</span></label><input type='text' name='hostname' autocomplete='off' maxlength='32' value='" + htmlEscape(m.hostname) + "' placeholder='ethermesh-1w'><label>TCP port</label><input type='number' name='port' min='1' max='65535' value='" + number(m.tcpPort) + "'><label>TCP auth token <span class='hint'>(optional; empty = no auth)</span></label><input type='text' name='token' autocomplete='off' value='" + htmlEscape(m.tcpToken) + "'><button type='submit'>Save &amp; Restart</button></form></body></html>";
    return body;
}

std::string renderRootPage(const Model& m) {
    const std::string title = htmlEscape(m.board + " openHop Modem");
    const std::string ip = display(m.network.currentIp);
    const std::string host = htmlEscape(m.hostname);
    const std::string ipValue = htmlEscape(preferredIp(m.config.useStaticIp, m.config.staticIp, m.network.currentIp));
    const std::string subnetValue = htmlEscape(preferredIp(m.config.useStaticIp, m.config.subnet, m.network.subnet));
    const std::string gatewayValue = htmlEscape(preferredIp(m.config.useStaticIp, m.config.gateway, m.network.gateway));
    const std::string dns1Value = htmlEscape(preferredIp(m.config.useStaticIp, m.config.dns1, m.network.dns1));
    const std::string dns2Value = htmlEscape(preferredIp(m.config.useStaticIp, m.config.dns2, m.network.dns2));
    std::string leaseHint;
    if (m.config.useStaticIp) leaseHint = "Static mode is saved. These values are what the modem will use after reboot.";
    else if (m.network.live) leaseHint = "DHCP is active. These fields show the live lease from " + htmlEscape(m.network.interfaceName) + ".";
    else leaseHint = "DHCP is active. No live lease is available yet, so the fields are blank.";

    std::string body;
    body.reserve(9000);
    body += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>" + title + "</title><style>body{font-family:system-ui,sans-serif;max-width:760px;margin:1.25em auto;padding:0 1em;color:#222;line-height:1.45}h2{margin:.2em 0 .35em}p{margin:.45em 0}.m{color:#666;font-size:.92em}.summary{background:#f7f7f7;border:1px solid #ddd;border-radius:8px;padding:.8em 1em;margin:1em 0 1.2em}.summary strong{display:inline-block;min-width:4.5em}.chips{margin-top:.55em}.chip{display:inline-block;background:#efefef;border:1px solid #ddd;border-radius:999px;padding:.2em .6em;margin:0 .35em .35em 0;font-size:.9em}details{border:1px solid #ddd;border-radius:8px;padding:.65em .8em;margin:0 0 .9em;background:#fff}summary{cursor:pointer;font-weight:700;list-style:none}summary::-webkit-details-marker{display:none}.inside{margin-top:.8em}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:.75em 1em}label{display:block;margin-top:.8em;font-weight:600}input{width:100%;padding:.55em;box-sizing:border-box;font-size:1em;border:1px solid #ccc;border-radius:6px}input[type=file]{padding:.3em 0;border:0}input[type=checkbox]{width:auto;margin-right:.45em}.checkline{display:flex;align-items:center;gap:.35em;margin-top:.8em}button{margin-top:.9em;padding:.6em 1em;background:#2f6f5e;color:#fff;border:0;border-radius:6px;cursor:pointer}button.danger{background:#b3261e}button:disabled{background:#999;cursor:not-allowed}code{font-family:ui-monospace,SFMono-Regular,monospace;background:#f3f3f3;padding:.1em .35em;border-radius:4px}@media (max-width:640px){body{padding:0 .75em}}</style></head><body><h2>" + title + "</h2><div class='summary'>";
    if (m.capabilities.mdns) body += "<p><strong>mDNS</strong> " + host + ".local</p>";
    else body += "<p><strong>Hostname</strong> " + host + "</p>";
    body += "<p><strong>IP</strong> " + ip + "</p><p><strong>Interface</strong> " + htmlEscape(m.network.interfaceName) + "</p><p><strong>Current connection</strong> " + (m.connectedClientIp.empty() ? "none" : htmlEscape(m.connectedClientIp)) + "</p>";
    if (m.capabilities.ethernet) {
        body += "<p><strong>Link</strong> " + display(m.network.linkState) + "</p><p><strong>MAC</strong> " + display(m.network.mac) + "</p><p><strong>TCP status</strong> " + display(m.network.tcpStatus) + "</p>";
    }
    body += "<div class='chips'><span class='chip'>" + std::string(m.config.tcpTokenSet ? "openHop protected" : "openHop open") + "</span><span class='chip'>" + std::string(m.config.useStaticIp ? "Static network saved" : "DHCP mode") + "</span></div><p class='m'><a href='/stats'>View stats page</a></p></div>";

    if (m.capabilities.httpFirmwareUpload && m.capabilities.updateAvailable) {
        body += "<details open><summary>OTA Update</summary><div class='inside'><p>Upload an app-only <code>firmware.bin</code> over the LAN.</p><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='firmware' accept='.bin' required><br><button type='submit'>Upload firmware.bin</button></form><p class='m'>CLI alternative: <code>curl -u admin:&lt;password&gt; -F firmware=@firmware.bin http://" + host + (m.capabilities.mdns ? ".local" : "") + "/update</code></p></div></details>";
    } else {
        body += "<details open><summary>Ethernet OTA unavailable</summary><div class='inside'><p>" + htmlEscape(m.updateUnavailableReason.empty() ? "Firmware update is unavailable on this platform." : m.updateUnavailableReason) + "</p><button type='button' disabled>Firmware update unavailable</button></div></details>";
    }

    if (!m.capabilities.writableManagement && m.capabilities.ethernet) {
        body += "<details open><summary>Network Status</summary><div class='inside'><div class='grid'>";
        appendKv(body, "Mode", m.config.useStaticIp ? "Static" : "DHCP");
        appendKv(body, "IP", display(m.network.currentIp));
        appendKv(body, "Subnet", display(m.network.subnet));
        appendKv(body, "Gateway", display(m.network.gateway));
        appendKv(body, "DNS 1", display(m.network.dns1, "none"));
        appendKv(body, "DNS 2", display(m.network.dns2, "none"));
        body += "</div></div></details>";
    }
    if (m.capabilities.writableManagement) {
    body += "<details><summary>Hostname</summary><div class='inside'><p>Controls the " + std::string(m.capabilities.mdns ? "mDNS / OTA hostname the modem advertises on the network." : "hostname shown by the modem on the network.") + "</p><form method='POST' action='/hostname'><label>" + std::string(m.capabilities.mdns ? "mDNS / OTA hostname" : "Hostname") + "</label><input type='text' name='hostname' autocomplete='off' maxlength='64' value='" + htmlEscape(m.config.hostname) + "' placeholder='leave blank for default'><button type='submit'>Save hostname</button></form><p class='m'>Blank resets to the board default. Reboot required.</p></div></details>";
    body += "<details><summary>Network</summary><div class='inside'><p>" + leaseHint + "</p><form method='POST' action='/network'><div class='checkline'><input type='checkbox' id='static' name='static' value='1'" + std::string(m.config.useStaticIp ? " checked" : "") + "><label for='static'>Use static IP instead of DHCP</label></div><div class='grid'><div><label>Static IP</label><input type='text' name='ip' value='" + ipValue + "' placeholder='192.168.1.42'></div><div><label>Subnet mask</label><input type='text' name='sn' value='" + subnetValue + "' placeholder='255.255.255.0'></div><div><label>Gateway</label><input type='text' name='gw' value='" + gatewayValue + "' placeholder='192.168.1.1'></div><div><label>DNS 1</label><input type='text' name='dns1' value='" + dns1Value + "' placeholder='1.1.1.1'></div><div><label>DNS 2</label><input type='text' name='dns2' value='" + dns2Value + "' placeholder='8.8.8.8'></div><div><label>Current source</label><div class='chip'>" + htmlEscape(m.network.interfaceName) + "</div></div></div>";
    if (m.capabilities.ethernet) {
        body += "<div class='grid'><div><label>Link</label><div class='chip'>" + display(m.network.linkState) + "</div></div><div><label>MAC</label><div class='chip'>" + display(m.network.mac) + "</div></div><div><label>TCP status</label><div class='chip'>" + display(m.network.tcpStatus) + "</div></div></div>";
    }
    if (m.capabilities.wifiAntennaSelection) {
        body += "<div class='checkline'><input type='checkbox' id='wifi_ant_ext' name='wifi_ant_ext' value='1'" + std::string(m.config.wifiExternalAntenna ? " checked" : "") + "><label for='wifi_ant_ext'>Use external Wi-Fi antenna</label></div>";
    }
    body += "<label>openHop TCP port</label><input type='number' name='port' min='1' max='65535' value='" + number(m.config.tcpPort) + "'><p class='m'>Port 80 is reserved for this management server.</p><button type='submit'>Save network settings</button></form></div></details>";
    }
    if (m.capabilities.heltecV43Controls) {
        body += "<details open><summary>Heltec V4.3 RF Front-End</summary><div class='inside'><p>Toggle the KCT8103L external RX LNA for receive only. The firmware always bypasses the FEM LNA during transmit so the TX path remains available.</p><form method='POST' action='/rf-lna'><div class='checkline'><input type='checkbox' id='v43_lna_on' name='v43_lna_on' value='1'" + std::string(m.config.heltecV43ExternalLnaEnabled ? " checked" : "") + "><label for='v43_lna_on'>Enable external FEM RX LNA</label></div><label>agc.reset.interval (seconds, 0 disables)<input name='agc_reset_interval_sec' type='number' min='0' max='3600' step='1' value='" + number(m.config.agcResetIntervalSec) + "'></label><p class='m'>Periodically restarts RX gain control during long idle periods to prevent strong out-of-band interference from clamping the noise floor.</p><button type='submit'>Save RF front-end settings</button></form><p class='m'>Settings apply immediately and persist across reboots. Unchecked LNA = GPIO5/CTX HIGH, external LNA bypassed.</p></div></details>";
    }
    if (m.capabilities.gps && m.capabilities.writableManagement) {
        body += "<details open><summary>GPS</summary><div class='inside'><p>Turn the onboard GPS receiver interface on only when location data is needed. Default is off to save battery.</p><form method='POST' action='/gps'><div class='checkline'><input type='checkbox' id='gps_enabled' name='gps_enabled' value='1'" + std::string(m.config.gpsEnabled ? " checked" : "") + "><label for='gps_enabled'>Enable GPS</label></div><button type='submit'>Save GPS setting</button></form><p class='m'>Saved atomically and applied after reboot.</p></div></details>";
    }
    if (m.capabilities.ethernet && (m.capabilities.battery || m.capabilities.gps)) {
        body += "<details open><summary>Device Status</summary><div class='inside'><div class='grid'>";
        if (m.capabilities.battery) {
            appendKv(body, "Battery", m.battery.voltageValid
                ? fixed(m.battery.voltageMv / 1000.0, 3) + " V" : "unknown");
        }
        if (m.capabilities.gps) {
            appendKv(body, "GPS fix", !m.gps.enabled ? "disabled" :
                (m.gps.fixValid ? "valid" : (m.gps.seen ? "no fix" : "waiting")));
        }
        body += "</div></div></details>";
    }
    if (m.capabilities.writableManagement) {
    body += "<details><summary>openHop Token</summary><div class='inside'><p>This token must match the <code>token</code> value in openHop so openHop can connect to the radio.</p><form method='POST' action='/token'><label>New openHop token</label><input type='password' name='token' autocomplete='new-password' maxlength='64'><label>Confirm openHop token</label><input type='password' name='confirm' autocomplete='new-password' maxlength='64'><button type='submit'>Save openHop token</button></form><p class='m'>Current mode: <span class='chip'>" + std::string(m.config.tcpTokenSet ? "Protected" : "Open") + "</span>. Leave both fields blank to clear it. Reboot required.</p></div></details><details><summary>HTTP Password</summary><div class='inside'><p>Protects this web page and management actions. Username: <code>admin</code>.</p><form method='POST' action='/auth'><label>New password</label><input type='password' name='password' autocomplete='new-password' required minlength='1' maxlength='64'><label>Confirm password</label><input type='password' name='confirm' autocomplete='new-password' required minlength='1' maxlength='64'><button type='submit'>Save password</button></form><p class='m'>Password changes take effect on the next request.</p></div></details>";
    if (m.capabilities.wifiReset) body += "<details><summary>Wi-Fi Setup Mode</summary><div class='inside'><p>Clear the saved modem configuration and reboot into the open <code>openHop-Modem-XXXX</code> setup AP.</p><form method='POST' action='/wifi-reset' onsubmit=\"return confirm('Clear saved modem configuration and reboot into Wi-Fi setup AP?');\"><button class='danger' type='submit'>Enter Wi-Fi Setup Mode</button></form><p class='m'>Use this before moving the modem to a different Wi-Fi network.</p></div></details>";
    body += "<details><summary>Reboot</summary><div class='inside'><p>Restart the modem without changing any settings.</p><form method='POST' action='/reboot'><button type='submit'>Reboot modem</button></form></div></details>";
    }
    if (m.capabilities.bleDfu) {
        body += "<details><summary>BLE DFU Recovery</summary><div class='inside'><p>Enter the installed Nordic BLE OTA bootloader. Its BLE name is defined by the installed bootloader.</p><form method='POST' action='/dfu/ble' onsubmit=\"return confirm('Enter BLE DFU recovery? Ethernet and port 5055 will disconnect until DFU completes or the device resets.');\"><button class='danger' type='submit'>Enter BLE DFU</button></form><p class='m'>No firmware data is uploaded by this action.</p></div></details>";
    }
    body += "</body></html>";
    return body;
}

std::string renderStatsPage(const Model& m) {
    std::string body;
    body.reserve(7000);
    body += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><meta http-equiv='refresh' content='5'><title>Modem Stats</title><style>body{font-family:system-ui,sans-serif;max-width:760px;margin:1.25em auto;padding:0 1em;color:#222;line-height:1.45}h2{margin:.2em 0 .35em}h3{margin:1.2em 0 .45em}.m{color:#666;font-size:.92em}.card{background:#f7f7f7;border:1px solid #ddd;border-radius:8px;padding:.8em 1em;margin:1em 0}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:.75em 1em}.kv{border:1px solid #ddd;border-radius:8px;background:#fff;padding:.7em .8em}.k{display:block;color:#666;font-size:.9em;margin-bottom:.2em}.v{font-weight:600}.actions{margin:1em 0}.actions a{margin-right:1em}code{font-family:ui-monospace,SFMono-Regular,monospace;background:#f3f3f3;padding:.1em .35em;border-radius:4px}@media (max-width:640px){body{padding:0 .75em}}</style></head><body><h2>" + htmlEscape(m.board + " openHop Modem") + " Stats</h2><div class='actions'><a href='/'>Back to main page</a></div><p class='m'>Auto-refreshes every 5 seconds.</p><div class='card'><div class='grid'>";
    appendKv(body, "Firmware", htmlEscape(m.firmware));
    appendKv(body, "Hostname", htmlEscape(m.hostname) + (m.capabilities.mdns ? ".local" : ""));
    appendKv(body, "Current IP", display(m.network.currentIp));
    appendKv(body, "Connected client", m.connectedClientIp.empty() ? "none" : htmlEscape(m.connectedClientIp));
    appendKv(body, "Interface", htmlEscape(m.network.interfaceName));
    if (m.network.hasWifiRssi) appendKv(body, "Wi-Fi signal", number(m.network.wifiRssiDbm) + " dBm");
    if (m.capabilities.ethernet) {
        appendKv(body, "Ethernet link", display(m.network.linkState));
        appendKv(body, "MAC", display(m.network.mac));
        appendKv(body, "TCP status", display(m.network.tcpStatus));
    }
    appendKv(body, "Uptime", uptime(m.uptimeSec));
    if (m.capabilities.battery) appendKv(body, "Battery", m.battery.voltageValid ? fixed(m.battery.voltageMv / 1000.0, 3) + " V" : "unknown");
    if (m.battery.chargeRateAvailable) appendKv(body, "Battery charge rate", m.battery.chargeRateValid ? fixed(m.battery.chargeRatePctPerHour, 3) + " %/hr" : "unknown");
    if (m.capabilities.gps && m.gps.enabled) {
        appendKv(body, "GPS fix", m.gps.fixValid ? "valid" : (m.gps.seen ? "no fix" : "waiting"));
        if (m.gps.fixValid) appendKv(body, "GPS location", fixed(m.gps.latitude, 6) + ", " + fixed(m.gps.longitude, 6));
    }
    body += "</div></div>";
    if (m.capabilities.radio && m.radio.available) {
        body += "<h3>Radio</h3><div class='grid'>";
        appendKv(body, "State", htmlEscape(m.radio.state));
        appendKv(body, "Frequency", fixed(m.radio.frequencyHz / 1000000.0, 3) + " MHz");
        appendKv(body, "Bandwidth", fixed(m.radio.bandwidthHz / 1000.0, 1) + " kHz");
        appendKv(body, "Spreading factor", "SF" + number(m.radio.spreadingFactor));
        appendKv(body, "Coding rate", "4/" + number(m.radio.codingRate));
        appendKv(body, "TX power", number(m.radio.txPowerDbm) + " dBm");
        appendKv(body, "Syncword", hexByte(m.radio.syncword));
        appendKv(body, "Preamble", number(m.radio.preambleLength));
        appendKv(body, "Auto CAD", m.radio.autoCadEnabled ? "On" : "Off");
        body += "</div>";
    }
    body += "<h3>Counters</h3><div class='grid'>";
    appendKv(body, "RX packets", number(m.counters.rxPackets)); appendKv(body, "TX packets", number(m.counters.txPackets)); appendKv(body, "CRC errors", number(m.counters.crcErrors)); appendKv(body, "Last RSSI", number(m.counters.lastRssiDbm) + " dBm"); appendKv(body, "Last SNR", fixed(m.counters.lastSnrDb, 1) + " dB"); appendKv(body, "Noise floor", fixed(m.counters.noiseFloorDbm, 1) + " dBm"); appendKv(body, "Die temperature", number(m.dieTemperatureC) + " C");
    body += "</div><h3>Network</h3><div class='grid'>";
    appendKv(body, "Mode", m.config.useStaticIp ? "Static" : "DHCP"); appendKv(body, "Port", number(m.config.tcpPort));
    if (m.network.hasWifiRssi) appendKv(body, "Wi-Fi RSSI", number(m.network.wifiRssiDbm) + " dBm");
    if (m.capabilities.wifiAntennaSelection) appendKv(body, "Wi-Fi antenna", m.config.wifiExternalAntenna ? "External" : "Internal");
    if (m.capabilities.heltecV43Controls) appendKv(body, "Heltec V4.3 external LNA", m.config.heltecV43ExternalLnaEnabled ? "Enabled" : "Bypassed");
    appendKv(body, "Gateway", display(m.network.gateway, "none")); appendKv(body, "Subnet", display(m.network.subnet, "none")); appendKv(body, "DNS 1", display(m.network.dns1, "none")); appendKv(body, "DNS 2", display(m.network.dns2, "none"));
    body += "</div></body></html>";
    return body;
}

std::string renderSystemJson(const Model& m) {
    std::string out = "{\"board\":" + quote(m.board) + ",\"firmware\":" + quote(m.firmware) + ",\"hostname\":" + quote(m.hostname);
    out += ",\"mdns\":" + (m.capabilities.mdns ? quote(m.hostname + ".local") : std::string("null"));
    out += ",\"interface\":" + quote(m.network.interfaceName) + ",\"current_ip\":" + quote(m.network.currentIp) + ",\"connected_client_ip\":" + nullableString(m.connectedClientIp) + ",\"uptime_sec\":" + number(m.uptimeSec) + ",\"uptime\":" + quote(uptime(m.uptimeSec)) + ",\"die_temperature_c\":" + number(m.dieTemperatureC) + ",\"battery_voltage_mv\":" + (m.battery.voltageValid ? number(m.battery.voltageMv) : "null") + ",\"battery_voltage_v\":" + (m.battery.voltageValid ? fixed(m.battery.voltageMv / 1000.0, 3) : "null");
    if (m.battery.chargeRateAvailable) out += ",\"battery_charge_rate_pct_per_hour\":" + (m.battery.chargeRateValid ? fixed(m.battery.chargeRatePctPerHour, 3) : "null");
    return out + "}";
}

std::string renderRadioJson(const Model& m) {
    std::string out = "{\"state\":" + quote(m.radio.state) + ",\"standby\":" + boolean(m.radio.standby) + ",\"auto_cad_enabled\":" + boolean(m.radio.autoCadEnabled) + ",\"frequency_hz\":" + number(m.radio.frequencyHz) + ",\"frequency_mhz\":" + fixed(m.radio.frequencyHz / 1000000.0, 3) + ",\"bandwidth_hz\":" + number(m.radio.bandwidthHz) + ",\"bandwidth_khz\":" + fixed(m.radio.bandwidthHz / 1000.0, 1) + ",\"spreading_factor\":" + number(m.radio.spreadingFactor) + ",\"coding_rate\":" + number(m.radio.codingRate) + ",\"tx_power_dbm\":" + number(m.radio.txPowerDbm) + ",\"syncword\":" + quote(hexByte(m.radio.syncword)) + ",\"syncword_value\":" + number(m.radio.syncword) + ",\"preamble_len\":" + number(m.radio.preambleLength);
    if (m.capabilities.heltecV43Controls) out += ",\"heltec_v43_external_lna_enabled\":" + boolean(m.config.heltecV43ExternalLnaEnabled) + ",\"heltec_v43_fem_lna_bypassed\":" + boolean(m.config.heltecV43FemLnaBypassed) + ",\"agc_reset_interval_sec\":" + number(m.config.agcResetIntervalSec);
    return out + "}";
}

std::string renderCountersJson(const Model& m) {
    return "{\"rx_packets\":" + number(m.counters.rxPackets) + ",\"tx_packets\":" + number(m.counters.txPackets) + ",\"crc_errors\":" + number(m.counters.crcErrors) + ",\"last_rssi_dbm\":" + number(m.counters.lastRssiDbm) + ",\"last_snr_db\":" + fixed(m.counters.lastSnrDb, 1) + ",\"noise_floor_dbm\":" + fixed(m.counters.noiseFloorDbm, 1) + "}";
}

std::string renderNetworkJson(const Model& m) {
    std::string out = "{\"mode\":" + quote(m.config.useStaticIp ? "static" : "dhcp") + ",\"use_static_ip\":" + boolean(m.config.useStaticIp) + ",\"interface\":" + quote(m.network.interfaceName) + ",\"live\":" + boolean(m.network.live) + ",\"current_ip\":" + quote(m.network.currentIp) + ",\"subnet\":" + nullableString(m.network.subnet) + ",\"gateway\":" + nullableString(m.network.gateway) + ",\"dns1\":" + nullableString(m.network.dns1) + ",\"dns2\":" + nullableString(m.network.dns2);
    if (m.network.hasWifiRssi) out += ",\"wifi_rssi_dbm\":" + number(m.network.wifiRssiDbm);
    if (m.capabilities.ethernet) out += ",\"link\":" + nullableString(m.network.linkState) + ",\"mac\":" + nullableString(m.network.mac) + ",\"tcp_status\":" + nullableString(m.network.tcpStatus);
    out += ",\"tcp_port\":" + number(m.config.tcpPort) + ",\"pymc_token_set\":" + boolean(m.config.tcpTokenSet) + ",\"saved\":{\"static_ip\":" + nullableString(m.config.staticIp) + ",\"subnet\":" + nullableString(m.config.subnet) + ",\"gateway\":" + nullableString(m.config.gateway) + ",\"dns1\":" + nullableString(m.config.dns1) + ",\"dns2\":" + nullableString(m.config.dns2) + "}}";
    return out;
}

std::string renderConfigJson(const Model& m) {
    std::string out = "{\"hostname\":" + quote(m.config.hostname) + ",\"effective_hostname\":" + quote(m.hostname);
    if (m.capabilities.exposeTcpToken) out += ",\"tcp_token\":" + quote(m.config.tcpToken);
    else out += ",\"tcp_token_set\":" + boolean(m.config.tcpTokenSet);
    out += ",\"tcp_port\":" + number(m.config.tcpPort) + ",\"use_static_ip\":" + boolean(m.config.useStaticIp) + ",\"static_ip\":" + nullableString(m.config.staticIp) + ",\"subnet\":" + nullableString(m.config.subnet) + ",\"gateway\":" + nullableString(m.config.gateway) + ",\"dns1\":" + nullableString(m.config.dns1) + ",\"dns2\":" + nullableString(m.config.dns2);
    if (m.capabilities.wifiAntennaSelection) out += ",\"wifi_external_antenna\":" + boolean(m.config.wifiExternalAntenna);
    if (m.capabilities.heltecV43Controls) out += ",\"heltec_v43_external_lna_enabled\":" + boolean(m.config.heltecV43ExternalLnaEnabled) + ",\"heltec_v43_fem_lna_bypassed\":" + boolean(m.config.heltecV43FemLnaBypassed) + ",\"agc_reset_interval_sec\":" + number(m.config.agcResetIntervalSec);
    out += ",\"gps_enabled\":" + boolean(m.config.gpsEnabled) + ",\"gps_available\":" + boolean(m.capabilities.gps) + "}";
    return out;
}

std::string renderGpsJson(const Model& m) {
    const auto& g = m.gps;
    std::string out = "{\"enabled\":" + boolean(g.enabled) + ",\"available\":" + boolean(g.available) + ",\"seen\":" + boolean(g.seen) + ",\"fix\":{\"valid\":" + boolean(g.fixValid) + ",\"quality\":" + number(g.fixQuality) + "},\"position\":{\"latitude\":" + (g.fixValid ? fixed(g.latitude, 6) : "null") + ",\"longitude\":" + (g.fixValid ? fixed(g.longitude, 6) : "null") + ",\"altitude_m\":" + (g.altitudeValid ? fixed(g.altitudeM, 1) : "null") + "},\"satellites\":{\"used_count\":" + number(g.satellitesUsed) + ",\"in_view_count\":" + number(g.satellitesInView) + ",\"in_view\":[";
    for (size_t i = 0; i < g.satellites.size(); ++i) {
        if (i) out += ',';
        const auto& sat = g.satellites[i];
        out += "{\"prn\":" + quote(sat.prn) + ",\"elevation_degrees\":" + (sat.elevationValid ? number(sat.elevationDegrees) : "null") + ",\"azimuth_degrees\":" + (sat.azimuthValid ? number(sat.azimuthDegrees) : "null") + ",\"snr_db\":" + (sat.snrValid ? fixed(sat.snrDb, 1) : "null") + "}";
    }
    out += "],\"time\":{\"utc_time\":" + nullableString(g.utcTime) + ",\"date\":" + nullableString(g.date) + ",\"datetime_utc\":" + nullableString(g.datetimeUtc) + "},\"motion\":{\"speed_kmh\":" + (g.speedValid ? fixed(g.speedKmh, 2) : "null") + ",\"course_degrees\":" + (g.courseValid ? fixed(g.courseDegrees, 2) : "null") + "},\"nmea\":{\"last_sentence_type\":" + nullableString(g.lastSentenceType) + ",\"valid_sentence_count\":" + number(g.validSentenceCount) + ",\"invalid_checksum_count\":" + number(g.invalidChecksumCount) + ",\"raw_byte_count\":" + number(g.rawByteCount) + ",\"config_command_count\":" + number(g.configCommandCount) + ",\"uart_rx_pin\":" + number(g.uartRxPin) + ",\"uart_tx_pin\":" + number(g.uartTxPin) + ",\"uart_baud\":" + number(g.uartBaud) + ",\"enable_pin\":" + number(g.enablePin) + ",\"reset_pin\":" + number(g.resetPin) + ",\"age_ms\":" + (g.ageValid ? number(g.ageMs) : "null") + "}}";
    return out;
}

std::string renderStatsJson(const Model& m) {
    std::string out = "{\"battery_voltage_mv\":" + (m.battery.voltageValid ? number(m.battery.voltageMv) : "null") + ",\"battery_voltage_v\":" + (m.battery.voltageValid ? fixed(m.battery.voltageMv / 1000.0, 3) : "null");
    if (m.battery.chargeRateAvailable) out += ",\"solar_charge_rate_percent_per_hour\":" + (m.battery.chargeRateValid ? fixed(m.battery.chargeRatePctPerHour, 3) : "null");
    out += ",\"system\":" + renderSystemJson(m) + ",\"radio\":" + renderRadioJson(m) + ",\"counters\":" + renderCountersJson(m) + ",\"network\":" + renderNetworkJson(m) + ",\"gps\":" + renderGpsJson(m) + "}";
    return out;
}

std::string renderTempJson(const Model& m) {
    std::string out = "{\"die_temperature_c\":" + number(m.dieTemperatureC) + ",\"battery_voltage_mv\":" + (m.battery.voltageValid ? number(m.battery.voltageMv) : "null") + ",\"battery_voltage_v\":" + (m.battery.voltageValid ? fixed(m.battery.voltageMv / 1000.0, 3) : "null");
    if (m.battery.chargeRateAvailable) out += ",\"battery_charge_rate_pct_per_hour\":" + (m.battery.chargeRateValid ? fixed(m.battery.chargeRatePctPerHour, 3) : "null");
    return out + ",\"firmware\":" + quote(m.firmware) + ",\"hostname\":" + quote(m.hostname) + "}";
}

}  // namespace WebUiShared
