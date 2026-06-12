// =============================================================
// tft_display.cpp — Heltec SPI TFT display drivers.
//
// Compiled into every env (because src_filter `+<*>` picks it up)
// but the contents are gated on board flags so non-TFT builds skip it
// without needing per-env src_filter exclusions. TFT board envs exclude
// oled_display.cpp and keep main.cpp talking to this same OledDisplay API.
//
// The TFT lives on the second hardware SPI peripheral (`SPI1` in
// the Adafruit BSP — PIN_SPI1_SCK=P1.08, MOSI=P1.09, MISO=P1.11
// from our variant.h), separate from the SX1262 bus. VDD_CTL and
// LEDA_CTL are both active-LOW power gates: driving them HIGH
// turns the panel off (this caught us during bring-up — early
// firmware drove HIGH thinking they were active-high enables and
// got a black screen with the radio still working).
//
// Public API matches OledDisplay so main.cpp doesn't care which
// panel lives on the board.
// =============================================================
#if defined(BOARD_HELTEC_TRACKER_V2)

#include "tft_display.h"
#include "board_config.h"

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

static constexpr int16_t TFT_W = 160;
static constexpr int16_t TFT_H = 80;
static constexpr int16_t HEADER_H = 14;

static constexpr uint16_t COLOUR_BG     = 0x0000;
static constexpr uint16_t COLOUR_FG     = 0xFFFF;
static constexpr uint16_t COLOUR_HEADER = 0x07E0;
static constexpr uint16_t COLOUR_ACCENT = 0x07FF;
static constexpr uint16_t COLOUR_WARN   = 0xFD20;
static constexpr uint16_t COLOUR_ERR    = 0xF800;

static Adafruit_ST7735 tft(BOARD.pin_tft_cs, BOARD.pin_tft_dc,
                           BOARD.pin_tft_sda, BOARD.pin_tft_scl,
                           BOARD.pin_tft_rst);

static void writeActivePin(int8_t pin, bool activeHigh, bool active) {
    if (pin < 0) return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, active == activeHigh ? HIGH : LOW);
}

static void drawHeader(const char* displayName, const char* version) {
    tft.fillRect(0, 0, TFT_W, HEADER_H, COLOUR_HEADER);
    tft.setTextSize(1);
    tft.setTextColor(COLOUR_BG, COLOUR_HEADER);
    tft.setCursor(2, 3);
    const char* name = (displayName && *displayName) ? displayName : "pyMC";
    tft.print(name);

    if (version) {
        int16_t bx, by; uint16_t bw, bh;
        tft.getTextBounds(version, 0, 0, &bx, &by, &bw, &bh);
        if (bw < 70) {
            tft.setCursor(TFT_W - (int16_t)bw - 2, 3);
            tft.print(version);
        }
    }

    tft.setTextColor(COLOUR_FG, COLOUR_BG);
}

static void drawText(int16_t x, int16_t y, const char* text,
                     uint16_t colour = COLOUR_FG, uint8_t size = 1) {
    tft.setTextSize(size);
    tft.setTextColor(colour, COLOUR_BG);
    tft.setCursor(x, y);
    tft.print(text ? text : "");
}

void OledDisplay::begin() {
    if (BOARD.pin_tft_sda < 0 || BOARD.pin_tft_scl < 0 ||
        BOARD.pin_tft_dc < 0 || BOARD.pin_tft_rst < 0 ||
        BOARD.pin_tft_cs < 0) {
        _ready = false;
        return;
    }

    writeActivePin(BOARD.pin_tft_power, BOARD.tft_power_active_high, true);
    writeActivePin(BOARD.pin_tft_bl, BOARD.tft_bl_active_high, true);
    pinMode(BOARD.pin_tft_rst, OUTPUT);
    digitalWrite(BOARD.pin_tft_rst, HIGH);
    delay(10);

    tft.initR(INITR_MINI160x80);
    tft.setRotation(1);
    uint8_t madctl = ST77XX_MADCTL_MY | ST77XX_MADCTL_MV | ST7735_MADCTL_BGR;
    tft.sendCommand(ST77XX_MADCTL, &madctl, 1);
    tft.setSPISpeed(40000000);
    tft.fillScreen(COLOUR_BG);
    tft.setTextColor(COLOUR_FG, COLOUR_BG);
    tft.setTextWrap(false);
    tft.cp437(true);
    _ready = true;
}

void OledDisplay::setDisplayName(const char* name) {
    if (!name) name = "";
    snprintf(_displayName, sizeof(_displayName), "%s", name);
}

