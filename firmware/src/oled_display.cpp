// =============================================================
// oled_display.cpp — I2C OLED status display
// Heltec V3-class boards use SSD1306; Station G2 uses SH1106.
// The board config provides I2C pins, optional VEXT rail, and reset line.
// =============================================================
#include "oled_display.h"
#include "board_config.h"
#include "splash_logo.h"

#define DISPLAY_ADDRESS 0x3C

void OledDisplay::begin() {
    // Boards without an OLED (e.g. ESP32-P4-Nano) declare pin_i2c_sda/scl
    // as -1. Skip Wire.begin() and SSD1306 init entirely; _ready stays
    // false so every show*() / turn*() call returns immediately.
    if (BOARD.pin_i2c_sda < 0 || BOARD.pin_i2c_scl < 0) {
        return;
    }

    // 1. Enable VEXT power rail when the board has one (Heltec V3 ⇒
    //    GPIO 36 LOW gates the 3V3 OLED rail). Skipped on boards that
    //    feed the OLED directly from 3V3.
    if (BOARD.pin_vext_enable_low >= 0) {
        pinMode(BOARD.pin_vext_enable_low, OUTPUT);
        digitalWrite(BOARD.pin_vext_enable_low, LOW);
        delay(100);
    }

    // 2. Pulse OLED RST when present (skipped when pin_i2c_oled_rst == -1
    //    — generic SSD1306 modules without an exposed RST pad).
    if (BOARD.pin_i2c_oled_rst >= 0) {
        pinMode(BOARD.pin_i2c_oled_rst, OUTPUT);
        digitalWrite(BOARD.pin_i2c_oled_rst, LOW);
        delay(50);
        digitalWrite(BOARD.pin_i2c_oled_rst, HIGH);
        delay(50);
    }

    // 3. Init I2C with the board's SDA/SCL pins
    Wire.begin(BOARD.pin_i2c_sda, BOARD.pin_i2c_scl);

    // 4. Create the board-specific OLED driver. Pass -1 when no RST pin is
    //    wired — both Adafruit drivers accept that and skip reset.
    _display = new OledDriver(128, 64, &Wire, BOARD.pin_i2c_oled_rst);

    // 5. Start display.
#if defined(BOARD_STATION_G2)
    if (_display->begin(DISPLAY_ADDRESS, true)) {
        _ready = true;

        _display->setContrast(0xFF);
#else
    if (_display->begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS, true, false)) {
        _ready = true;

        // 6. Max contrast/brightness
        _display->ssd1306_command(SSD1306_SETCONTRAST);
        _display->ssd1306_command(0xFF);
#endif

        _display->clearDisplay();
        _display->setTextColor(OLED_WHITE);
        _display->setTextSize(1);
        _display->cp437(true);
        _display->display();
    }
}

void OledDisplay::showSplash() {
    if (!_ready) return;
    _display->clearDisplay();
    // Centered horizontally on a 128px-wide panel: (128 - 64) / 2 = 32.
    // Vertically the 64x64 bitmap fills the full 64px height.
    int16_t x = (128 - SPLASH_LOGO_W) / 2;
    int16_t y = 0;
    _display->drawBitmap(x, y, SPLASH_LOGO,
                         SPLASH_LOGO_W, SPLASH_LOGO_H, OLED_WHITE);
    _display->display();
}

void OledDisplay::showBoot(const char* version) {
    if (!_ready) return;
    _display->clearDisplay();
    _display->setTextSize(1);
    _display->setTextColor(OLED_WHITE);
    _display->setCursor(0, 0);
    _display->println("openHop Modem");
    _display->println(version);
    _display->println();
    _display->println("Waiting for host...");
    _display->display();
}

