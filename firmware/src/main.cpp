// =============================================================
// main.cpp — pymc_modem LoRa Modem firmware
// Serial + Wi-Fi/TCP bridge to SX1262 for pymc_core on RPi.
//
// Supported boards (selected at compile time via -DBOARD_<name>):
//   * Heltec WiFi LoRa 32 V3 (ESP32-S3 + bare SX1262)
//   * Ikoka Stick (XIAO ESP32-S3 + Ebyte E22P868M30S)
//
// USB-CDC @ 921600 baud AND/OR TCP on the port configured via NVS.
// OTA (ArduinoOTA + HTTP) is always-on whenever STA is connected.
//
// All MeshCore protocol logic runs on the RPi in pymc_core.
// =============================================================

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <stdarg.h>
#include "protocol.h"
#include "board_config.h"
#include "frame_parser.h"
#include "compat.h"
#if defined(BOARD_HELTEC_T114)
#  include "node_state.h"
#endif

// Network / OLED / OTA stack only exists on ESP32 boards. The
// nRF52840-based Heltec T114 build excludes those .cpp files via
// platformio.ini's build_src_filter and these headers via the
// #ifdef below.
#ifdef ARDUINO_ARCH_ESP32
#  include <WiFi.h>
#  include <Wire.h>
#  include <esp_task_wdt.h>
#  include <esp_system.h>
#  include <esp_mac.h>
#  if defined(BOARD_HELTEC_TRACKER_V2)
#    include "tft_display.h"
#  else
#    include "oled_display.h"
#  endif
#  include "wifi_manager.h"
#  include "tcp_server.h"
#  include "ota_manager.h"
#  include "ethernet_manager.h"
#  include "runtime_stats.h"
#  include "gps_manager.h"
#else
// nRF52 (Heltec T114) build: the network / OLED / OTA managers are
// excluded from the build via platformio.ini's build_src_filter.
// Provide drop-in stub namespaces + an empty OledDisplay class so
// the existing call sites in main.cpp compile unchanged. All
// methods are no-ops and isSTAConnected() / hasIP() / etc. always
// return false, so the runtime branches that gate on network
// state simply skip.
#include <IPAddress.h>
namespace WifiManager {
    enum class Mode : uint8_t { OFFLINE = 0, STA_CONNECTING = 1,
                                STA_CONNECTED = 2, AP_CONFIG = 3 };
    struct Config {
        String   ssid;
        String   password;
        String   hostname;
        bool     useStaticIP = false;
        IPAddress staticIP;
        IPAddress gateway;
        IPAddress subnet;
        IPAddress dns1;
        IPAddress dns2;
        String   tcpToken;
        uint16_t tcpPort = 0;
        bool     wifiExternalAntenna = false;
        bool     gpsEnabled = false;
    };
    inline void  checkResetButton()  {}
    inline void  begin()             {}
    inline void  loop()              {}
    inline void  loadConfigOnly()    {}
    inline bool  isSTAConnected()    { return false; }
    inline bool  isAPActive()        { return false; }
    inline bool  hasWifiAntennaSwitch() { return false; }
    inline void  applyWifiAntennaSwitch() {}
    inline const char* getSSID()     { return "---"; }
    inline const char* getIPString() { return "---"; }
    inline const char* getHostname() { return "---"; }
    inline Mode  getMode()           { return Mode::OFFLINE; }
    inline const Config& getConfig() { static Config c; return c; }
    inline void  saveConfig(const Config&) {}
    inline void  factoryReset()      {}
}
namespace TCPServer {
    inline void begin(uint16_t, const String&) {}
    inline void loop() {}
    inline void end()  {}
    inline bool isClientReady() { return false; }
    inline void write(const uint8_t*, size_t) {}
    inline String getClientIP() { return String(); }
}
namespace OTAManager {
    inline void begin(const String&, const String&) {}
    inline void loop() {}
    inline void notifyValidFrame() {}
}
namespace EthernetManager {
    inline void begin(const char* = nullptr,
                      bool = false,
                      const IPAddress& = IPAddress((uint32_t)0),
                      const IPAddress& = IPAddress((uint32_t)0),
                      const IPAddress& = IPAddress((uint32_t)0),
                      const IPAddress& = IPAddress((uint32_t)0),
                      const IPAddress& = IPAddress((uint32_t)0)) {}
    inline void end()   {}
    inline void loop()  {}
    inline bool isLinkUp() { return false; }
    inline bool hasIP()    { return false; }
    inline const char* getIPString() { return "---"; }
}
// Per-board display driver on nRF52 boards. T114 ships with an
// LH114T-IF03 TFT-LCD (ST7789, 135×240); the XIAO nRF52840 +
// Wio-SX1262 kit ships with no display at all. The class name
// stays `OledDisplay` regardless so main.cpp's call sites
// compile unchanged.
#if defined(BOARD_HELTEC_T114)
#  include "tft_display.h"
#else
#  include "display_stub.h"
#endif
// Tiny WiFi.* stand-in — only methods main.cpp actually calls when
// has_wifi happens to be true; the firmware branches gate them on
// runtime state which is always false on the T114.
struct _WiFiStub {
    inline IPAddress localIP()    { return IPAddress(); }
    inline IPAddress softAPIP()   { return IPAddress(); }
    inline void setHostname(const char*) {}
    inline void macAddress(uint8_t mac[6]) { compatGetMac(mac); }
};
static _WiFiStub WiFi;
#endif

// ─── Version ─────────────────────────────────────────────────
// Base version is shared by every board; the board's fw_suffix
// distinguishes one binary from another (e.g. "v0.8.0-ikoka").
#define FW_VERSION_BASE "v0.8.0"
static String fwVersion;   // populated in setup()

// ─── Task watchdog — self-heal on loop() hang ───────────────
// A 30 s deadline is comfortably longer than any legitimate loop() burst
// (OTA HTTP upload chunks, CAD scans, OLED redraw) but short enough to
// reboot automatically if ArduinoOTA / WebServer / WifiManager deadlocks.
static constexpr uint32_t LOOP_WDT_TIMEOUT_S = 30;

// ─── Hardware setup ──────────────────────────────────────────
class PymcSX1262 : public SX1262 {
public:
    using SX1262::SX1262;

    int16_t applyRegisterPatch08B5() {
        uint8_t value = 0;
        int16_t state = readRegister(0x08B5, &value, 1);
        if (state != RADIOLIB_ERR_NONE) return state;
        value |= 0x01;
        return writeRegister(0x08B5, &value, 1);
    }
};

// SX1262 / E22P pin map comes from BOARD (see boards/<name>.h).
#if defined(BOARD_PHOTON_1W_XIAO_ESP32C6)
static SPIClass loraSpi(0);
PymcSX1262 radio = new Module(BOARD.pin_lora_nss, BOARD.pin_lora_dio1,
                              BOARD.pin_lora_rst, BOARD.pin_lora_busy,
                              loraSpi);
#else
PymcSX1262 radio = new Module(BOARD.pin_lora_nss, BOARD.pin_lora_dio1,
                              BOARD.pin_lora_rst, BOARD.pin_lora_busy);
#endif

// Single instance regardless of build — on ESP32 this is the real
// SSD1306 driver from oled_display.cpp; on nRF52 it's a no-op stub
// defined above so call sites compile unchanged.
OledDisplay oled;

// ─── Default config: EU/UK (Narrow), Switzerland preset ──────
static RadioConfig currentConfig = {
    .freq_hz      = 869618000,
    .bandwidth_hz = 62500,
    .sf           = 8,
    .cr           = 8,
    .power_dbm    = 22,
    .syncword     = 0x12,
    .preamble_len = 16
};

static StatusResp  status        = {};
// Hard-standby flag set by CMD_RADIO_STANDBY. While true, loop()
// will NOT call startReceive() after a TX/CAD/RX completion, and
// the radio sits in idle. Cleared by CMD_RADIO_RESUME (which
// re-applies the config and re-enters RX).
static bool radioStandby  = false;
static bool autoCadEnabled = false;   // pre-TX CAD; enabled via CMD_SET_AUTO_CAD, persisted in NodeState

// Single DIO1 ISR flag — interpreted as RX_DONE when !isTxActive, otherwise as TX_DONE.
// A single flag avoids the race where an IRQ that fires at the tail of a TX
// could leak into the next RX handler or vice-versa.
static volatile bool dio1Flag    = false;
static volatile uint32_t dio1IrqCount = 0;
static bool        radioReady    = false;
static bool        isTxActive    = false;

// ─── Noise floor sampling (mirrors SX1262Radio._sample_noise_floor) ──
#define NUM_NOISE_FLOOR_SAMPLES 20
#define NOISE_SAMPLING_THRESHOLD 10
static float    noiseFloor       = -99.0f;
static float    noiseFloorSum    = 0.0f;
static int      noiseFloorCount  = 0;
static uint32_t lastPacketTime   = 0;
static uint32_t lastNoiseSample  = 0;

// ─── CAD parameters (set by host via CMD_SET_CAD_PARAMS) ─────
// When cadCustom == false we call scanChannel() with RadioLib's defaults;
// host can override by programming peak/min/symbols/exit_mode.
static bool    cadCustom   = false;
static uint8_t cadSymNum   = 0x01;  // RADIOLIB_SX126X_CAD_ON_2_SYMB — matches pymc_core
static uint8_t cadDetPeak  = 22;    // pymc_core default for SF7-SF8
static uint8_t cadDetMin   = 10;    // AN1200.48 recommendation
static uint8_t cadExitMode = 0x00;  // RADIOLIB_SX126X_CAD_GOTO_STDBY

