// =============================================================
// w5100s_ethernet_transport.cpp — W5100S TCP transport for
// RAK4631 + RAK13800 / WisMesh Ethernet Gateway.
//
// This is deliberately protocol-compatible with the existing ESP32
// tcp_server.cpp: one client, optional shared-token AUTH, same pyMC
// binary frame format, same TransportSource::TCP command path.
// =============================================================

#if defined(PYMC_ETHERNET_W5100S)

#include "w5100s_ethernet_transport.h"
#include "protocol.h"
#include "frame_parser.h"
#include "net_filter.h"
#include "compat.h"
#include "board_config.h"

#include <SPI.h>
#include <RAK13800_W5100S.h>
#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52_SERIES) || defined(ARDUINO_NRF52_ADAFRUIT)
#  include "nrf_gpio.h"
#endif

#ifndef PYMC_ETH_TCP_PORT
#  define PYMC_ETH_TCP_PORT 5055
#endif
#ifndef PYMC_ETH_TOKEN
#  define PYMC_ETH_TOKEN ""
#endif
#ifndef PYMC_ETH_HOSTNAME
#  define PYMC_ETH_HOSTNAME "pymc-rak4631-eth"
#endif
#ifndef PYMC_ETH_USE_DHCP
#  define PYMC_ETH_USE_DHCP 1
#endif
#ifndef PYMC_ETH_DHCP_TIMEOUT_MS
#  define PYMC_ETH_DHCP_TIMEOUT_MS 5000UL
#endif
#ifndef PYMC_ETH_DHCP_RESPONSE_TIMEOUT_MS
#  define PYMC_ETH_DHCP_RESPONSE_TIMEOUT_MS 1000UL
#endif
#ifndef PYMC_ETH_DHCP_RETRY_MS
#  define PYMC_ETH_DHCP_RETRY_MS 30000UL
#endif
#ifndef PYMC_ETH_STATIC_FALLBACK_ON_DHCP_FAIL
// Avoid claiming a hard-coded address on an unknown production LAN unless the
// builder explicitly opts in. With the default 0, DHCP failure leaves the
// transport offline and retries while link is up.
#  define PYMC_ETH_STATIC_FALLBACK_ON_DHCP_FAIL 0
#endif
#ifndef PYMC_ETH_STATIC_IP
#  define PYMC_ETH_STATIC_IP 192, 168, 1, 50
#endif
#ifndef PYMC_ETH_GATEWAY
#  define PYMC_ETH_GATEWAY 192, 168, 1, 1
#endif
#ifndef PYMC_ETH_SUBNET
#  define PYMC_ETH_SUBNET 255, 255, 255, 0
#endif
#ifndef PYMC_ETH_DNS
#  define PYMC_ETH_DNS 1, 1, 1, 1
#endif

#ifndef PYMC_ETH_POWER_PIN
#  define PYMC_ETH_POWER_PIN -1
#endif
#ifndef PYMC_ETH_RESET_PIN
#  define PYMC_ETH_RESET_PIN -1
#endif
#ifndef PYMC_ETH_CS_PIN
#  define PYMC_ETH_CS_PIN SS
#endif
#ifndef PYMC_ETH_RESET_LOW_MS
#  define PYMC_ETH_RESET_LOW_MS 100
#endif
#ifndef PYMC_ETH_POST_RESET_MS
#  define PYMC_ETH_POST_RESET_MS 1000
#endif
#ifndef PYMC_ETH_HARD_RESET_ON_BEGIN
// Official MeshCore RAK4631 Ethernet support does not pulse W5100S reset:
// toggling reset can drop PHY link and break PoE-powered boot. Keep reset
// deasserted by default; builders can opt in when debugging non-PoE setups.
#  define PYMC_ETH_HARD_RESET_ON_BEGIN 0
#endif
#ifndef PYMC_ETH_ASSUME_UNKNOWN_LINK_UP
// W5100S should be able to report cable link state. Treating Unknown as
// link-up can start the TCP path while the chip/bus is not ready; make the
// compatibility fallback explicit for local testing if needed.
#  define PYMC_ETH_ASSUME_UNKNOWN_LINK_UP 0
#endif