void OledDisplay::setRadioInfo(uint32_t freq_hz, uint8_t sf, uint32_t bw_hz,
                               uint8_t cr, int8_t power_dbm,
                               int16_t last_rssi, int16_t last_snr_x10) {
    _freq_hz = freq_hz;
    _sf = sf;
    _bw_hz = bw_hz;
    _cr = cr;
    _power_dbm = power_dbm;
    _last_rssi = last_rssi;
    _last_snr_x10 = last_snr_x10;
}

void OledDisplay::setStandby(bool on) {
    _standby = on;
}

void OledDisplay::showSplash() {
    if (!_ready) return;
    tft.fillScreen(COLOUR_BG);
    drawText(8, 18, "pyMC", COLOUR_ACCENT, 3);
    drawText(8, 50, BOARD.name, COLOUR_FG, 1);
}

void OledDisplay::showStatus(uint32_t rx, uint32_t tx,
                             const char* ssid, const char* ip,
                             const char* state, const char* version,
                             uint16_t battery_mv) {
    (void)battery_mv;
    if (!_ready) return;
    tft.fillScreen(COLOUR_BG);
    drawHeader(_displayName, version);

    if (_standby) {
        drawText(22, 28, "STANDBY", COLOUR_ERR, 2);
        drawText(18, 52, "radio parked", COLOUR_FG, 1);
        return;
    }

    char buf[48];
    snprintf(buf, sizeof(buf), "%s  RX %lu TX %lu",
             state ? state : "---", (unsigned long)rx, (unsigned long)tx);
    drawText(2, 18, buf, COLOUR_ACCENT, 1);

    if (_last_rssi == INT16_MIN) {
        snprintf(buf, sizeof(buf), "RSSI --  SNR --");
    } else {
        snprintf(buf, sizeof(buf), "RSSI %d  SNR %.1f",
                 (int)_last_rssi, _last_snr_x10 / 10.0);
    }
    drawText(2, 31, buf, COLOUR_FG, 1);

    snprintf(buf, sizeof(buf), "%.3fM SF%u BW%lu",
             _freq_hz / 1e6, (unsigned)_sf,
             (unsigned long)(_bw_hz / 1000));
    drawText(2, 44, buf, COLOUR_FG, 1);

    snprintf(buf, sizeof(buf), "%s %s", ssid ? ssid : "---", ip ? ip : "---");
    drawText(2, 57, buf, COLOUR_WARN, 1);
}

void OledDisplay::showRadioConfig(uint32_t freq_hz, uint32_t bandwidth_hz,
                                  uint8_t sf, uint8_t cr, int8_t power_dbm,
                                  uint16_t syncword, uint8_t preamble_len,
                                  const char* version) {
    if (!_ready) return;
    tft.fillScreen(COLOUR_BG);
    drawHeader(_displayName, version);

    char buf[48];
    snprintf(buf, sizeof(buf), "%.3f MHz", freq_hz / 1e6);
    drawText(2, 18, buf, COLOUR_ACCENT, 1);
    snprintf(buf, sizeof(buf), "BW %lu SF%u CR4/%u",
             (unsigned long)bandwidth_hz, (unsigned)sf, (unsigned)cr);
    drawText(2, 31, buf, COLOUR_FG, 1);
    snprintf(buf, sizeof(buf), "Pwr %d dBm Sync %02X",
             (int)power_dbm, (unsigned)syncword);
    drawText(2, 44, buf, COLOUR_FG, 1);
    snprintf(buf, sizeof(buf), "Preamble %u", (unsigned)preamble_len);
    drawText(2, 57, buf, COLOUR_FG, 1);
}

void OledDisplay::showDiagnostics(uint32_t uptime_sec,
                                  const char* tcp_client_ip,
                                  uint32_t usb_idle_sec,
                                  uint32_t rx_count, uint32_t tx_count,
                                  uint32_t crc_errors,
                                  const char* version) {
    if (!_ready) return;
    tft.fillScreen(COLOUR_BG);
    drawHeader(_displayName, version);

    char buf[48];
    uint32_t h = uptime_sec / 3600;
    uint32_t m = (uptime_sec / 60) % 60;
    uint32_t s = uptime_sec % 60;
    snprintf(buf, sizeof(buf), "Up %lu:%02lu:%02lu",
             (unsigned long)h, (unsigned long)m, (unsigned long)s);
    drawText(2, 18, buf, COLOUR_ACCENT, 1);
    snprintf(buf, sizeof(buf), "TCP %s", tcp_client_ip ? tcp_client_ip : "---");
    drawText(2, 31, buf, COLOUR_FG, 1);
    snprintf(buf, sizeof(buf), "USB idle %lus", (unsigned long)usb_idle_sec);
    drawText(2, 44, buf, COLOUR_FG, 1);
    snprintf(buf, sizeof(buf), "RX %lu TX %lu CRC %lu",
             (unsigned long)rx_count, (unsigned long)tx_count,
             (unsigned long)crc_errors);
    drawText(2, 57, buf, crc_errors ? COLOUR_WARN : COLOUR_FG, 1);
}