// ─── Transport state ─────────────────────────────────────────
static FrameParser serialParser;
static FrameParser uartParser;        // protocol UART (Serial2 on nRF52)
static bool        uartEnabled = false;
static bool        tcpStarted    = false;
static bool        otaStarted    = false;
static String      deviceHostname;   // e.g. "heltec-ab12cd" (no .local)

// Hardware UART used for the protocol when BOARD.pin_protocol_uart_*
// is wired. nRF52 (T114) → Serial2 on the variant's PIN_SERIAL2_*.
// ESP32 (Arduino-ESP32) doesn't auto-instantiate Serial2 the same
// way, but every supported ESP32 board in this firmware speaks the
// protocol over USB-CDC anyway, so we leave the UART path as
// nRF52-only for now.
#ifdef ARDUINO_ARCH_ESP32
#  define PROTO_UART  Serial1
#else
#  define PROTO_UART  Serial2
#endif

// Timing
static uint32_t lastOledUpdate = 0;

// OLED sleep timer + screen cycle
enum class Screen : uint8_t { SLEEP = 0, STATUS = 1, RADIO = 2, DIAGNOSTICS = 3 };
static Screen   currentScreen  = Screen::SLEEP;
static constexpr uint32_t OLED_WAKE_DURATION_MS = 30000;
static constexpr uint32_t PRG_DEBOUNCE_MS       = 200;
static constexpr uint32_t PRG_WIFI_RESET_HOLD_MS = 5000;
// pyMC splash holds for at least SPLASH_HOLD_MS while setup() runs Wi-Fi /
// Ethernet / radio init in parallel. End-of-setup waits out any remainder.
static constexpr uint32_t SPLASH_HOLD_MS        = 5000;
// Boards without a usable PRG/BOOT button (pin_user_button < 0) cycle
// STATUS→RADIO→DIAGNOSTICS→STATUS automatically every SCREEN_AUTO_CYCLE_MS.
// Boards with a working button keep the manual short-tap cycle and ignore this.
static constexpr uint32_t SCREEN_AUTO_CYCLE_MS  = 4000;
static uint32_t oledWakeUntil    = 0;
static uint32_t prgIgnoreUntil   = 0;
static uint32_t prgPressedSince  = 0;
static bool     prgWasPressed    = false;
static bool     prgLongHandled   = false;
static uint32_t splashStartedMs  = 0;
static uint32_t lastAutoCycleMs  = 0;

// Worst-case loop() iteration time observed since boot. Reported via
// CMD_GET_DEBUG so we can spot watchdog-bait blocking calls without
// a serial cable. Reset on overflow doesn't matter — value is rolling
// max in microseconds.
static uint32_t maxLoopUs = 0;

// Host-link health for the DIAGNOSTICS screen: millis() of the last USB
// frame that parsed cleanly. 0 = no frame yet since boot.
static uint32_t lastUsbCmdMs = 0;

#ifdef ARDUINO_ARCH_ESP32
static bool readFuelGaugeRegister(uint8_t address, uint8_t reg, uint16_t& value) {
    Wire.setTimeOut(50);
    Wire.beginTransmission(address);
    Wire.write(reg);
    // Use a STOP between the register select and read.  The MAX17048 accepts
    // this, and it avoids the ESP32-C6 Arduino core's repeated-start recovery
    // path, which can wedge long enough to trip our loop watchdog when the
    // Photon I2C bus/fuel gauge is absent or not pulled up.
    if (Wire.endTransmission(true) != 0) {
        return false;
    }
    if (Wire.requestFrom(address, (uint8_t)2) != 2) {
        return false;
    }
    value = ((uint16_t)Wire.read() << 8) | Wire.read();
    return true;
}

static uint16_t readBatteryMilliVolts() {
    if (BOARD.battery.fuel_gauge_i2c_addr != 0) {
        uint16_t vcell = 0;
        if (readFuelGaugeRegister(BOARD.battery.fuel_gauge_i2c_addr,
                                  BOARD.battery.fuel_gauge_vcell_reg,
                                  vcell)) {
            // MAX17048 VCELL uses 78.125 uV/LSB units, same conversion as
            // the original MeshCore Photon firmware: vcell * 5 / 64 mV.
            uint32_t mv = ((uint32_t)vcell * 5U) / 64U;
            return mv > 65534U ? 65534U : (uint16_t)mv;
        }
        return 0xFFFF;
    }

    if (BOARD.battery.pin < 0 || BOARD.battery.multiplier <= 0.0f) {
        return 0xFFFF;
    }

    if (BOARD.battery.enable_pin >= 0) {
        pinMode(BOARD.battery.enable_pin, OUTPUT);
        digitalWrite(BOARD.battery.enable_pin,
                     BOARD.battery.enable_active_high ? HIGH : LOW);
        delay(5);
    }

    uint32_t totalMv = 0;
    constexpr uint8_t samples = 8;
    for (uint8_t i = 0; i < samples; ++i) {
        totalMv += analogReadMilliVolts(BOARD.battery.pin);
        delay(1);
    }
    float packMv = (totalMv / (float)samples) * BOARD.battery.multiplier;
    if (packMv < 0.0f) return 0;
    if (packMv > 65534.0f) return 65534;
    return (uint16_t)(packMv + 0.5f);
}

static bool readBatteryChargeRatePctPerHour(float& pctPerHour) {
    if (BOARD.battery.fuel_gauge_i2c_addr == 0 ||
        BOARD.battery.fuel_gauge_crate_reg == 0) {
        return false;
    }

    uint16_t crate = 0;
    if (!readFuelGaugeRegister(BOARD.battery.fuel_gauge_i2c_addr,
                               BOARD.battery.fuel_gauge_crate_reg,
                               crate)) {
        return false;
    }

    // MAX17048 CRATE is signed, 0.208 %/hr per LSB. MeshCore surfaced this
    // as current; on Photon it is actually the battery charge/discharge rate.
    pctPerHour = (float)((int16_t)crate) * 0.208f;
    return true;
}

namespace RuntimeStats {
Snapshot capture() {
    Snapshot snap = {};
    snap.status = status;
    snap.status.uptime_sec = millis() / 1000;
    snap.status.radio_state = radioStandby ? 2 : (isTxActive ? 1 : 0);
    snap.status.temp_c = (int8_t)temperatureRead();
    snap.status.noise_floor_x10 = (int16_t)(noiseFloor * 10.0f);
    snap.status.battery_mv = readBatteryMilliVolts();
    snap.radio = currentConfig;
    snap.firmwareVersion = fwVersion;
    snap.radioStandby = radioStandby;
    snap.autoCadEnabled = autoCadEnabled;
    snap.hasBatteryChargeRatePctPerHour = BOARD.battery.fuel_gauge_i2c_addr != 0 &&
        BOARD.battery.fuel_gauge_crate_reg != 0;
    if (snap.hasBatteryChargeRatePctPerHour) {
        snap.batteryChargeRatePctPerHourValid = readBatteryChargeRatePctPerHour(
            snap.batteryChargeRatePctPerHour);
    }
    return snap;
}
}
#endif

// ─── ISR callback ────────────────────────────────────────────
#if defined(ESP32)
IRAM_ATTR
#endif
void onDio1Rise() {
    dio1Flag = true;
    dio1IrqCount = dio1IrqCount + 1;
}

// ─── E22 RF switch boot sequence ────────────────────────────
// Some carrier boards (Ebyte E22P series, see datasheet §4.2) need
// their EN pin held LOW for several seconds at power-up so the LDOs
// and PA bias can settle before RF traffic starts. After the hold,
// EN goes HIGH and stays there forever — never toggled by the radio
// path. Boards without an external switch (en_pin == -1) skip both
// steps entirely.
static uint32_t enLowStartedMs = 0;

static void rfSwitchEnLowAtBoot() {
    // Boards without a LoRa front end (e.g. ESP32-P4-Nano on day one)
    // list pin numbers for documentation but the module is not actually
    // wired — skip every RF-switch action so we don't drive a pin into
    // an unused circuit and don't pay the multi-second settle delay.
    if (!BOARD.has_lora_radio) return;
    if (BOARD.rf_switch.en_pin < 0) return;
    pinMode(BOARD.rf_switch.en_pin, OUTPUT);
    digitalWrite(BOARD.rf_switch.en_pin, LOW);
    enLowStartedMs = millis();
}

static void writeOutputPin(int8_t pin, bool high) {
    if (pin < 0) return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, high ? HIGH : LOW);
}

static void setTxLed(bool on) {
    if (BOARD.pin_lora_tx_led < 0) return;
    bool high = on ? BOARD.lora_tx_led_active_high
                   : !BOARD.lora_tx_led_active_high;
    writeOutputPin(BOARD.pin_lora_tx_led, high);
}

static void txLedInitAtBoot() {
    setTxLed(false);
}
static void rfSwitchEnHighAfterSettle() {
    if (!BOARD.has_lora_radio) return;
    if (BOARD.rf_switch.en_pin < 0) return;
    uint32_t elapsed = millis() - enLowStartedMs;
    if (elapsed < BOARD.rf_switch.en_low_hold_ms) {
        uint32_t remaining = BOARD.rf_switch.en_low_hold_ms - elapsed;
        // Feed the watchdog every second while we wait so the 30 s
        // task watchdog stays happy on long holds.
        while (remaining > 0) {
            uint32_t step = remaining > 1000 ? 1000 : remaining;
            delay(step);
            compatWdtReset();
            remaining -= step;
        }
    }
    digitalWrite(BOARD.rf_switch.en_pin, HIGH);
    delay(20);   // small post-rise settle before we hit the SPI bus
}

