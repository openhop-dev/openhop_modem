// =============================================================
// tft_display.h — ST7789 1.14" 135×240 driver for Heltec T114.
//
// Same public surface as `oled_display.h` (the SSD1306 driver
// for ESP32 boards) so main.cpp's call sites compile unchanged
// regardless of which display lives on the board. The class is
// still called `OledDisplay` even though the underlying panel is
// a TFT — keeps the name consistent across the firmware family.
//
// Hardware (T114 V2-specific, see Datasheet_T114.pdf and the
// MeshCore variant in _incoming/MeshCore-main):
//   * Panel: Heltec LH114T-IF03 — IPS, 135×240, 262 K colours,
//            Sitronix ST7789V controller, 4-line SPI write-only.
//   * SPI bus: shared with the SX1262 on the default SPI
//              peripheral (SCK=P0.19, MOSI=P0.22, MISO=P0.23).
//              CS gates which device gets the burst.
//   * Control: CS = P0.11, DC/RS = P0.12, RST = P0.02.
//   * Power gates: VDD_CTL = P0.03 (panel logic rail), LEDA_CTL
//                  = P0.15 (backlight LED anode). Both must be
//                  driven HIGH before any draw; pulled LOW in
//                  turnOff() to save current.
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
                    const char* state, const char* version);
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