void OledDisplay::showError(const char* msg) {
    if (!_ready) return;
    tft.fillScreen(COLOUR_BG);
    tft.fillRect(0, 0, TFT_W, HEADER_H, COLOUR_ERR);
    drawText(2, 3, "ERROR", COLOUR_FG, 1);
    drawText(2, 24, msg ? msg : "", COLOUR_ERR, 2);
}

void OledDisplay::turnOff() {
    if (!_ready) return;
    digitalWrite(BOARD.pin_tft_rst, LOW);
    writeActivePin(BOARD.pin_tft_bl, BOARD.tft_bl_active_high, false);
    writeActivePin(BOARD.pin_tft_power, BOARD.tft_power_active_high, false);
    _ready = false;
}

void OledDisplay::turnOn() {
    begin();
}

#elif defined(BOARD_HELTEC_T114)

#include "tft_display.h"
#include "splash_logo.h"

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ─── T114 TFT pin map (variant Heltec_T114_Board) ────────────
// All Arduino pin indices in the custom variant equal the raw
// nRF GPIO number, so these constants double as datasheet refs.
static constexpr int8_t PIN_TFT_RST_GPIO     = 2;   // P0.02
static constexpr int8_t PIN_TFT_VDD_CTL_GPIO = 3;   // P0.03 — panel logic VDD enable
static constexpr int8_t PIN_TFT_CS_GPIO      = 11;  // P0.11
static constexpr int8_t PIN_TFT_DC_GPIO      = 12;  // P0.12
static constexpr int8_t PIN_TFT_BL_GPIO      = 15;  // P0.15 — backlight LED anode (LEDA_CTL)

// ─── Panel geometry ──────────────────────────────────────────
// Native ST7789 framebuffer is 240×320; the LH114T-IF03 only
// exposes a 135×240 visible window. We render in landscape
// (rotation=1) — the T114 enclosure is wider than tall when the
// USB-C port is on the right, and 240×135 leaves room for the
// 4-line status block without wrapping.
static constexpr int16_t TFT_W = 240;
static constexpr int16_t TFT_H = 135;

// ─── Layout constants ────────────────────────────────────────
// Rotation 1 = landscape (240 wide × 135 tall). Header (banner +
// state tag) sits at the top, body follows top-down. All draw
// calls use Adafruit_GFX's classic 6×8 font scaled 1× or 2×.
static constexpr uint16_t COLOUR_BG       = 0x0000;   // black
static constexpr uint16_t COLOUR_FG       = 0xFFFF;   // white
static constexpr uint16_t COLOUR_HEADER   = 0x07E0;   // green
static constexpr uint16_t COLOUR_ACCENT   = 0x07FF;   // cyan
static constexpr uint16_t COLOUR_WARN     = 0xFD20;   // orange
static constexpr uint16_t COLOUR_ERR      = 0xF800;   // red

// Adafruit_ST7789(SPIClass*, CS, DC, RST). Bind to SPI1 — its
// pins (SCK=40, MOSI=41, MISO=43) are dedicated to the TFT and
// don't fight with the SX1262 bus on SPI.
static Adafruit_ST7789 tft(&SPI1, PIN_TFT_CS_GPIO, PIN_TFT_DC_GPIO, PIN_TFT_RST_GPIO);