static void configureStaticGpios() {
    if (!BOARD.has_lora_radio) return;
    uint8_t count = BOARD.static_gpio_count;
    if (count > (sizeof(BOARD.static_gpios) / sizeof(BOARD.static_gpios[0]))) {
        count = sizeof(BOARD.static_gpios) / sizeof(BOARD.static_gpios[0]);
    }
    for (uint8_t i = 0; i < count; ++i) {
        const auto& gpio = BOARD.static_gpios[i];
        if (gpio.pin < 0) continue;
        pinMode(gpio.pin, OUTPUT);
        digitalWrite(gpio.pin, gpio.level_high ? HIGH : LOW);
    }
}

static void rfSwitchConfigureRadio() {
    // DIO2 + explicit RX/TX pins are independent — the Wio-SX1262 board uses
    // both: DIO2 drives the TX path internally while RXEN (an external GPIO)
    // gates the LNA on the RX path. Older boards (Heltec V3 / T3S3 / RAK3112)
    // only set dio2_as_rf_switch and leave rx_pin/tx_pin == -1, so the second
    // call becomes a no-op for them.
    if (BOARD.rf_switch.dio2_as_rf_switch) {
        int state = radio.setDio2AsRfSwitch(true);
        Serial.printf("[INFO] setDio2AsRfSwitch(true) -> %d\n", state);
    }
    if (BOARD.rf_switch.rx_pin >= 0 || BOARD.rf_switch.tx_pin >= 0) {
        uint32_t rx = BOARD.rf_switch.rx_pin >= 0
                          ? (uint32_t)BOARD.rf_switch.rx_pin
                          : RADIOLIB_NC;
        uint32_t tx = BOARD.rf_switch.tx_pin >= 0
                          ? (uint32_t)BOARD.rf_switch.tx_pin
                          : RADIOLIB_NC;
        radio.setRfSwitchPins(rx, tx);
        Serial.printf("[INFO] setRfSwitchPins(rx=%lu tx=%lu)\n",
                      (unsigned long)rx, (unsigned long)tx);
    }
}

static void configureBoardRadioOptions() {
    if (BOARD.sx126x_current_limit_ma > 0) {
        int state = radio.setCurrentLimit(BOARD.sx126x_current_limit_ma);
        Serial.printf("[INFO] setCurrentLimit(%d mA) -> %d\n",
                      (int)BOARD.sx126x_current_limit_ma, state);
    }

    if (BOARD.sx126x_rx_boosted_gain) {
        int state = radio.setRxBoostedGainMode(true);
        Serial.printf("[INFO] setRxBoostedGainMode(true) -> %d\n", state);
    }

    if (BOARD.sx126x_register_patch) {
        int state = radio.applyRegisterPatch08B5();
        Serial.printf("[INFO] SX126x register patch 0x08B5 -> %d\n", state);
    }
}

// ─── Frame output ────────────────────────────────────────────
static void writeFrame(uint8_t cmd, const uint8_t* payload, uint16_t len,
                       bool toSerial, bool toTCP, bool toUart) {
    uint8_t buf[MAX_FRAME_SIZE];
    uint16_t i = 0;
    buf[i++] = PROTO_SYNC;
    buf[i++] = cmd;
    buf[i++] = len & 0xFF;
    buf[i++] = (len >> 8) & 0xFF;
    if (len > 0 && payload) {
        memcpy(buf + i, payload, len);
        i += len;
    }
    uint16_t crc = crc16_ccitt(buf + 1, 3 + len);
    buf[i++] = crc & 0xFF;
    buf[i++] = (crc >> 8) & 0xFF;

    if (toSerial) {
        // Skip Serial.flush(): on ESP32-S3 TinyUSB, flush blocks indefinitely
        // when the host stops reading from /dev/ttyACM. USB-CDC is buffered by
        // the stack and delivered asynchronously — not calling flush is the
        // recommended pattern and was identified as one cause of loop() hang
        // in v0.5.4.
        Serial.write(buf, i);
    }
    if (toTCP) {
        TCPServer::write(buf, i);
    }
    if (toUart && uartEnabled) {
        PROTO_UART.write(buf, i);
    }
}

void sendFrame(uint8_t cmd, const uint8_t* payload, uint16_t len, TransportSource dest) {
    writeFrame(cmd, payload, len,
               dest == TransportSource::USB,
               dest == TransportSource::TCP,
               dest == TransportSource::UART);
}

void sendError(uint8_t errCode, TransportSource dest) {
    sendFrame(CMD_ERROR, &errCode, 1, dest);
}

void broadcastFrame(uint8_t cmd, const uint8_t* payload, uint16_t len) {
    // Async events (RX_PACKET, TX_DONE, TX_FAIL): fan out to every
    // active host transport so whichever one the controller is
    // listening on receives the event.
    writeFrame(cmd, payload, len,
               /*toSerial=*/true,
               /*toTCP=*/TCPServer::isClientReady(),
               /*toUart=*/uartEnabled);
}

// ─── Remote log → host (CMD_LOG_MSG) ─────────────────────────
// Sends the formatted line up the UART (when enabled) so the P4
// controller can aggregate sector logs in its central LogBuf.
// Always also lands on local Serial (USB-CDC) — operator at the
// console doesn't lose anything when logRemote is used.
//
// Levels: 0=INFO, 1=WARN, 2=ERR (matches LogBuf::Level on P4).
//
// Payload format on the wire (CMD_LOG_MSG = 0x80):
//   level(1) | text(N≤200, no NUL terminator)
static void logRemote(uint8_t level, const char* fmt, ...) {
    char text[200];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof(text)) n = sizeof(text) - 1;

    // Local USB-CDC mirror, prefixed so dual-port debugging matches
    // the dashboard's source labels.
    const char* tag = (level == 2) ? "ERR " : (level == 1) ? "WARN" : "INFO";
    Serial.printf("[%s] %s\n", tag, text);

    if (!uartEnabled) return;
    uint8_t frame[1 + (int)sizeof(text)];
    frame[0] = level;
    memcpy(frame + 1, text, (size_t)n);
    // Send only over UART — P4 is the consumer; USB host already
    // got the same line via the Serial.printf above. TCP path is
    // unused on T114 (has_network=false) so explicit .write below.
    {
        uint8_t buf[8 + sizeof(frame)];
        uint16_t i = 0;
        buf[i++] = PROTO_SYNC;
        buf[i++] = CMD_LOG_MSG;
        uint16_t len = (uint16_t)(1 + n);
        buf[i++] = len & 0xFF;
        buf[i++] = (len >> 8) & 0xFF;
        memcpy(buf + i, frame, len); i += len;
        uint16_t crc = crc16_ccitt(buf + 1, 3 + len);
        buf[i++] = crc & 0xFF;
        buf[i++] = (crc >> 8) & 0xFF;
        PROTO_UART.write(buf, i);
    }
}

#define LOG_R_INFO(...) logRemote(0, __VA_ARGS__)
#define LOG_R_WARN(...) logRemote(1, __VA_ARGS__)
#define LOG_R_ERR(...)  logRemote(2, __VA_ARGS__)

// ─── WIFI_STATUS response builder ───────────────────────────
// Payload: mode(1) ip(4,BE) port(2,LE) ssid_len(1) ssid(N) host_len(1) host(M)
static uint16_t buildWifiStatusPayload(uint8_t* out) {
    uint16_t i = 0;

    uint8_t mode;
    switch (WifiManager::getMode()) {
        case WifiManager::Mode::STA_CONNECTED:  mode = 2; break;
        case WifiManager::Mode::STA_CONNECTING: mode = 1; break;
        case WifiManager::Mode::AP_CONFIG:      mode = 3; break;
        default:                                mode = 0; break;
    }
    out[i++] = mode;

    IPAddress ip = WifiManager::isSTAConnected() ? WiFi.localIP()
                 : WifiManager::isAPActive()     ? WiFi.softAPIP()
                                                 : IPAddress((uint32_t)0);
    out[i++] = ip[0];  // big-endian dotted quad
    out[i++] = ip[1];
    out[i++] = ip[2];
    out[i++] = ip[3];

    uint16_t port = WifiManager::getConfig().tcpPort;
    out[i++] = port & 0xFF;
    out[i++] = (port >> 8) & 0xFF;

    const char* ssid = WifiManager::getSSID();
    uint8_t ssid_len = ssid ? (uint8_t)strnlen(ssid, 32) : 0;
    out[i++] = ssid_len;
    if (ssid_len) { memcpy(out + i, ssid, ssid_len); i += ssid_len; }

    const char* host = WifiManager::getHostname();
    uint8_t host_len = host ? (uint8_t)strnlen(host, 32) : 0;
    if (host_len > 32) host_len = 32;
    out[i++] = host_len;
    if (host_len) { memcpy(out + i, host, host_len); i += host_len; }

    return i;
}

// ─── SET_WIFI payload parser ────────────────────────────────
// Layout: ssid_len(1) ssid(N) pass_len(1) pass(M) port(2,LE)
//         tok_len(1) tok(K) [host_len(1) host(H)]
// Only meaningful when the firmware actually has a Wi-Fi stack;
// the nRF52 build (Heltec T114) doesn't, so the parser is gone
// from the binary entirely.
#ifdef ARDUINO_ARCH_ESP32
static bool parseSetWifi(const uint8_t* p, uint16_t len, WifiManager::Config& out) {
    out = WifiManager::getConfig();   // preserve static IP settings by default
    uint16_t i = 0;

    if (i + 1 > len) return false;
    uint8_t slen = p[i++];
    if (slen == 0 || slen > 32 || i + slen > len) return false;
    out.ssid = String((const char*)(p + i), slen);
    i += slen;

    if (i + 1 > len) return false;
    uint8_t plen = p[i++];
    if (plen > 64 || i + plen > len) return false;
    out.password = plen ? String((const char*)(p + i), plen) : String();
    i += plen;

    if (i + 2 > len) return false;
    uint16_t port = p[i] | ((uint16_t)p[i+1] << 8);
    i += 2;
    if (port == 0) return false;
    out.tcpPort = port;

    if (i + 1 > len) return false;
    uint8_t tlen = p[i++];
    if (tlen > 64 || i + tlen > len) return false;
    out.tcpToken = tlen ? String((const char*)(p + i), tlen) : String();
    i += tlen;

    if (i < len) {
        if (i + 1 > len) return false;
        uint8_t hlen = p[i++];
        if (hlen > 32 || i + hlen > len) return false;
        out.hostname = hlen ? String((const char*)(p + i), hlen) : String();
        i += hlen;
    }

    if (i != len) return false;

    out.useStaticIP = false;   // USB provisioning = DHCP only
    return true;
}
#endif