#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52_SERIES) || defined(ARDUINO_NRF52_ADAFRUIT)
// W5100S Ethernet on SPIM3 (SPIM1 is reserved for I2C/TWI by the Adafruit
// nRF52 BSP — see nrfx_config.h). The SX1262 LoRa radio uses SPIM2 in
// main.cpp. Both use the WisBlock IO-slot pins defined in the board header.
static SPIClass PymcEthSpi(NRF_SPIM3, PYMC_ETH_SPI_MISO,
                           PYMC_ETH_SPI_SCK, PYMC_ETH_SPI_MOSI);

// RAK19018 PoE can brown out if WB_IO2 is not asserted early enough. The
// official firmware drives WB_IO2 high from a constructor before normal
// Arduino setup; do the same for the known RAK13800 power pin.
#  if defined(PYMC_ETH_POWER_PIN) && (PYMC_ETH_POWER_PIN == 34)
static void __attribute__((constructor(102))) pymcRak4631EarlyEthernetPower() {
    nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(1, 2));  // WB_IO2 = P1.02 / Arduino 34
    nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(1, 2));
}
#  endif
#endif

// Forward decls from main.cpp, same as firmware/src/tcp_server.cpp.
extern void processHostCommand(uint8_t cmd, const uint8_t* payload, uint16_t len, TransportSource src);
extern void noteTransportFrameError(uint8_t err_code);

static void buildFrame(uint8_t* buf, uint16_t& outLen, uint8_t cmd,
                       const uint8_t* payload, uint16_t len) {
    uint16_t i = 0;
    buf[i++] = PROTO_SYNC;
    buf[i++] = cmd;
    buf[i++] = len & 0xFF;
    buf[i++] = (len >> 8) & 0xFF;
    if (len > 0 && payload) {
        memcpy(buf + i, payload, len);
        i += len;
    }
    uint16_t crc = crc16_ccitt(buf + 1, 3 + len);
    buf[i++] = crc & 0xFF;
    buf[i++] = (crc >> 8) & 0xFF;
    outLen = i;
}

namespace EthernetManager {
    static bool started = false;
    static bool gotIP = false;
    static bool dhcpMode = false;
    static char ipString[16] = "---";
    static uint32_t lastMaintainMs = 0;
    static uint32_t lastDhcpAttemptMs = 0;
    static byte mac[6] = {0x02, 0x50, 0x59, 0x4d, 0x43, 0x00};
    static char hostnameString[64] = PYMC_ETH_HOSTNAME;

    static const char* linkStatusString() {
        auto s = Ethernet.linkStatus();
        if (s == LinkON) return "up";
        if (s == LinkOFF) return "down";
        if (s == Unknown) return "unknown";
        return "?";
    }

    static bool linkStatusIsUp() {
        auto s = Ethernet.linkStatus();
#if PYMC_ETH_ASSUME_UNKNOWN_LINK_UP
        return s == LinkON || s == Unknown;
#else
        return s == LinkON;
#endif
    }

    static void clearIPString() {
        gotIP = false;
        strncpy(ipString, "---", sizeof(ipString));
        ipString[sizeof(ipString) - 1] = '\0';
    }

    static void refreshIPString() {
        // A cached/static localIP is not a usable TCP path if the cable/link is
        // down. Gate hasIP()/status on link state so the TCP server is not
        // started against a stale address after unplug/replug or DHCP failure.
        if (!linkStatusIsUp()) {
            clearIPString();
            return;
        }

        IPAddress ip = Ethernet.localIP();
        if (ip == IPAddress((uint32_t)0)) {
            clearIPString();
            return;
        }
        gotIP = true;
        snprintf(ipString, sizeof(ipString), "%u.%u.%u.%u",
                 ip[0], ip[1], ip[2], ip[3]);
    }

#if PYMC_ETH_USE_DHCP
    static bool tryDhcp(const char* reason) {
        lastDhcpAttemptMs = millis();
        Serial.printf("[ETH] DHCP %s timeout=%lums response=%lums\n",
                      reason ? reason : "attempt",
                      (unsigned long)PYMC_ETH_DHCP_TIMEOUT_MS,
                      (unsigned long)PYMC_ETH_DHCP_RESPONSE_TIMEOUT_MS);
        int ok = Ethernet.begin(mac, PYMC_ETH_DHCP_TIMEOUT_MS,
                                PYMC_ETH_DHCP_RESPONSE_TIMEOUT_MS);
        refreshIPString();
        if (ok != 0 && gotIP) {
            Serial.printf("[ETH] DHCP lease %s\n", ipString);
            return true;
        }
        clearIPString();
        Serial.println("[ETH] DHCP failed; transport remains offline until retry/static config");
        return false;
    }
#endif

