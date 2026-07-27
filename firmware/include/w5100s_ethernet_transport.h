// =============================================================
// w5100s_ethernet_transport.h — RAK13800/W5100S Ethernet transport
// for nRF52 openHop Modem targets.
// =============================================================
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace EthernetManager {
    enum class LinkState : uint8_t {
        UNKNOWN = 0,
        DOWN = 1,
        UP = 2,
    };

    struct Snapshot {
        bool started = false;
        bool hasIP = false;
        bool useDhcp = false;
        LinkState linkState = LinkState::UNKNOWN;
        IPAddress localIP;
        IPAddress subnet;
        IPAddress gateway;
        IPAddress dns1;
        IPAddress dns2;
        char hostname[65] = {};
        uint8_t mac[6] = {};
    };

    void begin(const char* hostname = nullptr,
               bool useStaticIP = false,
               const IPAddress& staticIP = IPAddress((uint32_t)0),
               const IPAddress& gateway = IPAddress((uint32_t)0),
               const IPAddress& subnet = IPAddress((uint32_t)0),
               const IPAddress& dns1 = IPAddress((uint32_t)0),
               const IPAddress& dns2 = IPAddress((uint32_t)0));
    void end();
    void loop();
    bool isLinkUp();
    bool hasIP();
    const char* getIPString();
    LinkState getLinkState();
    bool isDhcpMode();
    IPAddress getLocalIP();
    IPAddress getSubnet();
    IPAddress getGateway();
    IPAddress getDns1();
    IPAddress getDns2();
    const char* getHostname();
    void getMac(uint8_t out[6]);
    Snapshot getSnapshot();
}

namespace TCPServer {
    void begin(uint16_t port, const String& token);
    void loop();
    void end();
    bool isClientReady();
    void write(const uint8_t* data, size_t len);
    String getClientIP();
}