void OledDisplay::begin() {
    // VDD_CTL and LEDA_CTL are both ACTIVE-LOW power gates on the
    // T114 (PMOS high-side switches). Drive LOW to turn the panel
    // logic and backlight on. RST stays HIGH for normal operation
    // — Adafruit_ST7789::init() does its own LOW pulse internally.
    pinMode(PIN_TFT_VDD_CTL_GPIO, OUTPUT);
    pinMode(PIN_TFT_BL_GPIO, OUTPUT);
    pinMode(PIN_TFT_RST_GPIO, OUTPUT);
    digitalWrite(PIN_TFT_VDD_CTL_GPIO, LOW);
    digitalWrite(PIN_TFT_BL_GPIO, LOW);
    digitalWrite(PIN_TFT_RST_GPIO, HIGH);
    delay(10);   // VDD ramp before we hit the controller

    // Bring up the dedicated TFT SPI bus. SX1262 stays on `SPI`,
    // so this one starts cold and we configure it explicitly.
    SPI1.begin();

    // init() takes the *native* portrait dimensions (135×240) so
    // the driver applies the correct column offset (52 px) for
    // the LH114T-IF03 subwindow; setRotation(1) then rotates into
    // our preferred landscape orientation.
    tft.init(135, 240, SPI_MODE0);
    tft.setRotation(3);                     // landscape, 180° vs rotation=1
                                            // (USB-C now on the LEFT)
    tft.setSPISpeed(40000000);              // 40 MHz — same as MeshCore
    tft.fillScreen(COLOUR_BG);
    tft.setTextColor(COLOUR_FG, COLOUR_BG);
    tft.setTextWrap(false);
    _ready = true;
}

// ─── Layout heights ─────────────────────────────────────────
// Landscape (240×135). The display is dominated by the sector
// name (controller-pushed) so an operator standing in front of
// a 4-modem rack can tell which is which from across the room.
//
//   y=0   ┌────────────────────────────────────────┐
//   |     │  EAST              v0.7-heltec_t114    │  name banner, textSize=3
//   y=32  ├────────────────────────────────────────┤
//   |     │  STATE: RX                             │  status line, textSize=2
//   |     │  RX 12   TX 3                          │
//   |     │  RSSI -78 dBm   SNR 12.3 dB            │
//   y=135 └────────────────────────────────────────┘
static constexpr int16_t NAME_BANNER_H = 32;

// ─── Helpers ─────────────────────────────────────────────────

// Banner with the controller-assigned display name (big text)
// plus a small state badge underneath. Falls back to "?" when
// the controller hasn't pushed a name yet.
static void drawNameBanner(const char* fallbackName, const char* version) {
    tft.fillRect(0, 0, TFT_W, NAME_BANNER_H, COLOUR_HEADER);
    tft.setTextColor(COLOUR_BG, COLOUR_HEADER);
    tft.setTextSize(3);
    const char* name = (fallbackName && *fallbackName) ? fallbackName : "?";

    int16_t bx, by; uint16_t bw, bh;
    tft.getTextBounds(name, 0, 0, &bx, &by, &bw, &bh);
    int16_t name_y = (NAME_BANNER_H - (int16_t)bh) / 2 - by;
    tft.setCursor(4, name_y);
    tft.print(name);

    // Right-aligned, much smaller version string.
    if (version) {
        tft.setTextSize(1);
        tft.getTextBounds(version, 0, 0, &bx, &by, &bw, &bh);
        tft.setCursor(TFT_W - (int16_t)bw - 4, NAME_BANNER_H - 10);
        tft.print(version);
    }
    tft.setTextColor(COLOUR_FG, COLOUR_BG);
    tft.setTextSize(1);
}

static void drawStateLine(int16_t y, const char* state) {
    tft.setCursor(4, y);
    tft.setTextSize(2);
    tft.setTextColor(COLOUR_ACCENT, COLOUR_BG);
    tft.print(state ? state : "...");
    tft.setTextColor(COLOUR_FG, COLOUR_BG);
}

static void drawLabelValue(int16_t y, const char* label, const String& value,
                           uint16_t labelColour = COLOUR_ACCENT,
                           uint8_t  textSize = 2) {
    tft.setCursor(4, y);
    tft.setTextSize(textSize);
    tft.setTextColor(labelColour, COLOUR_BG);
    tft.print(label);
    tft.setTextColor(COLOUR_FG, COLOUR_BG);
    tft.println(value);
}

void OledDisplay::setDisplayName(const char* name) {
    if (!name) name = "";
    snprintf(_displayName, sizeof(_displayName), "%s", name);
    // No automatic redraw — the next showStatus/showRadio/etc.
    // call will pick up the new name. Forcing a redraw here would
    // require remembering the last `show*` payload.
}

void OledDisplay::setRadioInfo(uint32_t freq_hz, uint8_t sf, uint32_t bw_hz,
                               uint8_t cr, int8_t power_dbm,
                               int16_t last_rssi, int16_t last_snr_x10) {
    _freq_hz       = freq_hz;
    _sf            = sf;
    _bw_hz         = bw_hz;
    _cr            = cr;
    _power_dbm     = power_dbm;
    _last_rssi     = last_rssi;
    _last_snr_x10  = last_snr_x10;
}

