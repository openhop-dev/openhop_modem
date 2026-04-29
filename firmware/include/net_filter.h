// =============================================================
// net_filter.h — LAN-only access policy shared by TCP server + OTA.
//
// The firmware is intended for local pymc_repeater control. It is
// not designed to bridge LoRa traffic over the public Internet, and
// every TCP-facing surface (the protocol server on 5055 and the OTA
// HTTP listener on 80) refuses connections whose source address is
// outside the private RFC1918 / link-local / loopback ranges.
//
// A NAT port-forward or a tunnel that exposes the modem to the
// public Internet will fail the check at accept() time and the
// connection is dropped before any frame parsing or auth check
// runs. This is a hard policy of the firmware, not configurable at
// runtime — to lift it, edit this file and re-flash.
//
// IPv4 only; the firmware doesn't expose IPv6 listeners today.
// =============================================================
#pragma once

#include <IPAddress.h>

inline bool isLanAddress(IPAddress a) {
    const uint8_t o0 = a[0];
    const uint8_t o1 = a[1];
    if (o0 == 10)                          return true;   // 10.0.0.0/8
    if (o0 == 172 && o1 >= 16 && o1 <= 31) return true;   // 172.16.0.0/12
    if (o0 == 192 && o1 == 168)            return true;   // 192.168.0.0/16
    if (o0 == 169 && o1 == 254)            return true;   // 169.254.0.0/16 (link-local)
    if (o0 == 127)                         return true;   // 127.0.0.0/8 (loopback)
    return false;
}
