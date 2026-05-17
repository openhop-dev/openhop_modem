// =============================================================
// ethernet_manager.cpp — RMII Ethernet bring-up driven by
// BoardConfig::EthernetConfig. No-op when the board doesn't
// enable Ethernet (every board except ESP32-P4-Nano).
// =============================================================
#include "ethernet_manager.h"
#include "board_config.h"

#include "sdkconfig.h"

// Internal EMAC + RMII PHY support is only present on a subset of
// targets (ESP32 original and ESP32-P4 in this project). On chips
// without it (ESP32-S3 etc.) the ETH headers don't define the
// EMAC enums, so we provide stubs and skip the implementation.
#if CONFIG_ETH_USE_ESP32_EMAC

#include <ETH.h>
#include <Network.h>
#include <esp_task_wdt.h>

namespace EthernetManager {

static bool   _started   = false;
static char   _ip_str[20]  = "---";
static char   _mac_str[18] = "";
static String _pending_hostname;
static network_event_handle_t _eth_event_handle = 0;

static void onEthernetEvent(arduino_event_id_t event, arduino_event_info_t) {
    if (event != ARDUINO_EVENT_ETH_START) return;
    if (_pending_hostname.length() == 0) return;
    if (ETH.setHostname(_pending_hostname.c_str())) {
        Serial.printf("[ETH] hostname='%s' applied at ETH_START\n",
                      _pending_hostname.c_str());
    } else {
        Serial.printf("[ETH] failed to apply hostname '%s' at ETH_START\n",
                      _pending_hostname.c_str());
    }
}

static eth_phy_type_t mapPhyType(BoardConfig::EthernetPhy phy) {
    switch (phy) {
        case BoardConfig::EthernetPhy::IP101: return ETH_PHY_IP101;
        case BoardConfig::EthernetPhy::NONE:
        default:                              return ETH_PHY_MAX;
    }
}

#if CONFIG_IDF_TARGET_ESP32P4
// On ESP32-P4 eth_clock_mode_t is emac_rmii_clock_mode_t. For boards
// where the PHY supplies the 50 MHz reference clock (IP101GRI on
// ESP32-P4-Nano) we pick EMAC_CLK_EXT_IN; when the SoC has to drive
// the clock back out we'd use EMAC_CLK_OUT (not used today).
static eth_clock_mode_t pickClockMode(bool rmii_clock_input) {
    return rmii_clock_input ? EMAC_CLK_EXT_IN : EMAC_CLK_OUT;
}
#else
// Classic ESP32 / S3 enum — kept for completeness even though no
// non-P4 board currently has BOARD.ethernet.enabled = true.
static eth_clock_mode_t pickClockMode(bool rmii_clock_input) {
    return rmii_clock_input ? ETH_CLOCK_GPIO0_IN : ETH_CLOCK_GPIO0_OUT;
}
#endif

static void resetPhy(int8_t rst_pin) {
    if (rst_pin < 0) return;
    pinMode(rst_pin, OUTPUT);
    digitalWrite(rst_pin, LOW);
    delay(50);
    digitalWrite(rst_pin, HIGH);
    delay(50);   // datasheet: IP101 needs ≥10 ms after RST deassert
}

void begin(const char* hostname,
           bool useStaticIP,
           const IPAddress& staticIP,
           const IPAddress& gateway,
           const IPAddress& subnet,
           const IPAddress& dns1,
           const IPAddress& dns2) {
    if (!BOARD.ethernet.enabled) return;

    const auto& e = BOARD.ethernet;

    Serial.printf("[ETH] init phy=%u addr=%d mdc=%d mdio=%d rst=%d clk=%s\n",
                  (unsigned)e.phy_type, (int)e.phy_addr,
                  (int)e.pin_mdc, (int)e.pin_mdio, (int)e.pin_phy_reset,
                  e.rmii_clock_input ? "EXT_IN" : "INT_OUT");

    resetPhy(e.pin_phy_reset);

    eth_phy_type_t phy = mapPhyType(e.phy_type);
    if (phy == ETH_PHY_MAX) {
        Serial.println("[ETH] unsupported phy_type — aborting");
        return;
    }

    _pending_hostname = (hostname && hostname[0] != '\0') ? String(hostname) : String();
    if (_eth_event_handle == 0) {
        _eth_event_handle = Network.onEvent(onEthernetEvent, ARDUINO_EVENT_ETH_START);
    }

    // power pin (-1 = none); clock mode mapped per-target above.
    bool ok = ETH.begin(phy,
                        e.phy_addr,
                        e.pin_mdc,
                        e.pin_mdio,
                        /*power=*/-1,
                        pickClockMode(e.rmii_clock_input));
    if (!ok) {
        Serial.println("[ETH] ETH.begin() returned false");
        return;
    }
    _started = true;

    if (_pending_hostname.length() > 0) {
        const char* applied = ETH.getHostname();
        Serial.printf("[ETH] netif hostname now '%s'\n",
                      (applied && applied[0] != '\0') ? applied : "(empty)");
    }

    // Apply hostname/static settings only after ETH.begin(), because
    // the Arduino NetworkInterface requires a live esp_netif first.
    if (useStaticIP) {
        if (ETH.config(staticIP, gateway, subnet, dns1, dns2)) {
            Serial.printf("[ETH] static cfg %u.%u.%u.%u/%u.%u.%u.%u gw=%u.%u.%u.%u\n",
                          staticIP[0], staticIP[1], staticIP[2], staticIP[3],
                          subnet[0], subnet[1], subnet[2], subnet[3],
                          gateway[0], gateway[1], gateway[2], gateway[3]);
        } else {
            Serial.println("[ETH] failed to apply runtime static config");
        }
    } else if (e.use_static_ip) {
        IPAddress ip  (e.static_ip[0], e.static_ip[1], e.static_ip[2], e.static_ip[3]);
        IPAddress gw  (e.gateway[0],   e.gateway[1],   e.gateway[2],   e.gateway[3]);
        IPAddress sn  (e.subnet[0],    e.subnet[1],    e.subnet[2],    e.subnet[3]);
        IPAddress bd1 (e.dns[0],       e.dns[1],       e.dns[2],       e.dns[3]);
        if (ETH.config(ip, gw, sn, bd1)) {
            Serial.printf("[ETH] board static cfg %u.%u.%u.%u/%u.%u.%u.%u gw=%u.%u.%u.%u\n",
                          ip[0], ip[1], ip[2], ip[3],
                          sn[0], sn[1], sn[2], sn[3],
                          gw[0], gw[1], gw[2], gw[3]);
        } else {
            Serial.println("[ETH] failed to apply board static config");
        }
    }

    // Cache MAC for later getMACString() lookups; ETH.macAddress() is
    // valid as soon as begin() returns true.
    String mac = ETH.macAddress();
    strncpy(_mac_str, mac.c_str(), sizeof(_mac_str) - 1);
    _mac_str[sizeof(_mac_str) - 1] = '\0';

    // Wait up to 5 s for link + DHCP. Feed the task watchdog so the
    // 30 s loopTask WDT stays happy if this happens during setup().
    uint32_t t0 = millis();
    while (millis() - t0 < 5000) {
        if (ETH.hasIP()) break;
        delay(100);
        esp_task_wdt_reset();
    }

    if (ETH.hasIP()) {
        IPAddress ip = ETH.localIP();
        snprintf(_ip_str, sizeof(_ip_str), "%u.%u.%u.%u",
                 ip[0], ip[1], ip[2], ip[3]);
        Serial.printf("[ETH] link up, IP=%s mac=%s\n", _ip_str, _mac_str);
    } else if (ETH.linkUp()) {
        Serial.println("[ETH] link up but no DHCP lease yet — will keep trying in background");
    } else {
        Serial.println("[ETH] no link detected after 5 s (cable unplugged?)");
    }
}

void end() {
    if (!_started) return;
    Serial.println("[ETH] tearing down EMAC + netif");
    ETH.end();
    _started = false;
    strncpy(_ip_str, "---", sizeof(_ip_str));
    _mac_str[0] = '\0';
}

void loop() {
    if (!_started) return;
    // ETH.h drives its own event task; we just refresh our cached
    // dotted-quad in case DHCP completed after begin() returned.
    if (ETH.hasIP()) {
        IPAddress ip = ETH.localIP();
        char tmp[20];
        snprintf(tmp, sizeof(tmp), "%u.%u.%u.%u",
                 ip[0], ip[1], ip[2], ip[3]);
        if (strcmp(tmp, _ip_str) != 0) {
            strncpy(_ip_str, tmp, sizeof(_ip_str));
            Serial.printf("[ETH] IP changed: %s\n", _ip_str);
        }
    }
}

bool isLinkUp() {
    return _started && ETH.linkUp();
}

bool hasIP() {
    return _started && ETH.hasIP();
}

const char* getIPString() {
    return _ip_str;
}

const char* getMACString() {
    return _mac_str;
}

} // namespace EthernetManager

#else  // !CONFIG_ETH_USE_ESP32_EMAC — chip has no internal EMAC

namespace EthernetManager {
void        begin(const char*, bool, const IPAddress&, const IPAddress&, const IPAddress&,
                  const IPAddress&, const IPAddress&) {}
void        end()          {}
void        loop()         {}
bool        isLinkUp()     { return false; }
bool        hasIP()        { return false; }
const char* getIPString()  { return "---"; }
const char* getMACString() { return ""; }
} // namespace EthernetManager

#endif // CONFIG_ETH_USE_ESP32_EMAC