    static void makeMac() {
#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52_SERIES) || defined(ARDUINO_NRF52_ADAFRUIT)
        // Same deterministic locally administered MAC pattern used by the
        // official MeshCore RAK4631 Ethernet implementation.
        uint32_t device_id = NRF_FICR->DEVICEID[0];
        mac[0] = 0x02;
        mac[1] = 0x92;
        mac[2] = 0x1F;
        mac[3] = (device_id >> 16) & 0xFF;
        mac[4] = (device_id >> 8) & 0xFF;
        mac[5] = device_id & 0xFF;
#else
        uint8_t chip[6] = {0};
        compatGetMac(chip);
        mac[0] = 0x02;
        mac[1] = chip[1] ^ 0x50;
        mac[2] = chip[2] ^ 0x59;
        mac[3] = chip[3] ^ 0x4d;
        mac[4] = chip[4] ^ 0x43;
        mac[5] = chip[5];
#endif
    }

    static void powerAndReset() {
        if (PYMC_ETH_POWER_PIN >= 0) {
            pinMode(PYMC_ETH_POWER_PIN, OUTPUT);
            digitalWrite(PYMC_ETH_POWER_PIN, HIGH);
        }
        if (PYMC_ETH_RESET_PIN >= 0) {
            pinMode(PYMC_ETH_RESET_PIN, OUTPUT);
#if PYMC_ETH_HARD_RESET_ON_BEGIN
            digitalWrite(PYMC_ETH_RESET_PIN, LOW);
            delay(PYMC_ETH_RESET_LOW_MS);
#endif
            // Keep W5100S reset deasserted. This mirrors official MeshCore
            // RAK4631 Ethernet support and avoids dropping a PoE PHY link.
            digitalWrite(PYMC_ETH_RESET_PIN, HIGH);
        }
        delay(PYMC_ETH_POST_RESET_MS);
    }

    void begin(const char* hostname,
               bool useStaticIP,
               const IPAddress& staticIP,
               const IPAddress& gateway,
               const IPAddress& subnet,
               const IPAddress& dns1,
               const IPAddress& dns2) {
        (void)dns2;
        end();
        makeMac();
        powerAndReset();

        const char* effectiveHostname = (hostname && hostname[0]) ? hostname : PYMC_ETH_HOSTNAME;
        strncpy(hostnameString, effectiveHostname, sizeof(hostnameString));
        hostnameString[sizeof(hostnameString) - 1] = '\0';

#if defined(ARDUINO_ARCH_NRF52) || defined(NRF52_SERIES) || defined(ARDUINO_NRF52_ADAFRUIT)
        PymcEthSpi.begin();
        Ethernet.init(PymcEthSpi, PYMC_ETH_CS_PIN);
#else
        SPI.begin();
        Ethernet.init(SPI, PYMC_ETH_CS_PIN);
#endif

        Serial.printf("[ETH] W5100S init host=%s cs=%d pwr=%d rst=%d mac=%02X:%02X:%02X:%02X:%02X:%02X link=%s\n",
                      hostnameString, (int)PYMC_ETH_CS_PIN,
                      (int)PYMC_ETH_POWER_PIN, (int)PYMC_ETH_RESET_PIN,
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                      linkStatusString());

        started = true;
        dhcpMode = false;
        clearIPString();

#if PYMC_ETH_USE_DHCP
        if (useStaticIP) {
            IPAddress dns = dns1 == IPAddress((uint32_t)0) ? gateway : dns1;
            Ethernet.begin(mac, staticIP, dns, gateway, subnet);
        } else {
            dhcpMode = true;
            if (!linkStatusIsUp()) {
                Serial.println("[ETH] no usable link at boot; DHCP deferred until link is up");
            } else if (!tryDhcp("initial")) {
#  if PYMC_ETH_STATIC_FALLBACK_ON_DHCP_FAIL
                Serial.println("[ETH] using compile-time static fallback after DHCP failure");
                dhcpMode = false;
                IPAddress ip(PYMC_ETH_STATIC_IP);
                IPAddress gw(PYMC_ETH_GATEWAY);
                IPAddress mask(PYMC_ETH_SUBNET);
                IPAddress dns(PYMC_ETH_DNS);
                Ethernet.begin(mac, ip, dns, gw, mask);
#  else
                Serial.println("[ETH] static fallback disabled; will retry DHCP while link is up");
#  endif
            }
        }
#else
        {
            IPAddress ip(PYMC_ETH_STATIC_IP);
            IPAddress gw(PYMC_ETH_GATEWAY);
            IPAddress mask(PYMC_ETH_SUBNET);
            IPAddress dns(PYMC_ETH_DNS);
            Ethernet.begin(mac, ip, dns, gw, mask);
        }
#endif

        refreshIPString();
        Serial.printf("[ETH] local IP %s link=%s usable=%u\n", ipString,
                      linkStatusString(), isLinkUp() ? 1U : 0U);
    }