// ─── Radio configuration ────────────────────────────────────
bool applyConfig(const RadioConfig& cfg) {
    radio.standby();
    int state;

    state = radio.setFrequency(cfg.freq_hz / 1e6f);
    if (state != RADIOLIB_ERR_NONE) return false;

    state = radio.setBandwidth(cfg.bandwidth_hz / 1000.0f);
    if (state != RADIOLIB_ERR_NONE) return false;

    state = radio.setSpreadingFactor(cfg.sf);
    if (state != RADIOLIB_ERR_NONE) return false;

    state = radio.setCodingRate(cfg.cr);
    if (state != RADIOLIB_ERR_NONE) return false;

    // Hardware ceiling per board (E22P868M30S = 30 dBm, bare SX1262 = 22).
    int8_t pwr = cfg.power_dbm;
    if (pwr > BOARD.max_tx_power_dbm) pwr = BOARD.max_tx_power_dbm;
    state = radio.setOutputPower(pwr);
    if (state != RADIOLIB_ERR_NONE) return false;

    state = radio.setSyncWord(cfg.syncword);
    if (state != RADIOLIB_ERR_NONE) return false;

    state = radio.setPreambleLength(cfg.preamble_len);
    if (state != RADIOLIB_ERR_NONE) return false;

    radio.explicitHeader();
    radio.setCRC(1);
    radio.invertIQ(false);

    // Auto-LDRO mirrors pymc_core sx1262_wrapper.py — without this,
    // SF11/SF12 presets are modulation-incompatible with pymc_core.
    radio.autoLDRO();

    // Push the live config to the TFT cache so the next status
    // refresh shows what the radio is actually running.
    oled.setRadioInfo(cfg.freq_hz, cfg.sf, cfg.bandwidth_hz, cfg.cr, pwr,
                     status.last_rssi, status.last_snr);
    return true;
}

bool startReceive() {
    // Hard standby blocks every code path that would put the
    // radio into RX. The flag is cleared by CMD_RADIO_RESUME
    // which re-runs applyConfig() and then calls this function
    // again with radioStandby=false.
    if (radioStandby) return true;
    return radio.startReceive() == RADIOLIB_ERR_NONE;
}

// ─── Handle received LoRa packet ────────────────────────────
void handleLoRaRx() {
    int len = radio.getPacketLength();
    if (len <= 0 || len > MAX_LORA_PAYLOAD) {
        startReceive();
        return;
    }

    uint8_t rxBuf[MAX_LORA_PAYLOAD];
    int state = radio.readData(rxBuf, len);
    if (state != RADIOLIB_ERR_NONE) {
        status.crc_errors++;
        startReceive();
        return;
    }

    int16_t rssi = (int16_t)radio.getRSSI();
    int16_t snr  = (int16_t)(radio.getSNR() * 10.0f);
    int16_t signal_rssi = rssi;

    status.rx_count++;
    status.last_rssi = rssi;
    status.last_snr  = snr;

    // TFT cache — display the freshest received-packet quality
    // alongside the rest of the radio state on the next status
    // refresh. Config fields stay at whatever applyConfig set.
    oled.setRadioInfo(currentConfig.freq_hz, currentConfig.sf,
                     currentConfig.bandwidth_hz, currentConfig.cr,
                     currentConfig.power_dbm,
                     rssi, snr);

    uint8_t rxPayload[6 + MAX_LORA_PAYLOAD];
    rxPayload[0] = rssi & 0xFF;
    rxPayload[1] = (rssi >> 8) & 0xFF;
    rxPayload[2] = snr & 0xFF;
    rxPayload[3] = (snr >> 8) & 0xFF;
    rxPayload[4] = signal_rssi & 0xFF;
    rxPayload[5] = (signal_rssi >> 8) & 0xFF;
    memcpy(rxPayload + 6, rxBuf, len);

    broadcastFrame(CMD_RX_PACKET, rxPayload, 6 + len);
    lastPacketTime = millis();
    startReceive();
}

