// =============================================================
// compat.h — tiny cross-platform shims so main.cpp builds on both
// the ESP32 family and supported nRF52840 targets.
//
// On ESP32 these are thin inlines around the ESP-IDF equivalents.
// On nRF52 they fall back to FICR-based MAC and stubbed-out
// values where the chip doesn't expose an analogue.
// =============================================================
#pragma once

#include <Arduino.h>
#include <stdint.h>

#if defined(ARDUINO_ARCH_ESP32)

#include <esp_task_wdt.h>
#include <esp_system.h>
#include <esp_mac.h>

inline void     compatWdtReset()     { esp_task_wdt_reset(); }
inline int      compatResetReason()  { return (int)esp_reset_reason(); }
inline void     compatGetMac(uint8_t mac[6]) { esp_efuse_mac_get_default(mac); }
inline uint32_t compatFreeHeap()     { return (uint32_t)ESP.getFreeHeap(); }
inline uint32_t compatMinFreeHeap()  { return (uint32_t)ESP.getMinFreeHeap(); }

#else  // nRF52 / non-ESP32

#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(ARDUINO_NRF52_ADAFRUIT)
#  include <nrf.h>
#endif

inline void     compatWdtReset()     { /* nRF52 WDT not armed by this firmware (yet) */ }
inline int      compatResetReason()  { return 0; }
inline void     compatGetMac(uint8_t mac[6]) {
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(ARDUINO_NRF52_ADAFRUIT)
    // FICR DEVICEID is a 64-bit chip-unique value. Mash it into a
    // 6-byte array using the lower 48 bits — same format as a MAC,
    // good enough for hostname suffix generation.
    uint32_t lo = NRF_FICR->DEVICEID[0];
    uint32_t hi = NRF_FICR->DEVICEID[1];
    mac[0] = (uint8_t)(hi >> 8);
    mac[1] = (uint8_t)(hi);
    mac[2] = (uint8_t)(lo >> 24);
    mac[3] = (uint8_t)(lo >> 16);
    mac[4] = (uint8_t)(lo >> 8);
    mac[5] = (uint8_t)(lo);
#else
    for (int i = 0; i < 6; i++) mac[i] = 0;
#endif
}
inline uint32_t compatFreeHeap()     { return 0; }
inline uint32_t compatMinFreeHeap()  { return 0; }

#endif  // ARDUINO_ARCH_ESP32
