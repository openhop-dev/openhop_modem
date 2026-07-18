// =============================================================
// oled_display.h — OLED status display for Heltec V3
// Uses Adafruit_SSD1306 (same as MeshCore firmware)
// =============================================================
#pragma once

#include <Wire.h>
#include <Adafruit_GFX.h>
#if defined(BOARD_STATION_G2)
#include <Adafruit_SH110X.h>
using OledDriver = Adafruit_SH1106G;
static constexpr uint16_t OLED_WHITE = SH110X_WHITE;
#else
#define SSD1306_NO_SPLASH
#include <Adafruit_SSD1306.h>
using OledDriver = Adafruit_SSD1306;
static constexpr uint16_t OLED_WHITE = SSD1306_WHITE;
#endif

class OledDisplay {
public:
    void begin();
    // Boot splash: openHop logo centered on the panel (64x64 bitmap on a
    // 128x64 display, so it lands offset-32 horizontally, 0 vertically).
    // Called immediately after begin(); the rest of setup() (Wi-Fi
    // connect, Ethernet bring-up, radio init) runs while it's visible.
    void showSplash();
    void showBoot(const char* version);
    // state is a short tag (e.g. "AP", "WiFi", "RX") shown top-right.
    // version is shown in the header so the currently-running firmware is
    // visible at a glance — useful after OTA to confirm the new build.
    void showStatus(uint32_t rx, uint32_t tx,
                    const char* ssid, const char* ip,
                    const char* state, const char* version,
                    uint16_t battery_mv = 0xFFFF);
    // Secondary screen: live radio configuration (freq, SF, BW, CR, power,
    // preamble, sync word). Reached by short-tap PRG from the status screen.
    void showRadioConfig(uint32_t freq_hz, uint32_t bandwidth_hz,
                         uint8_t sf, uint8_t cr, int8_t power_dbm,
                         uint16_t syncword, uint8_t preamble_len,
                         const char* version);
    // Tertiary screen: uptime + host-link health (TCP client IP, last USB
    // command age, CRC error count). Reached by short-tap PRG from the
    // radio screen.
    void showDiagnostics(uint32_t uptime_sec,
                         const char* tcp_client_ip,   // "" when no client
                         uint32_t usb_idle_sec,        // UINT32_MAX = never
                         uint32_t rx_count, uint32_t tx_count,
                         uint32_t crc_errors,
                         const char* version);
    void showError(const char* msg);
    void clear();

    // Power control — panel stays initialized, only DISPLAYOFF/ON is toggled.
    void turnOff();
    void turnOn();

    // ─── v0.7 hooks (originally added for the T114 TFT, mirrored here as
    // no-ops / thin updates so the heltec_v3 build stays in lockstep with
    // the shared main.cpp) ───────────────────────────────────────────────

    // Cache radio config so status screens can show it. The OLED footprint
    // doesn't have room for everything that the T114 TFT does — implementation
    // updates internal cache and (optionally) refreshes the visible screen.
    void setRadioInfo(uint32_t freq_hz, uint8_t sf, uint32_t bandwidth_hz,
                      uint8_t cr, int8_t power_dbm,
                      uint8_t preamble_len, uint16_t syncword);

    // Display name cache — used by sector-array deployments where the
    // controller may rename a modem at runtime. On Heltec V3 this is
    // currently a no-op (no big-name screen), but kept in the API so
    // shared main.cpp compiles unchanged.
    void setDisplayName(const char* name);

    // Standby flag — when true the modem is parked (radio off). The status
    // screen's "state" tag will reflect this on next render.
    void setStandby(bool standby);

private:
    OledDriver *_display = nullptr;
    bool _ready = false;
};