// ─── Host command dispatch ──────────────────────────────────
void processHostCommand(uint8_t cmd, const uint8_t* payload, uint16_t len,
                        TransportSource src) {
    // Any successfully-processed host frame counts toward OTA sanity.
    OTAManager::notifyValidFrame();

    // Boards without a LoRa radio, or boards where SX1262 init failed,
    // ack the non-radio commands (PING, GET_VERSION, GET_WIFI, AUTH, …)
    // but refuse anything that would touch the SX1262. The host can still
    // probe the modem and configure Wi-Fi via the existing flow.
    if (!BOARD.has_lora_radio || !radioReady) {
        switch (cmd) {
        case CMD_TX_REQUEST:  case CMD_SET_CONFIG:  case CMD_GET_CONFIG:
        case CMD_STATUS_REQ:  case CMD_NOISE_REQ:   case CMD_CAD_REQUEST:
        case CMD_RX_START:    case CMD_SET_CAD_PARAMS:
            sendError(ERR_NO_RADIO, src);
            return;
        default:
            break;  // PING / GET_VERSION / WiFi / AUTH stay live
        }
    }

    switch (cmd) {

    case CMD_TX_REQUEST: {
        if (len == 0 || len > MAX_LORA_PAYLOAD) {
            sendError(ERR_PAYLOAD_TOO_BIG, src);
            break;
        }
        // v0.5.7: non-blocking TX with our own timeout.
        // The previous radio.transmit() was synchronous and could wait
        // indefinitely when SX1262 lost the TX_DONE IRQ (observed after CAD
        // timeouts), which in turn blocked loop() long enough for the 30 s
        // task watchdog to reboot the firmware every minute.
        isTxActive = true;
        radio.standby();
        delay(1);

        // ─── Auto-CAD before TX (when enabled by controller) ──
        // Up to CAD_AUTO_RETRIES of CAD-with-backoff. If the
        // channel is busy after every retry, we bail with
        // ERR_CHANNEL_BUSY instead of trampling a neighbour.
        // Local decision (lowest latency vs P4-managed equivalent).
        if (autoCadEnabled) {
            // 2 retries (3 scans total) + tightened jitter caps
            // worst-case main-loop blocking around ~450 ms (was ~1 s
            // before). Important because the loop also drains the
            // UART RX ring — at 921600 baud the controller can push
            // ~46 KB/s and our SERIAL_BUFFER_SIZE is 512 B.
            constexpr uint8_t  CAD_AUTO_RETRIES   = 2;
            constexpr uint32_t CAD_TIMEOUT_MS     = 200;   // worst-case SF12 ≈ 100 ms
            bool channel_clear = false;
            for (uint8_t attempt = 0; attempt < CAD_AUTO_RETRIES; attempt++) {
                ChannelScanConfig_t cfg = {};
                cfg.cad.symNum    = cadSymNum;
                cfg.cad.detPeak   = cadDetPeak;
                cfg.cad.detMin    = cadDetMin;
                cfg.cad.exitMode  = cadExitMode;
                cfg.cad.irqFlags  = RADIOLIB_IRQ_CAD_DEFAULT_FLAGS;
                cfg.cad.irqMask   = RADIOLIB_IRQ_CAD_DEFAULT_MASK;
                dio1Flag = false;
                if (radio.startChannelScan(cfg) != RADIOLIB_ERR_NONE) break;
                uint32_t cad_t0 = millis();
                while (!dio1Flag && (millis() - cad_t0) < CAD_TIMEOUT_MS) {
                    compatWdtReset();
                    delay(2);
                }
                uint16_t irq = radio.getIrqFlags();
                radio.clearIrqFlags(RADIOLIB_IRQ_CAD_DEFAULT_FLAGS);
                bool busy = (irq & RADIOLIB_SX126X_IRQ_CAD_DETECTED) != 0;
                if (!busy) { channel_clear = true; break; }
                // Random backoff 50-200 ms to prevent step-locking
                // with another sector that retried at the same time.
                delay(20 + (millis() & 0x1F));
            }
            if (!channel_clear) {
                LOG_R_WARN("auto-CAD: channel busy after retries, abort TX");
                isTxActive = false;
                sendError(ERR_CHANNEL_BUSY, src);
                startReceive();
                break;
            }
        }

        dio1Flag = false;
        uint32_t irqStart = dio1IrqCount;
        setTxLed(true);
        int state = radio.startTransmit((uint8_t*)payload, len);
        LOG_R_INFO("TX_REQUEST len=%u src=%u state=%d",
                   (unsigned)len, (unsigned)src, state);
        if (state != RADIOLIB_ERR_NONE) {
            LOG_R_ERR("startTransmit() failed, state=%d", state);
            isTxActive = false;
            radio.finishTransmit();
            setTxLed(false);
            sendError(ERR_TX_TIMEOUT, src);
            startReceive();
            break;
        }

        // Worst-case airtime for SF12/BW7.8k at 255 B ≈ 20 s, but the
        // sector controller upstream caps a per-sector slot at ~4 s.
        // Aligning closely (4500 ms) so a wedged-radio LOG_R_ERR
        // ("hard timeout") still reaches the controller right after
        // its own TIMEOUT log — useful for distinguishing "modem is
        // stuck waiting on DIO1" from "modem finished but TX_DONE
        // didn't make it back through UART".
        const uint32_t TX_TIMEOUT_MS = 4500;
        uint32_t txStart = millis();
        while (!dio1Flag && (millis() - txStart) < TX_TIMEOUT_MS) {
            compatWdtReset();   // keep watchdog happy while we poll
            delay(2);
        }

        bool txOk = dio1Flag;
        radio.finishTransmit();
        setTxLed(false);
        dio1Flag = false;
        isTxActive = false;
        lastPacketTime = millis();

        if (txOk) {
            status.tx_count++;
            uint32_t airtime_us = radio.getTimeOnAir(len);
            uint8_t resp[4];
            resp[0] = airtime_us & 0xFF;
            resp[1] = (airtime_us >> 8) & 0xFF;
            resp[2] = (airtime_us >> 16) & 0xFF;
            resp[3] = (airtime_us >> 24) & 0xFF;
            sendFrame(CMD_TX_DONE, resp, 4, src);
            LOG_R_INFO("TX_DONE airtime=%lu us, sent via src=%u",
                       (unsigned long)airtime_us, (unsigned)src);
        } else {
            // Hard TX timeout — the SX1262 is likely stuck in a bad state.
            // Rebuild from scratch: standby → re-apply full config → RX.
            LOG_R_ERR("TX hard timeout (%u bytes, dio1_delta=%lu) — resetting radio",
                      (unsigned)len, (unsigned long)(dio1IrqCount - irqStart));
            radio.standby();
            delay(5);
            applyConfig(currentConfig);
            sendError(ERR_TX_TIMEOUT, src);
        }
        startReceive();
        break;
    }

    case CMD_CAD_REQUEST: {
        // v0.5.8: non-blocking CAD with our own timeout.
        // The previous radio.scanChannel() was synchronous and would block
        // until CAD_DONE IRQ arrived. When SX1262 dropped that IRQ (first
        // scan after setCAD, or generally unreliable behaviour at SF8/BW62k)
        // loopTask sat in scanChannel for tens of seconds without feeding
        // the task watchdog — reboot cycle every ~60 s.
        radio.standby();
        delay(1);

        dio1Flag = false;
        int state;
        if (cadCustom) {
            // v0.5.9: IRQ flags & mask MUST be set — zero-initialised cfg
            // made RadioLib call setDioIrqParams(0, 0), so CAD_DONE/DETECTED
            // never routed to DIO1 and our dio1Flag polling always timed
            // out. That turned every CAD into ERR_CAD_FAILED.
            ChannelScanConfig_t cfg = {};
            cfg.cad.symNum   = cadSymNum;
            cfg.cad.detPeak  = cadDetPeak;
            cfg.cad.detMin   = cadDetMin;
            cfg.cad.exitMode = cadExitMode;
            cfg.cad.timeout  = 0;
            cfg.cad.irqFlags = RADIOLIB_IRQ_CAD_DEFAULT_FLAGS;
            cfg.cad.irqMask  = RADIOLIB_IRQ_CAD_DEFAULT_MASK;
            state = radio.startChannelScan(cfg);
        } else {
            state = radio.startChannelScan();   // overload already sets flags
        }

        if (state != RADIOLIB_ERR_NONE) {
            sendError(ERR_CAD_FAILED, src);
            startReceive();
            break;
        }

        // 500 ms easily covers a legitimate CAD scan at any SF+symNum we use.
        const uint32_t CAD_TIMEOUT_MS = 500;
        uint32_t cadStart = millis();
        while (!dio1Flag && (millis() - cadStart) < CAD_TIMEOUT_MS) {
            compatWdtReset();
            delay(1);
        }

        if (!dio1Flag) {
            // CAD_DONE never fired — treat as failure and clean up the chip
            // before the next request. Don't block the repeater's LBT
            // forever; reporting failure lets the host decide.
            LOG_R_WARN("CAD IRQ timeout — resetting radio");
            radio.standby();
            delay(5);
            applyConfig(currentConfig);
            sendError(ERR_CAD_FAILED, src);
            startReceive();
            break;
        }

        int scanResult = radio.getChannelScanResult();
        dio1Flag = false;
        uint8_t result[1];
        if (scanResult == RADIOLIB_LORA_DETECTED) {
            result[0] = 1;
        } else if (scanResult == RADIOLIB_CHANNEL_FREE) {
            result[0] = 0;
        } else {
            sendError(ERR_CAD_FAILED, src);
            startReceive();
            break;
        }
        sendFrame(CMD_CAD_RESP, result, 1, src);
        break;
    }

    case CMD_SET_CAD_PARAMS: {
        if (len != 4) {
            sendError(ERR_INVALID_CONFIG, src);
            break;
        }
        cadSymNum   = payload[0];
        cadDetPeak  = payload[1];
        cadDetMin   = payload[2];
        cadExitMode = payload[3];
        cadCustom   = true;

        // Ack before letting the chip settle. A blocking primer scan here
        // was attempted in an earlier v0.5.5 draft and itself hung — the
        // SX1262 can miss CAD_DONE during the regs-write window and leave
        // scanChannel() waiting forever. A short settle delay is enough
        // for the register write to commit; the first real CAD_REQUEST
        // from the host will then behave normally.
        sendFrame(CMD_CAD_PARAMS_RESP, payload, 4, src);
        delay(30);
        break;
    }

    case CMD_RX_START: {
        startReceive();
        sendFrame(CMD_RX_STARTED, nullptr, 0, src);
        break;
    }

    case CMD_SET_CONFIG: {
        if (len != sizeof(RadioConfig)) {
            sendError(ERR_INVALID_CONFIG, src);
            break;
        }
        memcpy(&currentConfig, payload, sizeof(RadioConfig));
        if (applyConfig(currentConfig)) {
            sendFrame(CMD_CONFIG_RESP, (uint8_t*)&currentConfig, sizeof(RadioConfig), src);
            startReceive();
        } else {
            sendError(ERR_INVALID_CONFIG, src);
        }
        break;
    }

    case CMD_GET_CONFIG: {
        sendFrame(CMD_CONFIG_RESP, (uint8_t*)&currentConfig, sizeof(RadioConfig), src);
        break;
    }

    case CMD_STATUS_REQ: {
        status.uptime_sec = millis() / 1000;
        status.radio_state = isTxActive ? 1 : 0;
#ifdef ARDUINO_ARCH_ESP32
        status.temp_c = (int8_t)temperatureRead();
        status.battery_mv = readBatteryMilliVolts();
#else
        status.temp_c = 0;   // nRF52 has its own temperature sensor — TODO
        status.battery_mv = 0xFFFF;
#endif
        status.noise_floor_x10 = (int16_t)(noiseFloor * 10.0f);
        sendFrame(CMD_STATUS_RESP, (uint8_t*)&status, sizeof(StatusResp), src);
        break;
    }

    case CMD_NOISE_REQ: {
        int16_t nf = (int16_t)(noiseFloor * 10.0f);
        uint8_t resp[2];
        resp[0] = nf & 0xFF;
        resp[1] = (nf >> 8) & 0xFF;
        sendFrame(CMD_NOISE_RESP, resp, 2, src);
        break;
    }

    case CMD_GET_WIFI: {
        uint8_t buf[200];
        uint16_t n = buildWifiStatusPayload(buf);
        sendFrame(CMD_WIFI_STATUS, buf, n, src);
        break;
    }

    case CMD_SET_WIFI: {
#ifndef ARDUINO_ARCH_ESP32
        sendError(ERR_INVALID_CMD, src);   // no Wi-Fi stack on this build
        break;
#else
        // Remote provisioning over USB — eliminates the need to physically
        // connect to the Heltec's AP portal.
        WifiManager::Config newCfg;
        if (!parseSetWifi(payload, len, newCfg)) {
            sendError(ERR_INVALID_WIFI, src);
            break;
        }

        // Ack with the pending config so the host can log it BEFORE reboot.
        WifiManager::saveConfig(newCfg);
        uint8_t buf[200];
        uint16_t n = buildWifiStatusPayload(buf);
        sendFrame(CMD_WIFI_STATUS, buf, n, src);

        if (src == TransportSource::USB) Serial.flush();
        delay(200);
        ESP.restart();
        break;  // unreached
#endif
    }

    case CMD_GET_VERSION: {
        const char* v = fwVersion.c_str();
        sendFrame(CMD_VERSION_RESP, (const uint8_t*)v, (uint16_t)strlen(v), src);
        break;
    }

    case CMD_GET_DEBUG: {
        // Snapshot for crash-loop diagnosis without a serial cable.
        // Layout: reset_reason(1B) | uptime_ms(4B LE) | free_heap(4B)
        //         | min_free_heap(4B) | last_loop_us(4B)
        uint8_t buf[17];
        buf[0] = (uint8_t)compatResetReason();
        uint32_t up_ms     = millis();
        uint32_t freeHeap  = compatFreeHeap();
        uint32_t minHeap   = compatMinFreeHeap();
        memcpy(&buf[1],  &up_ms,    4);
        memcpy(&buf[5],  &freeHeap, 4);
        memcpy(&buf[9],  &minHeap,  4);
        memcpy(&buf[13], &maxLoopUs, 4);
        sendFrame(CMD_DEBUG_RESP, buf, sizeof(buf), src);
        break;
    }

    case CMD_PING: {
        sendFrame(CMD_PONG, nullptr, 0, src);
        break;
    }

    case CMD_SET_AUTO_CAD: {
        // Enables/disables on-modem auto-CAD: when on, every
        // CMD_TX_REQUEST runs a CAD scan (with backoff retries)
        // before startTransmit. Setting persisted in LittleFS so
        // it survives modem reboot independent of the controller.
        if (len < 1) { sendError(ERR_INVALID_CMD, src); break; }
        bool on = payload[0] != 0;
        autoCadEnabled = on;
#if defined(BOARD_HELTEC_T114)
        NodeState::setAutoCad(on);
#endif
        LOG_R_INFO("auto-CAD %s", on ? "ON" : "OFF");
        uint8_t status = 0;
        sendFrame(CMD_SET_AUTO_CAD_RESP, &status, 1, src);
        break;
    }
    case CMD_SET_DISPLAY_NAME: {
        // Controller pushes the per-sector display name (≤ 16 ASCII
        // bytes). Stored in the OledDisplay instance + LittleFS so
        // the modem keeps it across reboots.
        char buf[24] = {0};
        uint16_t copy = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
        memcpy(buf, payload, copy);
        oled.setDisplayName(buf);
#if defined(BOARD_HELTEC_T114)
        NodeState::setDisplayName(buf);
#endif
        LOG_R_INFO("display name → '%s'", buf);
        uint8_t status = 0;
        sendFrame(CMD_SET_DISPLAY_NAME_RESP, &status, 1, src);
        break;
    }
    case CMD_RADIO_STANDBY: {
        radio.standby();
        radioStandby = true;
        oled.setStandby(true);
#if defined(BOARD_HELTEC_T114)
        NodeState::setStandby(true);
#endif
        LOG_R_INFO("radio STANDBY");
        uint8_t status = 0;
        sendFrame(CMD_RADIO_STANDBY_RESP, &status, 1, src);
        break;
    }
    case CMD_RADIO_RESUME: {
        radioStandby = false;
        oled.setStandby(false);
#if defined(BOARD_HELTEC_T114)
        NodeState::setStandby(false);
#endif
        bool ok = applyConfig(currentConfig) && startReceive();
        LOG_R_INFO("radio RESUME (ok=%d)", (int)ok);
        uint8_t status = ok ? 0 : 1;
        sendFrame(CMD_RADIO_RESUME_RESP, &status, 1, src);
        break;
    }
    case CMD_ENTER_BOOTLOADER: {
        // Triggers Adafruit nRF52 DFU mode without touching the
        // reset button: write the magic value to GPREGRET (bootloader
        // checks it on startup and enters DFU instead of jumping to
        // the app), ack the host, then NVIC_SystemReset().
        // After reboot the operator finalises the flash by plugging
        // USB locally — full UART OTA is the next step (CMD_OTA_*).
        // No-op on ESP32 (no equivalent path; CDC + esptool_py is
        // already trivial there).
        sendFrame(CMD_PONG, nullptr, 0, src);
        LOG_R_INFO("ENTER_BOOTLOADER requested — resetting into DFU");
#ifdef NRF52_SERIES
        // 0x57 = OTA_DFU magic from Adafruit nRF52 BSP
        // (variants/<board>/dfu/usb_desc.h notwithstanding —
        // bootloader matches on the value, not symbolic name).
        NRF_POWER->GPREGRET = 0x57;
        delay(100);
        NVIC_SystemReset();
#else
        sendError(ERR_INVALID_CMD, src);
#endif
        break;
    }

    // ─── OTA (skeleton — flash writer not yet implemented) ───
    // Wire format and orchestration on the controller side are
    // ready; the actual nRF52 dual-bank flash writer + bootloader
    // settings page commit needs a sacrificial-board test pass
    // before going live. Every handler below answers with
    // ERR_OTA_UNSUPPORTED so an over-eager controller doesn't
    // brick a modem trying to push bytes into nowhere.
    case CMD_OTA_BEGIN: {
        LOG_R_WARN("OTA_BEGIN received — flash writer not implemented");
        uint8_t status = 3;   // unsupported
        sendFrame(CMD_OTA_BEGIN_RESP, &status, 1, src);
        break;
    }
    case CMD_OTA_CHUNK: {
        uint8_t status = 1;   // bad_offset (no session active)
        sendFrame(CMD_OTA_CHUNK_RESP, &status, 1, src);
        break;
    }
    case CMD_OTA_VERIFY: {
        uint8_t resp[1 + 32] = {1};   // 1 = no buffer; sha256 zeros
        sendFrame(CMD_OTA_VERIFY_RESP, resp, sizeof(resp), src);
        break;
    }
    case CMD_OTA_APPLY: {
        uint8_t status = 1;
        sendFrame(CMD_OTA_APPLY_RESP, &status, 1, src);
        break;
    }
    case CMD_OTA_ABORT: {
        sendFrame(CMD_PONG, nullptr, 0, src);
        break;
    }

    case CMD_WIFI_RESET: {
        sendFrame(CMD_WIFI_RESET, nullptr, 0, src);
        if (src == TransportSource::USB) Serial.flush();
        delay(200);
        WifiManager::factoryReset();   // does not return
        break;
    }

    default:
        sendError(ERR_INVALID_CMD, src);
        break;
    }
}