    void end() {
        started = false;
        gotIP = false;
        dhcpMode = false;
        lastMaintainMs = 0;
        lastDhcpAttemptMs = 0;
        clearIPString();
    }

    void loop() {
        if (!started) return;
        uint32_t now = millis();
        if (now - lastMaintainMs >= 1000) {
            lastMaintainMs = now;
#if PYMC_ETH_USE_DHCP
            if (dhcpMode && linkStatusIsUp()) {
                Ethernet.maintain();
            }
#endif
            refreshIPString();
        }

#if PYMC_ETH_USE_DHCP
        if (dhcpMode && !gotIP && linkStatusIsUp() &&
            (lastDhcpAttemptMs == 0 || now - lastDhcpAttemptMs >= PYMC_ETH_DHCP_RETRY_MS)) {
            tryDhcp("retry");
        }
#endif
    }

    bool isLinkUp() {
        if (!started) return false;
        return linkStatusIsUp();
    }

    bool hasIP() {
        if (!started) return false;
        refreshIPString();
        return gotIP && linkStatusIsUp();
    }

    const char* getIPString() {
        if (started) refreshIPString();
        return ipString;
    }
}

namespace TCPServer {
    static EthernetServer* server = nullptr;
    static EthernetClient client;
    static String requiredToken;
    static bool authenticated = false;
    static FrameParser parser;
    static uint32_t acceptedCount = 0;
    static uint32_t frameCount = 0;
    static bool lastIpUsable = false;

    static bool requiresAuth() {
        return requiredToken.length() > 0;
    }

    static void sendToClient(uint8_t cmd, const uint8_t* payload, uint16_t len) {
        if (!client || !client.connected()) return;
        uint8_t buf[MAX_FRAME_SIZE];
        uint16_t flen = 0;
        buildFrame(buf, flen, cmd, payload, len);
        client.write(buf, flen);
    }

    static void sendErrorToClient(uint8_t err) {
        sendToClient(CMD_ERROR, &err, 1);
    }

    static void disconnectClient() {
        if (client) {
            Serial.printf("[TCP/ETH] disconnect auth=%u frames=%lu\n",
                          authenticated ? 1U : 0U, (unsigned long)frameCount);
            client.stop();
        }
        authenticated = false;
        frameCount = 0;
        parser.reset();
    }

