// =============================================================
// w5100s_ethernet_transport.h — RAK13800/W5100S Ethernet transport
// for nRF52 openHop Modem targets.
// =============================================================
#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include "tcp_server.h"

namespace EthernetManager {
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
}