// ─── Serial-side parser callbacks ───────────────────────────
static void onSerialFrameOk(uint8_t cmd, const uint8_t* payload, uint16_t len,
                            TransportSource src) {
    lastUsbCmdMs = millis();
    processHostCommand(cmd, payload, len, src);
}

static void onSerialFrameErr(uint8_t err_code, TransportSource src) {
    if (err_code == ERR_CRC_MISMATCH) status.crc_errors++;
    LOG_R_ERR("frame parse error 0x%02X (src=%u)",
              (unsigned)err_code, (unsigned)src);
    sendError(err_code, src);
}

void noteTransportFrameError(uint8_t err_code) {
    if (err_code == ERR_CRC_MISMATCH) status.crc_errors++;
}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
    // PRG held ≥3s at boot → wipe Wi-Fi NVS and reboot. Must come before
    // other init so button sampling is clean.
    WifiManager::checkResetButton();

    // Drive the E22 EN pin LOW immediately so the LDOs and PA bias
    // see a clean, deliberate power-up — no-op on boards with
    // en_pin == -1. Counter starts now; rfSwitchEnHighAfterSettle()
    // below makes sure the full board.en_low_hold_ms has elapsed
    // before SPI traffic begins.
    rfSwitchEnLowAtBoot();
    txLedInitAtBoot();

#if defined(BOARD_HELTEC_T114)
    // Restore non-volatile T114 state BEFORE radio init so we know
    // whether to enter standby on boot. Display name will be picked
    // up by the OLED at the first show*() call.
    NodeState::begin();
    radioStandby   = NodeState::getStandby();
    autoCadEnabled = NodeState::getAutoCad();
#endif

    Serial.begin(921600);
    // On boards with native USB-CDC (Ikoka, LilyGO T3-S3) the first
    // Serial output races against the host opening the CDC endpoint —
    // anything printed before the host attaches is silently dropped.
    // Wait up to 3 s for `Serial` to report itself as connected so the
    // [BOOT] reset_reason banner reliably reaches the operator. Boards
    // with a UART bridge fall through immediately — Serial is always
    // truthy on hardware UART.
    {
        uint32_t t0_serial = millis();
        while (!Serial && (millis() - t0_serial) < 3000) delay(10);
    }
    delay(200);

    // Optional protocol UART — used by sector-array controllers
    // that wire the modem over a hard link instead of USB-CDC.
    // Stays disabled when BOARD.pin_protocol_uart_rx is -1 (every
    // board except T114 in the current fleet); on T114 the pins
    // come from the variant's Serial2 (P0.09 / P0.10).
    if (BOARD.pin_protocol_uart_rx >= 0 && BOARD.pin_protocol_uart_tx >= 0) {
#ifdef ARDUINO_ARCH_ESP32
        PROTO_UART.begin(BOARD.protocol_uart_baud, SERIAL_8N1,
                         BOARD.pin_protocol_uart_rx,
                         BOARD.pin_protocol_uart_tx);
#else
        // nRF52 BSP: pins are baked into Serial2 from variant.h.
        PROTO_UART.begin(BOARD.protocol_uart_baud);
#endif
        uartEnabled = true;
        Serial.printf("[BOOT] protocol UART up @ %lu baud (rx=%d tx=%d)\n",
                      (unsigned long)BOARD.protocol_uart_baud,
                      (int)BOARD.pin_protocol_uart_rx,
                      (int)BOARD.pin_protocol_uart_tx);
    }

    fwVersion = String(FW_VERSION_BASE) + "-" + BOARD.fw_suffix;

    // Print the reason this boot started so we can tell brownouts,
    // panics, watchdog timeouts and clean reboots apart even on boards
    // without UART0 (Ikoka — native USB-CDC only, ROM banner doesn't
    // reach the host). compatResetReason() values:
    //   1 POWERON, 2 EXT, 3 SW, 4 PANIC, 5 INT_WDT, 6 TASK_WDT,
    //   7 WDT, 8 DEEPSLEEP, 9 BROWNOUT, 10 SDIO, 11 USB.
    {
        const char* labels[] = {
            "?", "POWERON", "EXT", "SW", "PANIC",
            "INT_WDT", "TASK_WDT", "WDT", "DEEPSLEEP",
            "BROWNOUT", "SDIO", "USB"
        };
        int rr = compatResetReason();
        const char* lbl = (rr >= 0 && (size_t)rr < sizeof(labels)/sizeof(labels[0]))
                              ? labels[rr] : "OTHER";
        Serial.printf("[BOOT] reset_reason=%d (%s)\n", rr, lbl);
    }

    // Boot splash: show pyMC logo for at least SPLASH_HOLD_MS while the
    // rest of setup() (Wi-Fi connect, Ethernet bring-up, radio init)
    // proceeds in the background. We just record when it went up;
    // the wait-until-elapsed happens at the end of setup().
    oled.begin();