void OledDisplay::setStandby(bool on) {
    _standby = on;
}

// ─── Splash ──────────────────────────────────────────────────

void OledDisplay::showSplash() {
    if (!_ready) return;
    tft.fillScreen(COLOUR_BG);

    // Splash uses raw 240×135 landscape canvas. Logo on the left,
    // big "pyMC" + display name on the right.
    const int16_t logoSrc = SPLASH_LOGO_W;   // 64
    const int16_t logoOut = logoSrc;         // keep at 1x for landscape
    const int16_t x0 = 6;
    const int16_t y0 = (TFT_H - logoOut) / 2;
    for (int16_t row = 0; row < logoSrc; row++) {
        for (int16_t col = 0; col < logoSrc; col++) {
            uint16_t byteIdx = row * (logoSrc / 8) + (col / 8);
            uint8_t bit = 7 - (col % 8);
            uint8_t b = pgm_read_byte(&SPLASH_LOGO[byteIdx]);
            if (b & (1 << bit)) {
                tft.drawPixel(x0 + col, y0 + row, COLOUR_FG);
            }
        }
    }

    int16_t tx = x0 + logoOut + 14;
    tft.setTextColor(COLOUR_ACCENT, COLOUR_BG);
    tft.setTextSize(3);
    tft.setCursor(tx, y0 + 14);
    tft.print("pyMC");

    if (_displayName[0]) {
        tft.setTextSize(2);
        tft.setTextColor(COLOUR_FG, COLOUR_BG);
        tft.setCursor(tx, y0 + 46);
        tft.print(_displayName);
    }
    tft.setTextSize(1);
    tft.setTextColor(COLOUR_FG, COLOUR_BG);
}

// ─── Status screen ───────────────────────────────────────────

void OledDisplay::showStatus(uint32_t rx, uint32_t tx,
                             const char* /*ssid*/, const char* /*ip*/,
                             const char* state, const char* version,
                             uint16_t /*battery_mv*/) {
    // T114 lives behind the sector controller via UART — we never
    // have an IP / SSID of our own. The two unused parameters stay
    // for ABI parity with oled_display.h on the ESP32 boards (same
    // call site in main.cpp). Instead we paint the live LoRa state
    // pushed in via setRadioInfo().
    if (!_ready) return;
    tft.fillScreen(COLOUR_BG);
    drawNameBanner(_displayName, version);

    // Hard-standby gets a dedicated, can't-miss screen. Radio is
    // parked → RX/SNR/TX numbers are stale by definition, so we
    // hide them and print a big red STANDBY badge instead.
    if (_standby) {
        int16_t y = NAME_BANNER_H + 16;
        // Big red "STANDBY" centred horizontally.
        tft.setTextSize(3);
        tft.setTextColor(COLOUR_ERR, COLOUR_BG);
        const char* tag = "STANDBY";
        int16_t bx, by; uint16_t bw, bh;
        tft.getTextBounds(tag, 0, 0, &bx, &by, &bw, &bh);
        tft.setCursor((TFT_W - (int16_t)bw) / 2, y);
        tft.print(tag);
        // Subtitle.
        y += bh + 12;
        tft.setTextSize(1);
        tft.setTextColor(COLOUR_FG, COLOUR_BG);
        const char* sub = "radio off — awaiting RESUME";
        tft.getTextBounds(sub, 0, 0, &bx, &by, &bw, &bh);
        tft.setCursor((TFT_W - (int16_t)bw) / 2, y);
        tft.print(sub);
        return;
    }

    int16_t y = NAME_BANNER_H + 6;
    drawStateLine(y, state ? state : "---");
    y += 24;

    // Counters — big enough to read from a meter away.
    drawLabelValue(y, "RX ", String(rx) + "  TX " + String(tx));
    y += 22;

    // Last received packet quality. INT16_MIN sentinel = never heard.
    if (_last_rssi == INT16_MIN) {
        drawLabelValue(y, "", String("RSSI  --"), COLOUR_ACCENT, 1);
    } else {
        char buf[40];
        snprintf(buf, sizeof(buf), "RSSI %d  SNR %.1f",
                 (int)_last_rssi, _last_snr_x10 / 10.0);
        drawLabelValue(y, "", String(buf), COLOUR_FG, 1);
    }
    y += 14;

    // Live radio config — small line at the bottom so an operator
    // can sanity-check that all sectors share the same parameters.
    if (_freq_hz > 0) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%.3fM SF%u BW%lu",
                 _freq_hz / 1e6, (unsigned)_sf,
                 (unsigned long)(_bw_hz / 1000));
        drawLabelValue(y, "", String(buf), COLOUR_ACCENT, 1);
        y += 12;
        snprintf(buf, sizeof(buf), "CR 4/%u  Pwr %d dBm",
                 (unsigned)_cr, (int)_power_dbm);
        drawLabelValue(y, "", String(buf), COLOUR_ACCENT, 1);
    }
}