void OledDisplay::showStatus(uint32_t rx, uint32_t tx,
                             const char* ssid, const char* ip,
                             const char* state, const char* version,
                             uint16_t battery_mv) {
    if (!_ready) return;

    char buf[32];
    _display->clearDisplay();
    _display->setTextSize(1);
    _display->setTextColor(OLED_WHITE);

    // Header: firmware version (left) + right-aligned state tag.
    // Font is 6 px/char; state tag reserves 6 * strlen(state) px on the right.
    const char* vs = (version && *version) ? version : "FW:?";
    int stateLen   = (state && *state) ? (int)strlen(state) : 0;
    int stateX     = 128 - stateLen * 6;
    int maxVerChars = (stateX / 6) - 1;          // 1-char gap before the tag
    if (maxVerChars < 0) maxVerChars = 0;
    char vbuf[22];
    snprintf(vbuf, sizeof(vbuf), "%s", vs);
    if ((int)strlen(vbuf) > maxVerChars && maxVerChars < (int)sizeof(vbuf)) {
        vbuf[maxVerChars] = '\0';
    }
    _display->setCursor(0, 0);
    _display->print(vbuf);
    if (stateLen > 0) {
        _display->setCursor(stateX, 0);
        _display->print(state);
    }
    _display->drawFastHLine(0, 10, 128, OLED_WHITE);

    // Line 1 — RX/TX counters
    _display->setCursor(0, 14);
    snprintf(buf, sizeof(buf), "RX:%lu  TX:%lu",
             (unsigned long)rx, (unsigned long)tx);
    _display->print(buf);

    // Line 2 — Wi-Fi SSID (truncated to fit after "W:" prefix)
    _display->setCursor(0, 28);
    snprintf(buf, sizeof(buf), "W:%s", ssid && *ssid ? ssid : "---");
    if (strlen(buf) > 21) buf[21] = '\0';
    _display->print(buf);

    // Line 3 — IP address
    _display->setCursor(0, 42);
    snprintf(buf, sizeof(buf), "IP:%s", ip && *ip ? ip : "---");
    if (strlen(buf) > 21) buf[21] = '\0';
    _display->print(buf);

    // Optional Line 4 — battery voltage. Only board variants that define
    // battery sensing pass a real value; every other board silently omits it.
    if (battery_mv != 0xFFFF) {
        _display->setCursor(0, 54);
        snprintf(buf, sizeof(buf), "BAT:%.2fV", (double)(battery_mv / 1000.0f));
        _display->print(buf);
    }

    // Heartbeat dot, bottom-right
    static bool dot = false;
    if (dot) _display->fillCircle(124, 60, 2, OLED_WHITE);
    dot = !dot;

    _display->display();
}

void OledDisplay::showRadioConfig(uint32_t freq_hz, uint32_t bandwidth_hz,
                                  uint8_t sf, uint8_t cr, int8_t power_dbm,
                                  uint16_t syncword, uint8_t preamble_len,
                                  const char* version) {
    if (!_ready) return;

    char buf[32];
    _display->clearDisplay();
    _display->setTextSize(1);
    _display->setTextColor(OLED_WHITE);

    // Header: version + right-aligned state tag "RADIO" (5 chars → 30 px)
    const char* vs = (version && *version) ? version : "FW:?";
    const char* stateTag = "RADIO";
    int stateX = 128 - (int)strlen(stateTag) * 6;
    int maxVerChars = (stateX / 6) - 1;
    if (maxVerChars < 0) maxVerChars = 0;
    char vbuf[22];
    snprintf(vbuf, sizeof(vbuf), "%s", vs);
    if ((int)strlen(vbuf) > maxVerChars && maxVerChars < (int)sizeof(vbuf)) {
        vbuf[maxVerChars] = '\0';
    }
    _display->setCursor(0, 0);
    _display->print(vbuf);
    _display->setCursor(stateX, 0);
    _display->print(stateTag);
    _display->drawFastHLine(0, 10, 128, OLED_WHITE);

    // Line 1 (y=14) — frequency in MHz with 3 decimals
    float freq_mhz = freq_hz / 1e6f;
    snprintf(buf, sizeof(buf), "F:%.3fMHz", (double)freq_mhz);
    _display->setCursor(0, 14);
    _display->print(buf);

    // Line 2 (y=24) — BW + SF
    float bw_khz = bandwidth_hz / 1000.0f;
    if (bw_khz >= 100.0f) {
        snprintf(buf, sizeof(buf), "BW:%.0fk SF:%u", (double)bw_khz, (unsigned)sf);
    } else {
        snprintf(buf, sizeof(buf), "BW:%.1fk SF:%u", (double)bw_khz, (unsigned)sf);
    }
    _display->setCursor(0, 24);
    _display->print(buf);

    // Line 3 (y=34) — TX power + coding rate
    snprintf(buf, sizeof(buf), "P:%ddBm CR:4/%u", (int)power_dbm, (unsigned)cr);
    _display->setCursor(0, 34);
    _display->print(buf);

    // Line 4 (y=44) — preamble + sync word
    snprintf(buf, sizeof(buf), "Pre:%u SW:0x%02X",
             (unsigned)preamble_len, (unsigned)(syncword & 0xFF));
    _display->setCursor(0, 44);
    _display->print(buf);

    // Heartbeat dot, bottom-right
    static bool dot = false;
    if (dot) _display->fillCircle(124, 60, 2, OLED_WHITE);
    dot = !dot;

    _display->display();
}