    static void onFrameOk(uint8_t cmd, const uint8_t* payload, uint16_t len, TransportSource src) {
        (void)src;
        frameCount++;
        Serial.printf("[TCP/ETH] frame cmd=0x%02X len=%u auth=%u\n",
                      cmd, (unsigned)len, authenticated ? 1U : 0U);

        if (requiresAuth() && !authenticated) {
            if (cmd == CMD_AUTH) {
                if (len == requiredToken.length() && memcmp(payload, requiredToken.c_str(), len) == 0) {
                    authenticated = true;
                    Serial.println("[TCP/ETH] auth OK");
                    sendToClient(CMD_AUTH_OK, nullptr, 0);
                } else {
                    Serial.println("[TCP/ETH] auth rejected");
                    sendErrorToClient(ERR_UNAUTHORIZED);
                    delay(5);
                    disconnectClient();
                }
            } else {
                sendErrorToClient(ERR_UNAUTHORIZED);
                delay(5);
                disconnectClient();
            }
            return;
        }

        if (cmd == CMD_AUTH) {
            sendToClient(CMD_AUTH_OK, nullptr, 0);
            return;
        }

        processHostCommand(cmd, payload, len, TransportSource::TCP);
    }

    static void onFrameErr(uint8_t err_code, TransportSource src) {
        (void)src;
        Serial.printf("[TCP/ETH] frame parse error 0x%02X\n", err_code);
        noteTransportFrameError(err_code);
        sendErrorToClient(err_code);
    }

    void begin(uint16_t port, const String& token) {
        end();
        requiredToken = token;
        authenticated = false;
        parser.reset();
        server = new EthernetServer(port ? port : PYMC_ETH_TCP_PORT);
        server->begin();
        lastIpUsable = EthernetManager::hasIP();
        Serial.printf("[TCP/ETH] listening on %u auth=%s\n",
                      (unsigned)(port ? port : PYMC_ETH_TCP_PORT),
                      requiresAuth() ? "required" : "open");
    }

    void end() {
        disconnectClient();
        if (server) {
            delete server;
            server = nullptr;
        }
        lastIpUsable = false;
    }

    void loop() {
        if (!server) return;

        const bool ipUsable = EthernetManager::hasIP();
        if (!ipUsable) {
            if (client) disconnectClient();
            lastIpUsable = false;
            return;
        }

        if (!lastIpUsable) {
            // A DHCP retry after cable replug calls Ethernet.begin() again.
            // On W5100S that can invalidate/recreate sockets, so restart the
            // listener when the Ethernet path transitions back to usable.
            server->begin();
            lastIpUsable = true;
            Serial.println("[TCP/ETH] IP restored; listener restarted");
        }

        if (!client || !client.connected()) {
            if (client) disconnectClient();
            EthernetClient incoming = server->accept();
            if (incoming) {
                IPAddress addr = incoming.remoteIP();
                if (!isLanAddress(addr)) {
                    Serial.printf("[TCP/ETH] rejecting non-LAN client %u.%u.%u.%u\n",
                                  addr[0], addr[1], addr[2], addr[3]);
                    incoming.stop();
                    return;
                }
                client = incoming;
                parser.reset();
                authenticated = false;
                frameCount = 0;
                acceptedCount++;
                Serial.printf("[TCP/ETH] accepted client %u.%u.%u.%u (#%lu, auth=%s)\n",
                              addr[0], addr[1], addr[2], addr[3],
                              (unsigned long)acceptedCount,
                              requiresAuth() ? "required" : "open");
            }
        }

        if (client && client.connected()) {
            while (client.available()) {
                uint8_t b = (uint8_t)client.read();
                frameparser_feed(parser, b, TransportSource::TCP, onFrameOk, onFrameErr);
            }
        }
    }

    bool isClientReady() {
        if (!EthernetManager::hasIP()) return false;
        if (!client || !client.connected()) return false;
        return !requiresAuth() || authenticated;
    }

    String getClientIP() {
        if (!EthernetManager::hasIP()) return String();
        if (!client || !client.connected()) return String();
        IPAddress addr = client.remoteIP();
        char buf[16];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u", addr[0], addr[1], addr[2], addr[3]);
        return String(buf);
    }

    void write(const uint8_t* data, size_t len) {
        if (!EthernetManager::hasIP()) return;
        if (!client || !client.connected()) return;
        client.write(data, len);
    }
}

#endif  // PYMC_ETHERNET_W5100S
