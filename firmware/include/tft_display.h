// =============================================================
// tft_display.h — SPI TFT display drivers for Heltec boards.
//
// Same public surface as `oled_display.h` (the SSD1306 driver
// for ESP32 boards) so main.cpp's call sites compile unchanged
// regardless of which display lives on the board. The class is
// still called `OledDisplay` even though the underlying panel is
// a TFT — keeps the name consistent across the firmware family.
//
// Board-specific pin maps live either in BoardConfig (ESP32-S3
// Tracker V2) or in the custom board variant/constants already used
// by the nRF52 T114 driver.
// =============================================================
#pragma once

#include <Arduino.h>
#include <stdint.h>

class OledDisplay {
public:
    void begin();
    void showSplash();
    void showStatus(uint32_t rx, uint32_t tx,
                    const char* ssid, const char* ip,
                    const char* state, const char* version,
                    uint16_t battery_mv = 0xFFFF);
    // Display name pushed by the sector controller (CMD_SET_DISPLAY_NAME).
    // Shown big at the top of every status screen so an operator can tell
    // physical T114s apart at a glance.
    void setDisplayName(const char* name);
    // Pushed by main.cpp after applyConfig() and after every successful
    // RX. Shown on the status screen so the operator sees the modem's
    // live radio state without leaving the panel.
    void setRadioInfo(uint32_t freq_hz, uint8_t sf, uint32_t bw_hz,
                      uint8_t cr, int8_t power_dbm,
                      int16_t last_rssi, int16_t last_snr_x10);
    // Reflects the CMD_RADIO_STANDBY / RESUME state — paints the
    // status banner with a STBY tag so the operator sees the modem
    // is parked.
    void setStandby(bool on);
    void showRadioConfig(uint32_t freq_hz, uint32_t bandwidth_hz,
                         uint8_t sf, uint8_t cr, int8_t power_dbm,
                         uint16_t syncword, uint8_t preamble_len,
                         const char* version);
    void showDiagnostics(uint32_t uptime_sec,
                         const char* tcp_client_ip,
                         uint32_t usb_idle_sec,
                         uint32_t rx_count, uint32_t tx_count,
                         uint32_t crc_errors,
                         const char* version);
    void showError(const char* msg);
    void turnOn();
    void turnOff();

private:
    bool _ready = false;
    char _displayName[24] = "";    // pushed by controller; empty until first SET_DISPLAY_NAME
    // Cached LoRa state — main.cpp pushes via setRadioInfo()
    uint32_t _freq_hz   = 0;
    uint8_t  _sf        = 0;
    uint32_t _bw_hz     = 0;
    uint8_t  _cr        = 0;
    int8_t   _power_dbm = 0;
    int16_t  _last_rssi      = INT16_MIN;
    int16_t  _last_snr_x10   = 0;
    bool     _standby   = false;
};