// ─── Radio screen ────────────────────────────────────────────

void OledDisplay::showRadioConfig(uint32_t freq_hz, uint32_t bandwidth_hz,
                                  uint8_t sf, uint8_t cr, int8_t power_dbm,
                                  uint16_t syncword, uint8_t preamble_len,
                                  const char* version) {
    if (!_ready) return;
    tft.fillScreen(COLOUR_BG);
    drawNameBanner(_displayName, version);

    int16_t y = NAME_BANNER_H + 6;
    drawStateLine(y, "RADIO"); y += 22;
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.3f MHz", freq_hz / 1e6);
        drawLabelValue(y, "F ", String(buf));
        y += 18;
    }
    {
        char buf[40];
        snprintf(buf, sizeof(buf), "BW %lu  SF %u  4/%u  %d dBm",
                 (unsigned long)bandwidth_hz, (unsigned)sf,
                 (unsigned)cr, (int)power_dbm);
        // Smaller for the long line.
        drawLabelValue(y, "", String(buf), COLOUR_FG, 1);
        y += 12;
    }
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "sync 0x%04X  pre %u",
                 (unsigned)syncword, (unsigned)preamble_len);
        drawLabelValue(y, "", String(buf), COLOUR_ACCENT, 1);
    }
}

// ─── Diagnostics screen ──────────────────────────────────────

void OledDisplay::showDiagnostics(uint32_t uptime_sec,
                                  const char* tcp_client_ip,
                                  uint32_t usb_idle_sec,
                                  uint32_t rx_count, uint32_t tx_count,
                                  uint32_t crc_errors,
                                  const char* version) {
    if (!_ready) return;
    tft.fillScreen(COLOUR_BG);
    drawNameBanner(_displayName, version);

    int16_t y = NAME_BANNER_H + 6;
    drawStateLine(y, "DIAG"); y += 22;
    {
        uint32_t h = uptime_sec / 3600;
        uint32_t m = (uptime_sec / 60) % 60;
        uint32_t s = uptime_sec % 60;
        char buf[24];
        snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu",
                 (unsigned long)h, (unsigned long)m, (unsigned long)s);
        drawLabelValue(y, "Up ", String(buf));
        y += 22;
    }
    drawLabelValue(y, "RX ", String(rx_count) + "  TX " + String(tx_count));
    y += 18;
    drawLabelValue(y, "CRC ", String(crc_errors),
                   crc_errors ? COLOUR_WARN : COLOUR_ACCENT, 1);
}

// ─── Error / power ───────────────────────────────────────────

void OledDisplay::showError(const char* msg) {
    if (!_ready) return;
    tft.fillScreen(COLOUR_BG);
    tft.fillRect(0, 0, TFT_W, 18, COLOUR_ERR);
    tft.setCursor(2, 5);
    tft.setTextColor(COLOUR_FG, COLOUR_ERR);
    tft.setTextSize(1);
    tft.print("ERROR");
    tft.setTextColor(COLOUR_FG, COLOUR_BG);

    if (msg) {
        tft.setCursor(2, 26);
        tft.setTextSize(2);
        tft.println(msg);
        tft.setTextSize(1);
    }
}

void OledDisplay::turnOff() {
    if (!_ready) return;
    // Active-low gates: HIGH disables the rail.
    digitalWrite(PIN_TFT_BL_GPIO, HIGH);
    digitalWrite(PIN_TFT_VDD_CTL_GPIO, HIGH);
    tft.enableSleep(true);
}

void OledDisplay::turnOn() {
    if (!_ready) return;
    digitalWrite(PIN_TFT_VDD_CTL_GPIO, LOW);
    tft.enableSleep(false);
    digitalWrite(PIN_TFT_BL_GPIO, LOW);
}

#endif  // BOARD_HELTEC_T114
