// =============================================================
// dhcp_routes.h — RFC 3442 classless static routes (DHCP option 121)
//
// The ESP-IDF lwIP build used by the Arduino core does not request,
// parse or install DHCP option 121 routes. On networks where VPN or
// other non-local prefixes are reachable through a different next hop
// than the default router, replies from the modem take the default
// route and are dropped by the router's connection tracking.
//
// This module:
//   * adds option 121 to the DHCP Parameter Request List,
//   * stages option 121 (or the Microsoft 249 alias) from DHCP replies,
//   * selects the learned next hop for matching IPv4 destinations.
//
// Routes are staged per DHCP message and published only when the SDK binds
// an accepted lease. Each interface owns its table; link/down/remove events
// retire its active and pending state. A renewed ACK omitting classless routes
// removes the old table. Malformed options never borrow previous-lease data.
// =============================================================
#pragma once

#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include <IPAddress.h>

namespace DhcpRoutes {

struct Route {
    IPAddress destination;
    uint8_t   prefix;
    IPAddress gateway;
};

// Announce support. Lifecycle callbacks register on the initialized lwIP core
// when the DHCP client registers its UDP receive callback. Safe to call before
// networking starts; repeated calls do not erase an already accepted lease.
void begin();

// Management accessors describe the current default interface. Each call is
// synchronized; several calls are not one atomic lease-generation snapshot.
size_t count();
bool   get(size_t index, Route& out);

bool      hasDefaultRoute();
IPAddress defaultGateway();
bool      active();

// The gateway supplied by DHCP option 3, kept separately because the
// route hook rewrites netif->gw before each off-link transmit.
IPAddress baseGateway();
bool      baseGatewayValid();

} // namespace DhcpRoutes

#endif // ARDUINO_ARCH_ESP32
