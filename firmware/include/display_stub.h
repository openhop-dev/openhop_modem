// =============================================================
// display_stub.h — no-op `OledDisplay` used by nRF52 boards that
// have neither an OLED nor a TFT (e.g. the XIAO nRF52840 +
// Wio-SX1262 kit). Same public surface as oled_display.h /
// tft_display.h so main.cpp's call sites compile unchanged.
// Every method is empty / returns a default; no peripherals are
// touched.
// =============================================================
#pragma once

#include <Arduino.h>
#include <stdint.h>

class OledDisplay {
public:
    inline void begin() {}
    inline void showSplash() {}
    inline void showStatus(uint32_t, uint32_t,
                           const char*, const char*,
                           const char*, const char*,
                           uint16_t = 0xFFFF) {}
    inline void setDisplayName(const char*) {}
    inline void setRadioInfo(uint32_t, uint8_t, uint32_t,
                             uint8_t, int8_t,
                             int16_t, int16_t) {}
    inline void setStandby(bool) {}
    inline void showRadioConfig(uint32_t, uint32_t,
                                uint8_t, uint8_t, int8_t,
                                uint16_t, uint8_t,
                                const char*) {}
    inline void showDiagnostics(uint32_t,
                                const char*,
                                uint32_t,
                                uint32_t, uint32_t,
                                uint32_t,
                                const char*) {}
    inline void showError(const char*) {}
    inline void turnOn() {}
    inline void turnOff() {}
};