#if defined(BOARD_HELTEC_T114)
    // Push restored state onto the OLED before showSplash so the
    // boot screen already has the right name + standby tag.
    oled.setDisplayName(NodeState::getDisplayName());
    oled.setStandby(radioStandby);
#endif
    oled.showSplash();
    splashStartedMs = millis();

    // Wait out the remaining EN-LOW hold (5 s on Ikoka; 0 on Heltec)
    // and raise EN HIGH for the rest of the device's lifetime. After
    // this point the RF switch is enabled; SX1262's DIO2 (or our
    // rx_pin / tx_pin GPIOs) will drive the actual TX/RX selection.
    rfSwitchEnHighAfterSettle();

    // Some boards also have PA/LNA front-end mode pins that must be
    // asserted to fixed levels before the SX1262 is initialized.
    configureStaticGpios();

    // ─── SX1262 init (skipped when board has no LoRa hardware) ──
    // ESP32-P4-NANO ships without a LoRa front end on day one — the
    // module is added later. Until then BOARD.has_lora_radio == false
    // and the firmware runs as a plain pymc_repeater bridge over
    // Wi-Fi / Ethernet, returning ERR_NO_RADIO for radio commands so
    // the host can still probe the modem.
    if (BOARD.has_lora_radio) {
        // Bring up the SPI bus for the SX1262 BEFORE radio.begin(). When
        // BOARD.pin_lora_sck/miso/mosi are -1 the board variant's default
        // SPI pins already match the LoRa wiring (Heltec V3, XIAO ESP32-S3
        // for Ikoka) and we leave the bus alone. When they're set the
        // board has remapped SPI (LilyGO T3-S3 etc.) and we must call
        // SPI.begin() with the explicit pins or RadioLib's first SPI
        // transfer fails.
        if (BOARD.pin_lora_sck >= 0 || BOARD.pin_lora_miso >= 0 || BOARD.pin_lora_mosi >= 0) {
#ifdef ARDUINO_ARCH_ESP32
#if defined(BOARD_PHOTON_1W_XIAO_ESP32C6)
            // Seeed XIAO ESP32-C6 Photon variant matches MeshCore's
            // working C6 port, which uses SPIClass(0) for the LoRa bus.
            loraSpi.begin(BOARD.pin_lora_sck, BOARD.pin_lora_miso,
                          BOARD.pin_lora_mosi);
#else
            // ESP32-S3/P4 GPIO matrix: rebind SPI to specific pins
            SPI.begin(BOARD.pin_lora_sck, BOARD.pin_lora_miso,
                      BOARD.pin_lora_mosi, BOARD.pin_lora_nss);
#endif
#else
            // nRF52 BSP: SPI peripheral has fixed pins on its
            // selected instance. The Heltec T114 variant.h ships
            // with the SX1262's pins already mapped to SPIM2.
            SPI.begin();
#endif
        }

        LOG_R_INFO("radio.begin nss=%d dio1=%d rst=%d busy=%d spi=(%d,%d,%d)",
                   (int)BOARD.pin_lora_nss, (int)BOARD.pin_lora_dio1,
                   (int)BOARD.pin_lora_rst, (int)BOARD.pin_lora_busy,
                   (int)BOARD.pin_lora_sck, (int)BOARD.pin_lora_miso,
                   (int)BOARD.pin_lora_mosi);
        int state = radio.begin();
        LOG_R_INFO("radio.begin -> %d", state);
        if (state != RADIOLIB_ERR_NONE) {
            oled.showError("SX1262 init fail!");
            while (Serial.availableForWrite() == 0) delay(10);
            sendError(ERR_RADIO_INIT, TransportSource::USB);
            Serial.println("[BOOT] SX1262 init failed — continuing with Wi-Fi/config portal only");
            radioReady = false;
        } else {
        if (BOARD.use_dio3_tcxo) {
            state = radio.setTCXO(BOARD.tcxo_voltage);
            LOG_R_INFO("setTCXO(%.1f V) -> %d", BOARD.tcxo_voltage, state);
        }
        rfSwitchConfigureRadio();
        configureBoardRadioOptions();

        if (!applyConfig(currentConfig)) {
            oled.showError("Config fail!");
            sendError(ERR_INVALID_CONFIG, TransportSource::USB);
            while (true) delay(1000);
        }

        radio.setDio1Action(onDio1Rise);
        LOG_R_INFO("DIO1 IRQ attached on GPIO%d", (int)BOARD.pin_lora_dio1);

        if (!startReceive()) {
            oled.showError("RX start fail!");
            while (true) delay(1000);
        }

        radioReady = true;
        }
    } else {
        Serial.println("[BOOT] no LoRa radio on this board — running as Wi-Fi/Ethernet bridge only");
        radioReady = false;
    }

    // ─── Network bring-up: Ethernet preferred, Wi-Fi fallback ──
    // On boards with both interfaces present (ESP32-P4-Nano), bringing
    // up Wi-Fi *and* Ethernet *and* the radio at the same time crashes
    // the C6 SDIO bridge from cumulative noise (see lesson_p4_nano_*).
    // Strategy: try Ethernet first; if a cable is plugged we keep ETH
    // and skip Wi-Fi; if no link we tear EMAC back down (so the RMII
    // GPIOs are released) and fall back to Wi-Fi. Boards without
    // Ethernet (`ethernet.enabled = false`) skip straight to Wi-Fi.
    WifiManager::loadConfigOnly();
    const auto& netCfg = WifiManager::getConfig();
    bool useEthernet = false;
    if (BOARD.ethernet.enabled) {
        EthernetManager::begin(WifiManager::getHostname(),
                               netCfg.useStaticIP,
                               netCfg.staticIP,
                               netCfg.gateway,
                               netCfg.subnet,
                               netCfg.dns1,
                               netCfg.dns2);   // waits up to 5 s for link + DHCP
        if (EthernetManager::isLinkUp()) {
            useEthernet = true;
            Serial.println("[NET] Ethernet link up — Wi-Fi will be skipped");
        } else {
            Serial.println("[NET] no Ethernet link — falling back to Wi-Fi");
            EthernetManager::end();   // free RMII pins so Wi-Fi can run cleanly
        }
    }

    if (!useEthernet && BOARD.has_wifi) {
        WifiManager::begin();
    } else {
        // Either Ethernet won, or Wi-Fi is compile-time disabled. The
        // saved config was loaded above for hostname/TCP setup.
    }
    deviceHostname = WifiManager::getHostname();

    bool netUp = WifiManager::isSTAConnected() || EthernetManager::hasIP();
    if (netUp) {
        const auto& wcfg = WifiManager::getConfig();
        // Diagnostic mode (has_wifi == false): ignore the saved token
        // so we can probe the TCP server without re-authenticating.
        // Restore normal auth once Wi-Fi comes back.
        String token = BOARD.has_wifi ? wcfg.tcpToken : String();
        uint16_t port = wcfg.tcpPort ? wcfg.tcpPort : 5055;
        TCPServer::begin(port, token);
        tcpStarted = true;

        OTAManager::begin(deviceHostname, token);
        otaStarted = true;
    }

    // Hold the splash for the rest of SPLASH_HOLD_MS if init finished
    // earlier than that. Wi-Fi STA connect already burns several seconds
    // so on most boards this loop is a no-op, but on the P4-Nano (no
    // radio init, no Wi-Fi delay when offline) we still want the logo
    // up for a clean visible second.
    while (millis() - splashStartedMs < SPLASH_HOLD_MS) {
        delay(50);
    }

    oled.showStatus(0, 0,
                    BOARD.has_wifi ? WifiManager::getSSID() : "---",
                    EthernetManager::hasIP() ? EthernetManager::getIPString()
                                             : (BOARD.has_wifi ? WifiManager::getIPString() : "---"),
                    "BOOT", fwVersion.c_str());
    currentScreen = Screen::STATUS;
    oledWakeUntil = millis() + OLED_WAKE_DURATION_MS;
    lastAutoCycleMs = millis();   // first auto-cycle fires SCREEN_AUTO_CYCLE_MS after splash

#ifdef ARDUINO_ARCH_ESP32
    GPSManager::begin(WifiManager::getConfig().gpsEnabled);
#endif

    // Arm the task watchdog LAST — everything above may legitimately take
    // many seconds (WiFi STA connect up to 30 s). From now on, any loop()
    // iteration that doesn't complete within LOOP_WDT_TIMEOUT_S triggers a
    // panic reboot. If the bootloader is OTA-aware, the rolled-back slot
    // would take over; on stock Arduino bootloader, the same image reboots.
    // ESP-IDF 5.x autostarts the task WDT on the IDLE tasks; we just
    // adjust the timeout + enable panic so a stuck loop() reboots.
    // nRF52 build — task WDT not armed in iter 1 (compatWdtReset()
    // is a no-op). The nRF52 watchdog peripheral can be added later
    // via NRF_WDT_NS direct register writes.
#ifdef ARDUINO_ARCH_ESP32
    {
        esp_task_wdt_config_t wdt_cfg = {
            .timeout_ms     = LOOP_WDT_TIMEOUT_S * 1000U,
            .idle_core_mask = 0,
            .trigger_panic  = true,
        };
        esp_task_wdt_reconfigure(&wdt_cfg);
    }
    esp_task_wdt_add(NULL);
