// =============================================================
// ethernet_manager.h — Optional on-board Ethernet (RMII PHY)
//
// Activated only when BOARD.ethernet.enabled == true (currently
// just ESP32-P4-Nano with IP101GRI). On every other board the
// begin()/loop() calls are no-ops so the same main.cpp compiles
// for the whole fleet.
// =============================================================
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace EthernetManager {

// Bring the EMAC + PHY up. Drives PHY reset, calls ETH.begin() with
// the parameters from BOARD.ethernet, then waits up to ~5 s for the
// link to come up and DHCP to lease an address. Watchdog-friendly.
void begin();

// Service Ethernet event polling. Currently a no-op (ETH.h dispatches
// link/IP events from its own task); kept for symmetry with WifiManager
// in case we add periodic status logging later.
void loop();

bool        isLinkUp();      // PHY autoneg done, link established
bool        hasIP();         // DHCP lease (or static IP) installed
const char* getIPString();   // dotted quad, "---" when no IP
const char* getMACString();  // colon-separated, empty when uninit

} // namespace EthernetManager