void OledDisplay::showDiagnostics(uint32_t uptime_sec,
                                  const char* tcp_client_ip,
                                  uint32_t usb_idle_sec,
                                  uint32_t rx_count, uint32_t tx_count,
                                  uint32_t crc_errors,
                                  const char* version) {
    if (!_ready) return;

    char buf[32];
    _display->clearDisplay();
    _display->setTextSize(1);
    _display->setTextColor(OLED_WHITE);

    // Header: version + right-aligned state tag "DIAG"
    const char* vs = (version && *version) ? version : "FW:?";
    const char* stateTag = "DIAG";
    int stateX = 128 - (int)strlen(stateTag) * 6;
    int maxVerChars = (stateX / 6) - 1;
    if (maxVerChars < 0) maxVerChars = 0;
    char vbuf[22];
    snprintf(vbuf, sizeof(vbuf), "%s", vs);
    if ((int)strlen(vbuf) > maxVerChars && maxVerChars < (int)sizeof(vbuf)) {
        vbuf[maxVerChars] = '\0';
    }
    _display->setCursor(0, 0);
    _display->print(vbuf);
    _display->setCursor(stateX, 0);
    _display->print(stateTag);
    _display->drawFastHLine(0, 10, 128, OLED_WHITE);

    // Line 1 (y=14) — uptime HH:MM:SS (rolls to DDd HH:MM after 100h)
    uint32_t days = uptime_sec / 86400;
    uint32_t h = (uptime_sec % 86400) / 3600;
    uint32_t m = (uptime_sec % 3600) / 60;
    uint32_t s = uptime_sec % 60;
    if (days > 0) {
        snprintf(buf, sizeof(buf), "Up:%lud %02lu:%02lu",
                 (unsigned long)days, (unsigned long)h, (unsigned long)m);
    } else {
        snprintf(buf, sizeof(buf), "Up:%02lu:%02lu:%02lu",
                 (unsigned long)h, (unsigned long)m, (unsigned long)s);
    }
    _display->setCursor(0, 14);
    _display->print(buf);

    // Line 2 (y=24) — TCP client (repeater) status
    _display->setCursor(0, 24);
    if (tcp_client_ip && *tcp_client_ip) {
        snprintf(buf, sizeof(buf), "RPT:%s", tcp_client_ip);
        if (strlen(buf) > 21) buf[21] = '\0';
        _display->print(buf);
    } else {
        _display->print("RPT:no client");
    }

    // Line 3 (y=34) — time since last USB command
    _display->setCursor(0, 34);
    if (usb_idle_sec == UINT32_MAX) {
        _display->print("USB:never");
    } else if (usb_idle_sec < 60) {
        snprintf(buf, sizeof(buf), "USB:%lus ago", (unsigned long)usb_idle_sec);
        _display->print(buf);
    } else {
        snprintf(buf, sizeof(buf), "USB:%lum ago",
                 (unsigned long)(usb_idle_sec / 60));
        _display->print(buf);
    }

    // Line 4 (y=44) — packet counters
    _display->setCursor(0, 44);
    snprintf(buf, sizeof(buf), "RX:%lu TX:%lu E:%lu",
             (unsigned long)rx_count, (unsigned long)tx_count,
             (unsigned long)crc_errors);
    _display->print(buf);

    // Heartbeat dot, bottom-right
    static bool dot = false;
    if (dot) _display->fillCircle(124, 60, 2, OLED_WHITE);
    dot = !dot;

    _display->display();
}

void OledDisplay::showError(const char* msg) {
    if (!_ready) return;
    _display->clearDisplay();
    _display->setTextSize(1);
    _display->setTextColor(OLED_WHITE);
    _display->setCursor(0, 0);
    _display->println("ERROR:");
    _display->println(msg);
    _display->display();
}

void OledDisplay::clear() {
    if (!_ready) return;
    _display->clearDisplay();
    _display->display();
}

void OledDisplay::turnOff() {
    if (!_ready) return;
#if defined(BOARD_STATION_G2)
    _display->oled_command(SH110X_DISPLAYOFF);
#else
    _display->ssd1306_command(SSD1306_DISPLAYOFF);
#endif
}

void OledDisplay::turnOn() {
    if (!_ready) return;
#if defined(BOARD_STATION_G2)
    _display->oled_command(SH110X_DISPLAYON);
#else
    _display->ssd1306_command(SSD1306_DISPLAYON);
#endif
}

// ─── v0.7 cache hooks (originally for T114 TFT) ─────────────────────────
// On Heltec V3 the OLED is too small for a dedicated radio-info / display-name
// screen, so these are stubs. showStatus / showRadioConfig already take the
// radio params they need directly via arguments.

void OledDisplay::setRadioInfo(uint32_t /*freq_hz*/, uint8_t /*sf*/,
                                uint32_t /*bandwidth_hz*/, uint8_t /*cr*/,
                                int8_t /*power_dbm*/, uint8_t /*preamble_len*/,
                                uint16_t /*syncword*/) {
    // no-op: showRadioConfig() takes the same params directly when needed
}

void OledDisplay::setDisplayName(const char* /*name*/) {
    // no-op: Heltec V3 doesn't have the big-name screen the T114 TFT does
}

void OledDisplay::setStandby(bool /*standby*/) {
    // no-op: showStatus's "state" tag handles the standby indicator
}