#endif

    Serial.printf("[BOOT] firmware %s on %s ready (loop WDT %us)\n",
                  fwVersion.c_str(), BOARD.name, (unsigned)LOOP_WDT_TIMEOUT_S);
}

// ─── Noise floor sampling ────────────────────────────────────
void sampleNoiseFloor() {
    if (!radioReady || isTxActive) return;
    if (millis() - lastPacketTime < 500) return;
    if (dio1Flag) return;  // don't read RSSI while an RX packet is incoming
    if (millis() - lastNoiseSample < 10) return;
    lastNoiseSample = millis();

    if (noiseFloorCount < NUM_NOISE_FLOOR_SAMPLES) {
        float instRssi = radio.getRSSI();
        if (instRssi < (noiseFloor + NOISE_SAMPLING_THRESHOLD)) {
            noiseFloorCount++;
            noiseFloorSum += instRssi;
        }
    } else if (noiseFloorCount >= NUM_NOISE_FLOOR_SAMPLES && noiseFloorSum != 0.0f) {
        float newFloor = noiseFloorSum / NUM_NOISE_FLOOR_SAMPLES;
        if (newFloor < -150.0f) newFloor = -150.0f;
        if (newFloor > -50.0f)  newFloor = -50.0f;
        noiseFloor = newFloor;
        noiseFloorSum = 0.0f;
        noiseFloorCount = 0;
    }
}

// ─── Main loop ───────────────────────────────────────────────
void loop() {
    // Track per-iteration time for watchdog-bait detection. Stored as
    // rolling max in microseconds; queryable via CMD_GET_DEBUG.
    static uint32_t loopStartUs = 0;
    if (loopStartUs != 0) {
        uint32_t dt = (uint32_t)micros() - loopStartUs;
        if (dt > maxLoopUs) maxLoopUs = dt;
    }
    loopStartUs = (uint32_t)micros();

    compatWdtReset();   // feed the loop watchdog every pass

    // DIO1 during TX is consumed by the TX handler's own wait loop; in
    // loop() we only act on it when the radio is in RX mode.
    if (dio1Flag && !isTxActive) {
        dio1Flag = false;
        handleLoRaRx();
    }

    while (Serial.available()) {
        uint8_t b = (uint8_t)Serial.read();
        frameparser_feed(serialParser, b, TransportSource::USB,
                         onSerialFrameOk, onSerialFrameErr);
    }

    if (uartEnabled) {
        while (PROTO_UART.available()) {
            uint8_t b = (uint8_t)PROTO_UART.read();
            frameparser_feed(uartParser, b, TransportSource::UART,
                             onSerialFrameOk, onSerialFrameErr);
        }
    }

    if (tcpStarted) TCPServer::loop();
    if (otaStarted) OTAManager::loop();
#ifdef ARDUINO_ARCH_ESP32
    GPSManager::loop();
#endif

    sampleNoiseFloor();
    if (BOARD.has_wifi) WifiManager::loop();
    EthernetManager::loop();

    // Lazy TCP + OTA start if STA or Ethernet came up after boot.
    bool netUp = WifiManager::isSTAConnected() || EthernetManager::hasIP();
    if (!tcpStarted && netUp) {
        const auto& wcfg = WifiManager::getConfig();
        String token = BOARD.has_wifi ? wcfg.tcpToken : String();
        uint16_t port = wcfg.tcpPort ? wcfg.tcpPort : 5055;
        TCPServer::begin(port, token);
        tcpStarted = true;
    }
    if (!otaStarted && netUp) {
        const auto& wcfg = WifiManager::getConfig();
        String token = BOARD.has_wifi ? wcfg.tcpToken : String();
        OTAManager::begin(deviceHostname, token);
        otaStarted = true;
    }

    // PRG/BOOT button:
    //   short tap       → cycle SLEEP → STATUS → RADIO → DIAGNOSTICS
    //   runtime 5s hold → wipe saved Wi-Fi/config and reboot into AP setup
    //   boot 3s hold    → same wipe/reboot, handled in setup()/checkResetButton()
    // Boards with pin_user_button < 0 (e.g. ESP32-P4-Nano where the BOOT
    // button shares a pin with RMII Ethernet TXD1) skip polling entirely.
    bool btn = false;
    if (BOARD.pin_user_button >= 0) {
        btn = (digitalRead(BOARD.pin_user_button) == (BOARD.user_button_active_low ? LOW : HIGH));
    }

    if (btn && !prgWasPressed) {
        prgPressedSince = millis();
        prgLongHandled = false;
        prgWasPressed = true;
    }

    if (btn && !prgLongHandled &&
        millis() - prgPressedSince >= PRG_WIFI_RESET_HOLD_MS) {
        prgLongHandled = true;
        Serial.println("[WiFi] PRG long hold -> reset Wi-Fi config and reboot to AP setup");
        if (currentScreen == Screen::SLEEP) oled.turnOn();
        oled.showError("Wi-Fi reset");
        delay(250);
        WifiManager::factoryReset();   // does not return
    }

    if (!btn && prgWasPressed) {
        bool shortTap = !prgLongHandled &&
                        millis() - prgPressedSince >= PRG_DEBOUNCE_MS &&
                        millis() > prgIgnoreUntil;
        prgWasPressed = false;
        prgIgnoreUntil = millis() + PRG_DEBOUNCE_MS;

        if (shortTap) {
            oledWakeUntil  = millis() + OLED_WAKE_DURATION_MS;
            switch (currentScreen) {
                case Screen::SLEEP:
                    oled.turnOn();
                    currentScreen = Screen::STATUS;
                    break;
                case Screen::STATUS:
                    currentScreen = Screen::RADIO;
                    break;
                case Screen::RADIO:
                    currentScreen = Screen::DIAGNOSTICS;
                    break;
                case Screen::DIAGNOSTICS:
                    currentScreen = Screen::STATUS;
                    break;
            }
            lastOledUpdate = 0;  // force immediate refresh on next render pass
        }
    }

    // Auto-cycle for boards without a working user button. Keeps the
    // panel awake (continually pushes oledWakeUntil forward) and steps
    // through STATUS → RADIO → DIAGNOSTICS every SCREEN_AUTO_CYCLE_MS.
    if (BOARD.pin_user_button < 0) {
        oledWakeUntil = millis() + OLED_WAKE_DURATION_MS;
        if (currentScreen != Screen::SLEEP &&
            millis() - lastAutoCycleMs >= SCREEN_AUTO_CYCLE_MS) {
            lastAutoCycleMs = millis();
            switch (currentScreen) {
                case Screen::STATUS:      currentScreen = Screen::RADIO; break;
                case Screen::RADIO:       currentScreen = Screen::DIAGNOSTICS; break;
                case Screen::DIAGNOSTICS: currentScreen = Screen::STATUS; break;
                default:                  currentScreen = Screen::STATUS; break;
            }
            lastOledUpdate = 0;  // force immediate redraw on next render pass
        }
    }

    if (currentScreen != Screen::SLEEP) {
        if ((int32_t)(millis() - oledWakeUntil) >= 0) {
            oled.turnOff();
            currentScreen = Screen::SLEEP;
        } else if (millis() - lastOledUpdate > 2000) {
            lastOledUpdate = millis();
            if (currentScreen == Screen::STATUS) {
                // Prefer the interface that actually has an IP. When
                // Wi-Fi is disabled at compile time (P4-Nano diag mode)
                // the IP comes from Ethernet — show ETH status + IP +
                // link-up tag so the panel reflects reality.
                const char* stateTag;
                const char* ssid;
                const char* ip;
                if (BOARD.has_wifi && WifiManager::isAPActive()) {
                    stateTag = "AP";
                    ssid     = WifiManager::getSSID();
                    ip       = WifiManager::getIPString();
                } else if (BOARD.has_wifi && WifiManager::isSTAConnected()) {
                    stateTag = "WiFi";
                    ssid     = WifiManager::getSSID();
                    ip       = WifiManager::getIPString();
                } else if (EthernetManager::hasIP()) {
                    stateTag = "ETH";
                    ssid     = "ethernet";
                    ip       = EthernetManager::getIPString();
                } else if (EthernetManager::isLinkUp()) {
                    stateTag = "ETHL";   // link up but no IP yet (DHCP pending / static fail)
                    ssid     = "ethernet";
                    ip       = "no-ip";
                } else {
                    stateTag = "...";
                    ssid     = BOARD.has_wifi ? WifiManager::getSSID()    : "---";
                    ip       = BOARD.has_wifi ? WifiManager::getIPString(): "---";
                }
                uint16_t batteryMv = 0xFFFF;
#ifdef ARDUINO_ARCH_ESP32
                batteryMv = readBatteryMilliVolts();
                status.battery_mv = batteryMv;
#endif
                oled.showStatus(status.rx_count, status.tx_count,
                                ssid, ip, stateTag, fwVersion.c_str(), batteryMv);
            } else if (currentScreen == Screen::RADIO) {
                oled.showRadioConfig(currentConfig.freq_hz,
                                     currentConfig.bandwidth_hz,
                                     currentConfig.sf,
                                     currentConfig.cr,
                                     currentConfig.power_dbm,
                                     currentConfig.syncword,
                                     currentConfig.preamble_len,
                                     fwVersion.c_str());
            } else {  // Screen::DIAGNOSTICS
                uint32_t uptime = millis() / 1000;
                String ip = TCPServer::getClientIP();
                uint32_t usb_idle = (lastUsbCmdMs == 0)
                    ? UINT32_MAX
                    : (millis() - lastUsbCmdMs) / 1000;
                oled.showDiagnostics(uptime, ip.c_str(), usb_idle,
                                     status.rx_count, status.tx_count,
                                     status.crc_errors, fwVersion.c_str());
            }
        }
    }
}
