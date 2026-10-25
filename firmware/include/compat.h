// =============================================================
// compat.h — tiny cross-platform shims so main.cpp builds on both
// the ESP32 family and the nRF52840 (Heltec T114).
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
inline float    compatReadCpuTemperature() { return temperatureRead(); }

#else  // nRF52 / non-ESP32

#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(ARDUINO_NRF52_ADAFRUIT)
#  include <nrf.h>
#  include "nrf52_loop_watchdog.h"
inline void     compatWdtStart(uint32_t timeoutS) { Nrf52LoopWatchdog::start(*NRF_WDT, timeoutS); }
inline void     compatWdtReset()     { Nrf52LoopWatchdog::feed(*NRF_WDT); }
#else
inline void     compatWdtStart(uint32_t) {}
inline void     compatWdtReset()     {}
#endif
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
inline float    compatReadCpuTemperature() {
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(ARDUINO_NRF52_ADAFRUIT)
    return readCPUTemperature();
#else
    return __builtin_nanf("");
#endif
}

// ESP32 ships analogReadMilliVolts() with per-chip eFuse calibration;
// the Adafruit nRF52 BSP only offers a raw analogRead(). The SAADC
// default is the internal 0.6 V reference with 1/6 gain, i.e. a 3.6 V
// full-scale range, so at 12-bit resolution one LSB is 3600/4096 mV.
// Boards apply their own divider ratio through BOARD.battery.multiplier.
inline uint32_t analogReadMilliVolts(uint8_t pin) {
    analogReadResolution(12);
    return ((uint32_t)analogRead(pin) * 3600U) / 4096U;
}

#endif  // ARDUINO_ARCH_ESP32
