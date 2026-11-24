// =============================================================
// main.cpp — openHop Modem firmware
// Serial + Wi-Fi/TCP + Ethernet/TCP bridge to SX1262 for openHop Core on RPi.
//
// Supported boards: see README.md and firmware/include/boards/.
// Selected at compile time via -DBOARD_<name> in platformio.ini.
//
// All MeshCore protocol logic runs on the host in openHop Core.
// =============================================================

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <stdarg.h>
#include "legacy_rak4631_build_flags.h"
#include "protocol.h"
#include "board_config.h"
#include "rak3401_ready_led.h"
#include "bootloader_manager.h"
#include "frame_parser.h"
#include "compat.h"
#include "rf_frontend.h"
#include "radio_runtime.h"
#include "radio_settings.h"
#include <new>
#include "rf_request_queue.h"
#include "rx_boost_control.h"
#include "agc_maintenance.h"
#include "station_g3_power.h"
#include "environment_sensor.h"
#include "runtime_stats.h"
#include "battery_monitor.h"
#include "pa_ramp.h"
#include "tbeam_1w_fan.h"
#include "gps_manager.h"
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
#  include "dhcp_routes.h"
#  include "runtime_stats.h"
#  include "gps_manager.h"
#  include "pmu_manager.h"
#  if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT && SOC_USB_SERIAL_JTAG_SUPPORTED
#    include "hal/usb_serial_jtag_ll.h"
#    include "usb_tx_stall_watch.h"
#    define OPENHOP_HWCDC_TX_KICK 1
#  endif
#else
// nRF52 builds exclude the ESP32 Wi-Fi/OTA/display managers via
// platformio.ini's build_src_filter. Most nRF52 targets are
// USB/UART-only; RAK4631 enables a separate W5100S Ethernet TCP
// transport under OPENHOP_ETHERNET_W5100S. Provide drop-in stub
// namespaces for the rest so call sites compile unchanged.
#include <IPAddress.h>
#if defined(OPENHOP_ETHERNET_W5100S)
#  ifndef OPENHOP_ETH_TCP_PORT
#    define OPENHOP_ETH_TCP_PORT 5055
#  endif
#  ifndef OPENHOP_ETH_TOKEN
#    define OPENHOP_ETH_TOKEN ""
#  endif
#  ifndef OPENHOP_ETH_HOSTNAME
#    define OPENHOP_ETH_HOSTNAME "openhop-rak4631-eth"
#  endif
#endif
#if defined(OPENHOP_ETHERNET_W5100S)
#  include "rak4631_config.h"
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
        String   httpPassword;
    };

    inline IPAddress toIPAddress(const Rak4631Config::IPv4Address& address) {
        return IPAddress(address.octets[0], address.octets[1],
                         address.octets[2], address.octets[3]);
    }

    inline Rak4631Config::IPv4Address fromIPAddress(const IPAddress& address) {
        return Rak4631Config::IPv4Address{{address[0], address[1], address[2], address[3]}};
    }

    inline Config& activeConfig() {
        static Config config;
        return config;
    }

    inline void loadConfigOnly() {
        Rak4631Config::begin();
        const auto& stored = Rak4631Config::getConfig();
        Config& config = activeConfig();
        config.hostname = stored.hostname;
        config.useStaticIP = stored.useStaticIP;
        config.staticIP = toIPAddress(stored.staticIP);
        config.gateway = toIPAddress(stored.gateway);
        config.subnet = toIPAddress(stored.subnet);
        config.dns1 = toIPAddress(stored.dns1);
        config.dns2 = toIPAddress(stored.dns2);
        config.tcpToken = stored.tcpToken;
        config.tcpPort = stored.tcpPort;
        config.gpsEnabled = stored.gpsEnabled;
        config.httpPassword = stored.httpPassword;
    }

    inline void  checkResetButton()  {}
    inline void  begin()             { loadConfigOnly(); }
    inline void  loop()              {}
    inline bool  isSTAConnected()    { return false; }
    inline bool  isAPActive()        { return false; }
    inline bool  hasWifiAntennaSwitch() { return false; }
    inline void  applyWifiAntennaSwitch() {}
    inline const char* getSSID()     { return "---"; }
    inline const char* getIPString() { return "---"; }
    inline const char* getHostname() { return Rak4631Config::getEffectiveHostname(); }
    inline Mode  getMode()           { return Mode::OFFLINE; }
    inline const Config& getConfig() { return activeConfig(); }

    inline bool saveConfig(const Config& config) {
        if (config.hostname.length() > Rak4631Config::MAX_TEXT_LENGTH ||
            config.tcpToken.length() > Rak4631Config::MAX_TEXT_LENGTH ||
            config.httpPassword.length() > Rak4631Config::MAX_TEXT_LENGTH) {
            return false;
        }
        Rak4631Config::Config stored = Rak4631Config::getConfig();
        snprintf(stored.hostname, sizeof(stored.hostname), "%s", config.hostname.c_str());
        stored.useStaticIP = config.useStaticIP;
        stored.staticIP = fromIPAddress(config.staticIP);
        stored.gateway = fromIPAddress(config.gateway);
        stored.subnet = fromIPAddress(config.subnet);
        stored.dns1 = fromIPAddress(config.dns1);
        stored.dns2 = fromIPAddress(config.dns2);
        stored.tcpPort = config.tcpPort;
        snprintf(stored.tcpToken, sizeof(stored.tcpToken), "%s", config.tcpToken.c_str());
        snprintf(stored.httpPassword, sizeof(stored.httpPassword), "%s",
                 config.httpPassword.c_str());
        stored.gpsEnabled = config.gpsEnabled;
        return Rak4631Config::saveConfig(stored);
    }
    inline void factoryReset() {
        if (Rak4631Config::factoryReset()) {
            delay(200);
            NVIC_SystemReset();
        }
    }
}
#else
// Keep USB-only nRF52 targets on their existing no-op network stub.
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
#if defined(OPENHOP_ETHERNET_W5100S)
    inline const char* getHostname() { return OPENHOP_ETH_HOSTNAME; }
#else
    inline const char* getHostname() { return "---"; }
#endif
    inline Mode  getMode()           { return Mode::OFFLINE; }
    inline const Config& getConfig() {
        static Config c = []() {
            Config cfg;
#if defined(OPENHOP_ETHERNET_W5100S)
            cfg.hostname = OPENHOP_ETH_HOSTNAME;
            cfg.tcpToken = OPENHOP_ETH_TOKEN;
            cfg.tcpPort = OPENHOP_ETH_TCP_PORT;
#endif
            return cfg;
        }();
        return c;
    }
    inline void  saveConfig(const Config&) {}
    inline void  factoryReset()      {}
}
#endif
#if defined(OPENHOP_ETHERNET_W5100S)
#  include "w5100s_ethernet_transport.h"
#  include "w5100s_http_server.h"
#else
namespace TCPServer {
    inline void begin(uint16_t, const String&) {}
    inline void loop() {}
    inline void end()  {}
    inline bool isClientReady() { return false; }
    inline uint32_t connectionGeneration() { return 0; }
    inline bool isConnectionCurrent(uint32_t) { return false; }
    inline bool canQueueResponse() { return false; }
    inline void write(const uint8_t*, size_t) {}
    inline String getClientIP() { return String(); }
}
#endif
namespace OTAManager {
    inline void begin(const String&, const String&) {}
    inline void loop() {}
    inline void notifyValidFrame() {}
}
#if !defined(OPENHOP_ETHERNET_W5100S)
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
#endif
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
// distinguishes one binary from another (e.g. "v1.4.5-ikoka").
#define FW_VERSION_BASE "v1.4.5"
static String fwVersion;   // populated in setup()

// ─── Task watchdog — self-heal on loop() hang ───────────────
// A 30 s deadline is comfortably longer than any legitimate loop() burst
// (OTA HTTP upload chunks, CAD scans, OLED redraw) but short enough to
// reboot automatically if ArduinoOTA / WebServer / WifiManager deadlocks.
static constexpr uint32_t LOOP_WDT_TIMEOUT_S = 30;

// Stable, count-driven radio owners. Hardware remains lazy until begin().
OledDisplay oled;
static RadioRuntime* radioOwners[RadioLayout::CAPACITY]{};
static size_t radioOwnerCount = 0;
static RadioRuntime* radioAt(size_t index) {
    return index < radioOwnerCount ? radioOwners[index] : nullptr;
}

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
// The splash covers board initialization; there is no post-init blocking hold.
// Boards without a usable PRG/BOOT button (pin_user_button < 0) cycle
// STATUS→RADIO→DIAGNOSTICS→STATUS automatically every SCREEN_AUTO_CYCLE_MS.
// Boards with a working button keep the manual short-tap cycle and ignore this.
static constexpr uint32_t SCREEN_AUTO_CYCLE_MS  = 4000;
static uint32_t oledWakeUntil    = 0;
static uint32_t prgIgnoreUntil   = 0;
static uint32_t lastAutoCycleMs  = 0;

// Worst-case loop() iteration time observed since boot. Reported via
// CMD_GET_DEBUG so we can spot watchdog-bait blocking calls without
// a serial cable. Reset on overflow doesn't matter — value is rolling
// max in microseconds.
static uint32_t maxLoopUs = 0;

// Host-link health for the DIAGNOSTICS screen: millis() of the last USB
// frame that parsed cleanly. 0 = no frame yet since boot.
static uint32_t lastUsbCmdMs = 0;

static void fillDeviceStatus(StatusResp& status);

namespace RuntimeStats {
Snapshot capture() {
    Snapshot snap = {};
    const RadioRuntime* primary = radioAt(0);
    snap.status = primary ? primary->status() : StatusResp{};
    snap.status.uptime_sec = millis() / 1000;
    // StatusResp reserves state 2 for errors; standby remains a healthy idle
    // state and is exposed separately in this richer runtime snapshot.
    if (!primary) { snap.status.radio_state = 2; fillDeviceStatus(snap.status); }
    snap.hasBoardTemperature = BOARD.thermistor.ntc_pin >= 0;
    snap.boardTemperatureC = TBeam1WFan::temperatureC();
    snap.hasBoardFan = BOARD.thermistor.fan_pin >= 0;
    snap.boardFanEnabled = TBeam1WFan::isEnabled();
    snap.radio = primary ? primary->config() : RadioConfig{};
    snap.firmwareVersion = fwVersion;
    snap.radioStandby = primary && primary->standby();
    snap.autoCadEnabled = primary && primary->autoCad();
    snap.hasBatteryChargeRatePctPerHour = BOARD.battery.fuel_gauge_i2c_addr != 0 &&
        BOARD.battery.fuel_gauge_crate_reg != 0;
    if (snap.hasBatteryChargeRatePctPerHour) {
        snap.batteryChargeRatePctPerHourValid =
            BatteryMonitor::readChargeRatePctPerHour(
                BOARD.battery, snap.batteryChargeRatePctPerHour);
    }
    StationG3Power::Snapshot power = StationG3Power::snapshot();
    snap.stationG3PowerMonitorAvailable = power.available;
    snap.stationG3PowerValid = power.valid;
    snap.stationG3InputVoltageV = power.inputVoltageV;
    snap.stationG3CurrentMa = power.currentMa;
    snap.stationG3PowerW = power.powerW;
    snap.stationG3MinimumInputVoltageV = power.minimumInputVoltageV;
    snap.stationG3MaximumCurrentMa = power.maximumCurrentMa;
#if defined(BOARD_STATION_G2) || defined(BOARD_STATION_G3)
    snap.agcResetCount = primary ? primary->agcResetCount() : 0;
    snap.lastAgcResetMsAgo = primary && primary->agcResetCount() > 0
        ? (uint32_t)(millis() - primary->lastAgcResetMs()) : 0;
    snap.environment = EnvironmentSensor::snapshot();
#endif
    return snap;
}
size_t radioCount() { return radioOwnerCount; }
bool captureRadio(size_t index, RadioSnapshot& out) {
    const RadioRuntime* owner = radioAt(index);
    if (!owner) return false;
    out = RadioSnapshot{};
    out.id = (uint8_t)index;
    out.label = RadioLayout::at(index).label;
    out.tcpPort = RadioLayout::port(index, WifiManager::getConfig().tcpPort);
    out.present = owner->present(); out.ready = owner->ready();
    out.enabled = owner->enabled();
    out.txReady = owner->txReady(); out.standby = owner->standby();
    out.busy = owner->busy(); out.autoCadEnabled = owner->autoCad();
    out.radio = owner->config(); out.status = owner->status();
    return true;
}
}

#if defined(BOARD_RAK3401)
static void writeOutputPin(int8_t pin, bool high) {
    if (pin < 0) return;
    pinMode(pin, OUTPUT); digitalWrite(pin, high ? HIGH : LOW);
}
#endif
#if defined(BOARD_RAK3401)
static Rak3401ReadyLed rak3401ReadyLed;
#endif

static void rak3401ReadyLedOffAtBoot() {
#if defined(BOARD_RAK3401)
    writeOutputPin(PIN_LED1, false);
#endif
}

static void startRak3401ReadyLedHeartbeat() {
#if defined(BOARD_RAK3401)
    // Briefly light the RAK19007 green user LED after radio init, then leave
    // it dark apart from the same 50 ms heartbeat once per minute.
    rak3401ReadyLed.begin(millis());
    writeOutputPin(PIN_LED1, rak3401ReadyLed.isLit());
#endif
}

static void updateRak3401ReadyLedHeartbeat() {
#if defined(BOARD_RAK3401)
    if (rak3401ReadyLed.update(millis())) {
        writeOutputPin(PIN_LED1, rak3401ReadyLed.isLit());
    }
#endif
}
// ─── Frame output: explicit, generation-bound reply vs radio event ───
static ResponseRoute primaryRoute(TransportSource source) {
    return {source, 0, source == TransportSource::TCP ? TCPServer::connectionGeneration() : 0};
}
static bool routeCurrent(const ResponseRoute& route) {
    if (route.radio >= RadioLayout::count()) return false;
    if (route.transport != TransportSource::TCP)
        return route.radio == 0 && (route.transport == TransportSource::USB || route.transport == TransportSource::UART);
#ifdef ARDUINO_ARCH_ESP32
    return TCPServer::isRouteCurrent(route);
#else
    return route.radio == 0 && TCPServer::isConnectionCurrent(route.generation);
#endif
}
static bool routeCanReply(const ResponseRoute& route) {
    if (!routeCurrent(route)) return false;
    if (route.transport != TransportSource::TCP) return true;
#ifdef ARDUINO_ARCH_ESP32
    return TCPServer::canQueueResponse(route);
#else
    return TCPServer::canQueueResponse();
#endif
}
static uint16_t encodeFrame(uint8_t cmd, const uint8_t* payload, uint16_t len, uint8_t* out) {
    if (len > MAX_LORA_PAYLOAD + 6 || (len && !payload)) return 0;
    out[0] = PROTO_SYNC; out[1] = cmd; out[2] = len; out[3] = len >> 8;
    if (len) memcpy(out + 4, payload, len);
    const uint16_t crc = crc16_ccitt(out + 1, 3 + len);
    out[4 + len] = crc; out[5 + len] = crc >> 8;
    return len + 6;
}
void sendFrame(uint8_t cmd, const uint8_t* payload, uint16_t len, const ResponseRoute& route) {
    if (!routeCurrent(route)) return;
    uint8_t frame[MAX_FRAME_SIZE];
    const uint16_t count = encodeFrame(cmd, payload, len, frame);
    if (!count) return;
    if (route.transport == TransportSource::USB) Serial.write(frame, count);
    else if (route.transport == TransportSource::UART) { if (uartEnabled) PROTO_UART.write(frame, count); }
    else {
#ifdef ARDUINO_ARCH_ESP32
        TCPServer::writeTo(route, frame, count);
#else
        TCPServer::write(frame, count); // validated single-radio W5100S adapter
#endif
    }
}
void sendFrame(uint8_t cmd, const uint8_t* payload, uint16_t len, TransportSource source) {
    sendFrame(cmd, payload, len, primaryRoute(source));
}
void sendError(uint8_t code, const ResponseRoute& route) { sendFrame(CMD_ERROR, &code, 1, route); }
void sendError(uint8_t code, TransportSource source) { sendError(code, primaryRoute(source)); }
static void publishRadioFrame(uint8_t radio, uint8_t cmd, const uint8_t* payload, uint16_t len) {
    if (radio >= radioOwnerCount) return;
    uint8_t frame[MAX_FRAME_SIZE];
    const uint16_t count = encodeFrame(cmd, payload, len, frame);
    if (!count) return;
    // USB/UART have no radio selector on the wire and are permanently primary.
    if (radio == 0) {
        Serial.write(frame,count);
        if (uartEnabled) PROTO_UART.write(frame, count);
    }
#ifdef ARDUINO_ARCH_ESP32
    TCPServer::writeRadioEvent(radio, frame, count);
#else
    if (radio == 0 && TCPServer::isClientReady() && TCPServer::canQueueResponse()) TCPServer::write(frame, count);
#endif
}
void broadcastFrame(uint8_t cmd, const uint8_t* payload, uint16_t len) { publishRadioFrame(0, cmd, payload, len); }
// HWCDC (arduino-esp32 3.3.x) can leave written bytes in its TX ring buffer
// with nothing telling the USB Serial/JTAG FIFO to send them, so a reply sits
// on the modem until the next Serial write. Re-arming the IN_EMPTY interrupt
// alone does not release it; a FIFO flush does, as HWCDC::write() and
// HWCDC::flush() do. Kick the FIFO when bytes have been waiting too long.
#if defined(OPENHOP_HWCDC_TX_KICK)
static UsbTxStallWatch usbTxStallWatch(20);
#endif

static void kickStalledUsbSerialTx() {
#if defined(OPENHOP_HWCDC_TX_KICK)
    if (!usbTxStallWatch.update(Serial.availableForWrite(), millis())) return;
    usb_serial_jtag_ll_txfifo_flush();
    usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
#endif
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
    Serial.printf("[%s] %s\n",tag,text);

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
static uint16_t buildWifiStatusPayload(uint8_t* out, uint8_t radio = 0) {
    uint16_t i = 0;

    const bool ethHasIP = EthernetManager::hasIP();

    uint8_t mode;
    switch (WifiManager::getMode()) {
        case WifiManager::Mode::STA_CONNECTED:  mode = 2; break;
        case WifiManager::Mode::STA_CONNECTING: mode = 1; break;
        case WifiManager::Mode::AP_CONFIG:      mode = 3; break;
        default:                                mode = ethHasIP ? 2 : 0; break;
    }
    out[i++] = mode;

    IPAddress ip = WifiManager::isSTAConnected() ? WiFi.localIP()
                 : WifiManager::isAPActive()     ? WiFi.softAPIP()
                                                 : IPAddress((uint32_t)0);
    if (ip == IPAddress((uint32_t)0) && ethHasIP) {
        if (!ip.fromString(EthernetManager::getIPString())) {
            ip = IPAddress((uint32_t)0);
        }
    }
    out[i++] = ip[0];  // big-endian dotted quad
    out[i++] = ip[1];
    out[i++] = ip[2];
    out[i++] = ip[3];

    uint16_t port = RadioLayout::port(radio, WifiManager::getConfig().tcpPort);
    out[i++] = port & 0xFF;
    out[i++] = (port >> 8) & 0xFF;

    const char* ssid = ethHasIP && !WifiManager::isSTAConnected() && !WifiManager::isAPActive()
        ? "ethernet"
        : WifiManager::getSSID();
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
    if (!RadioLayout::acceptsPrimaryPort(port)) return false;
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

// ─── Count-driven setup and cooperative radio service ───────────
static void fillDeviceStatus(StatusResp& status) {
    // Device telemetry is shared. Radio snapshots/display refreshes must not
    // multiply ADC/I2C sampling or move slow board IO into every radio pass.
    static bool valid = false;
    static uint32_t sampledAt = 0;
    static int8_t temperature = INT8_MIN;
    static uint16_t battery = UINT16_MAX;
    const uint32_t now = millis();
    if (!valid || (uint32_t)(now - sampledAt) >= 1000U) {
        temperature = RuntimeStatsValues::cpuTemperatureC(compatReadCpuTemperature());
        battery = BatteryMonitor::readMilliVolts(BOARD.battery);
        sampledAt = now; valid = true;
    }
    status.temp_c = temperature;
    status.battery_mv = battery;
}
static void radioReply(const ResponseRoute& route, uint8_t cmd, const uint8_t* p, uint16_t n) {
    sendFrame(cmd, p, n, route);
}
static void updatePrimaryDisplay() {
    const RadioRuntime* owner = radioAt(0);
    if (!owner) return;
    const RadioConfig& config = owner->config();
    const StatusResp status = owner->status();
    oled.setRadioInfo(config.freq_hz, config.sf, config.bandwidth_hz, config.cr,
                      config.power_dbm, status.last_rssi, status.last_snr);
    oled.setStandby(owner->standby());
}
static void radioDiagnostic(uint8_t id, const char* text) {
    LOG_R_INFO("%s fw=%s %s", RadioLayout::at(id).label, fwVersion.c_str(), text);
}
static void createRadioOwners() {
    if (radioOwnerCount || !RadioHardware::validBindings()) return;
    const RadioRuntime::Hooks hooks{routeCurrent, routeCanReply, radioReply, publishRadioFrame, fillDeviceStatus, radioDiagnostic};
    for (size_t i = 0; i < RadioLayout::count(); ++i) {
        radioOwners[i] = new (std::nothrow) RadioRuntime((uint8_t)i, RadioLayout::at(i), hooks,
            i == 0 ? &RFFrontEnd::primary() : nullptr, RadioSettings::enabled(i));
        if (!radioOwners[i]) {
            for (size_t j = 0; j < i; ++j) { delete radioOwners[j]; radioOwners[j] = nullptr; }
            return;
        }
    }
    radioOwnerCount = RadioLayout::count();
}
static void serviceRadioIrqs() {
    static size_t first = 0;
    for (size_t i = 0; i < radioOwnerCount; ++i) radioOwners[(first + i) % radioOwnerCount]->serviceIrq();
    if (radioOwnerCount) first = (first + 1) % radioOwnerCount;
}
static void serviceRadioWork() {
    for (size_t i = 0; i < radioOwnerCount; ++i) {
        radioOwners[i]->service(); radioOwners[i]->maintenance();
    }
}
void processHostCommand(uint8_t cmd, const uint8_t* payload, uint16_t len,
                        const ResponseRoute& route) {
    if (!routeCurrent(route)) return;
    OTAManager::notifyValidFrame();
    const TransportSource src = route.transport;
    RadioRuntime* owner = radioAt(route.radio);
    const bool oldStandby = owner && owner->standby();
    const bool oldAutoCad = owner && owner->autoCad();
    if (owner && owner->command(cmd, payload, len, route)) {
        if (route.radio == 0) {
            updatePrimaryDisplay();
#if defined(BOARD_HELTEC_T114)
            if (oldStandby != owner->standby()) NodeState::setStandby(owner->standby());
            if (oldAutoCad != owner->autoCad()) NodeState::setAutoCad(owner->autoCad());
#endif
        }
        return;
    }
    (void)oldStandby; (void)oldAutoCad;
    switch (cmd) {
    case CMD_GET_WIFI: {
        uint8_t buf[200];
        uint16_t n = buildWifiStatusPayload(buf, route.radio);
        sendFrame(CMD_WIFI_STATUS, buf, n, route);
        break;
    }

    case CMD_SET_WIFI: {
#ifndef ARDUINO_ARCH_ESP32
        sendError(ERR_INVALID_CMD, route);   // no Wi-Fi stack on this build
        break;
#else
        // Remote provisioning over USB — eliminates the need to physically
        // connect to the Heltec's AP portal.
        WifiManager::Config newCfg;
        if (!parseSetWifi(payload, len, newCfg)) {
            sendError(ERR_INVALID_WIFI, route);
            break;
        }

        // Ack with the pending config so the host can log it BEFORE reboot.
        WifiManager::saveConfig(newCfg);
        uint8_t buf[200];
        uint16_t n = buildWifiStatusPayload(buf, route.radio);
        sendFrame(CMD_WIFI_STATUS, buf, n, route);

        if (src == TransportSource::USB) Serial.flush();
        delay(200);
        ESP.restart();
        break;  // unreached
#endif
    }

    case CMD_GET_VERSION: {
        const char* v = fwVersion.c_str();
        sendFrame(CMD_VERSION_RESP, (const uint8_t*)v, (uint16_t)strlen(v), route);
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
        sendFrame(CMD_DEBUG_RESP, buf, sizeof(buf), route);
        break;
    }

    case CMD_PING: {
        sendFrame(CMD_PONG, nullptr, 0, route);
        break;
    }

    case CMD_SET_DISPLAY_NAME: {
        if (route.radio != 0) { sendError(ERR_UNSUPPORTED, route); break; }
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
        sendFrame(CMD_SET_DISPLAY_NAME_RESP, &status, 1, route);
        break;
    }
    case CMD_ENTER_BOOTLOADER: {
#ifdef NRF52_SERIES
        // Preserve the protocol command's established Adafruit USB bootloader
        // transition (GPREGRET 0x57), but let the BSP perform the shutdown and
        // reset sequence rather than writing GPREGRET directly. The tested RAK
        // bootloader exposes serial DFU, not a UF2 mass-storage disk.
        sendFrame(CMD_PONG, nullptr, 0, route);
        LOG_R_INFO("ENTER_BOOTLOADER requested — entering USB serial DFU bootloader");
        delay(100);
        BootloaderManager::enterUf2Dfu();
#else
        sendError(ERR_INVALID_CMD, route);
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
        sendFrame(CMD_OTA_BEGIN_RESP, &status, 1, route);
        break;
    }
    case CMD_OTA_CHUNK: {
        uint8_t status = 1;   // bad_offset (no session active)
        sendFrame(CMD_OTA_CHUNK_RESP, &status, 1, route);
        break;
    }
    case CMD_OTA_VERIFY: {
        uint8_t resp[1 + 32] = {1};   // 1 = no buffer; sha256 zeros
        sendFrame(CMD_OTA_VERIFY_RESP, resp, sizeof(resp), route);
        break;
    }
    case CMD_OTA_APPLY: {
        uint8_t status = 1;
        sendFrame(CMD_OTA_APPLY_RESP, &status, 1, route);
        break;
    }
    case CMD_OTA_ABORT: {
        sendFrame(CMD_PONG, nullptr, 0, route);
        break;
    }

    case CMD_WIFI_RESET: {
        sendFrame(CMD_WIFI_RESET, nullptr, 0, route);
        if (src == TransportSource::USB) Serial.flush();
        delay(200);
        WifiManager::factoryReset();   // does not return
        break;
    }

    default:
        sendError(ERR_INVALID_CMD, route);
        break;
    }
}


// USB/UART and the unchanged W5100S backend retain the primary-only ABI.
void processHostCommand(uint8_t cmd, const uint8_t* payload, uint16_t len, TransportSource source) {
    processHostCommand(cmd, payload, len, primaryRoute(source));
}
static void noteRadioFrameError(uint8_t radio, uint8_t code) {
    if (auto* owner = radioAt(radio)) owner->noteFrameError(code);
}
static bool startRadioListeners(const String& token) {
    if (!radioOwnerCount || !RadioHardware::validBindings()) return false;
    uint16_t ports[RadioLayout::CAPACITY]{};
    for (size_t i = 0; i < radioOwnerCount; ++i) ports[i] = RadioLayout::port(i, WifiManager::getConfig().tcpPort);
#ifdef ARDUINO_ARCH_ESP32
    TCPServer::setRadioFrameErrorHandler(noteRadioFrameError);
    return TCPServer::beginRadios(ports, radioOwnerCount, token,
        static_cast<RadioCommandHandler>(processHostCommand));
#else
    if (radioOwnerCount != 1) return false;
    TCPServer::begin(ports[0], token);
    return true;
#endif
}

// ─── Serial-side parser callbacks ───────────────────────────
static void onSerialFrameOk(uint8_t cmd, const uint8_t* payload, uint16_t len,
                            TransportSource src) {
    lastUsbCmdMs = millis();
    processHostCommand(cmd, payload, len, src);
}

static void onSerialFrameErr(uint8_t err_code, TransportSource src) {
    noteRadioFrameError(0, err_code);
    LOG_R_ERR("frame parse error 0x%02X (src=%u)",
              (unsigned)err_code, (unsigned)src);
    sendError(err_code, src);
}

void noteTransportFrameError(uint8_t err_code) {
    noteRadioFrameError(0, err_code);
}

template <typename Stream>
static void serviceProtocolInput(Stream& stream, FrameParser& parser, TransportSource source) {
    bool complete = false;
    uint16_t bytes = 0;
    while (bytes < 256 && !complete && stream.available()) {
        const uint8_t b = (uint8_t)stream.read(); ++bytes;
        frameparser_feed(parser, b, source,
            [](uint8_t cmd, const uint8_t* p, uint16_t n, TransportSource src, void* context) {
                *static_cast<bool*>(context) = true; onSerialFrameOk(cmd, p, n, src);
            },
            [](uint8_t error, TransportSource src, void* context) {
                *static_cast<bool*>(context) = true; onSerialFrameErr(error, src);
            }, &complete);
    }
}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
#if defined(BOARD_RAK4631_WISMESH_ETH)
    // The RAK application does not run the Bluefruit SOC event task. Disable a
    // bootloader-inherited SoftDevice before USB starts so InternalFS writes are
    // synchronous instead of waiting forever for an undelivered flash event.
    Rak4631Config::prepareFlashRuntime();
#endif
    // PRG held ≥5s at boot → wipe Wi-Fi NVS and reboot. Must come before
    // other init so button sampling is clean.
    WifiManager::checkResetButton();

    // Drive the E22 EN pin LOW immediately so the LDOs and PA bias
    // see a clean, deliberate power-up — no-op on boards with
    // en_pin == -1. Counter starts now; rfSwitchEnHighAfterSettle()
    // below makes sure the full board.en_low_hold_ms has elapsed
    // before SPI traffic begins.
    RadioSettings::load();
    createRadioOwners();
    RadioHardware::prepareBoot(RadioSettings::activeMask());
    rak3401ReadyLedOffAtBoot();

#if defined(BOARD_HELTEC_T114)
    // Restore non-volatile T114 state BEFORE radio init so we know
    // whether to enter standby on boot. Display name will be picked
    // up by the OLED at the first show*() call.
    NodeState::begin();
    if (auto* primary = radioAt(0)) primary->restoreState(NodeState::getStandby(), NodeState::getAutoCad());
#endif

    Serial.begin(921600);
#if defined(OPENHOP_HWCDC_TX_KICK)
    // Nothing has been written yet, so this is the empty TX buffer size.
    usbTxStallWatch.prime(Serial.availableForWrite());
#endif
    // Network/radio startup must not depend on a USB host reading output.

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

    // Print the reason this boot started. Nordic RESETREAS is a bitmask
    // saved by the core before setup(); ESP32 reports an enum instead.
    // Keep all Nordic bits visible, including combined/unknown reasons.
    {
#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(ARDUINO_NRF52_ADAFRUIT)
        const uint32_t rr = compatResetReason();
        Serial.printf("[BOOT] reset_reason=0x%08lX (nRF52: pin=%u watchdog=%u software=%u lockup=%u)\n",
                      (unsigned long)rr,
                      (unsigned)((rr & POWER_RESETREAS_RESETPIN_Msk) != 0),
                      (unsigned)((rr & POWER_RESETREAS_DOG_Msk) != 0),
                      (unsigned)((rr & POWER_RESETREAS_SREQ_Msk) != 0),
                      (unsigned)((rr & POWER_RESETREAS_LOCKUP_Msk) != 0));
#else
        const char* labels[] = {
            "?", "POWERON", "EXT", "SW", "PANIC",
            "INT_WDT", "TASK_WDT", "WDT", "DEEPSLEEP",
            "BROWNOUT", "SDIO", "USB"
        };
        int rr = compatResetReason();
        const char* lbl = (rr >= 0 && (size_t)rr < sizeof(labels)/sizeof(labels[0]))
                              ? labels[rr] : "OTHER";
        Serial.printf("[BOOT] reset_reason=%d (%s)\n", rr, lbl);
#endif
    }

    // On boards with a PMU (LilyGO T-Beam-S3 Supreme), the OLED/LoRa/GNSS
    // rails are unpowered until this runs — must come before oled.begin(),
    // SPI.begin()/radio.begin(), and GPSManager::begin() below. No-op on
    // every other board (BOARD.pmu.enabled defaults to false). PmuManager
    // only exists on ESP32 builds (see the #ifdef ARDUINO_ARCH_ESP32 include
    // block above) — no nRF52 board has a PMU today.
#ifdef ARDUINO_ARCH_ESP32
    PmuManager::begin();
#endif

    // Boot splash covers board/radio/network initialization without delaying
    // entry into cooperative IRQ and host service after setup completes.
    oled.begin();
    EnvironmentSensor::begin();
    StationG3Power::begin();
#if defined(BOARD_HELTEC_T114)
    // Push restored state onto the OLED before showSplash so the
    // boot screen already has the right name + standby tag.
    oled.setDisplayName(NodeState::getDisplayName());
    oled.setStandby(radioAt(0) && radioAt(0)->standby());
#endif
    oled.showSplash();

    // Wait out the remaining EN-LOW hold (5 s on Ikoka; 0 on Heltec)
    // and raise EN HIGH for the rest of the device's lifetime. After
    // this point the RF switch is enabled; SX1262's DIO2 (or our
    // rx_pin / tx_pin GPIOs) will drive the actual TX/RX selection.
    for (size_t i = 0; i < radioOwnerCount; ++i) {
        if (radioOwners[i]->enabled() && radioOwners[i]->present()) radioOwners[i]->frontEnd().begin();
    }
    TBeam1WFan::begin();
    RadioHardware::enableRails(RadioSettings::activeMask());
    for (size_t i = 0; i < radioOwnerCount; ++i) {
        const bool ok = radioOwners[i]->begin();
        Serial.printf("[BOOT] %s ready=%d\n", RadioLayout::at(i).label, (int)ok);
        // Failure is local: every declared endpoint and healthy radio survives.
    }
    updatePrimaryDisplay();
    if (radioAt(0) && radioAt(0)->ready()) startRak3401ReadyLedHeartbeat();

    // ─── Network bring-up: Ethernet preferred, Wi-Fi fallback ──
    // On boards with both interfaces present (ESP32-P4-Nano), bringing
    // up Wi-Fi *and* Ethernet *and* the radio at the same time crashes
    // the C6 SDIO bridge from cumulative noise (see lesson_p4_nano_*).
    // Strategy: try Ethernet first; if a cable is plugged we keep ETH
    // and skip Wi-Fi; if no link and Wi-Fi is available, tear EMAC
    // back down (releasing RMII GPIOs) and fall back to Wi-Fi. Ethernet-only
    // boards keep EMAC active so later link/DHCP can start services. Boards without
    // Ethernet (`ethernet.enabled = false`) skip straight to Wi-Fi.
    // Initialize the DHCP option 121 module before either interface starts.
    // Its lifecycle hooks register on the lwIP core at DHCP PCB creation.
#ifdef ARDUINO_ARCH_ESP32
    DhcpRoutes::begin();
#endif
    WifiManager::loadConfigOnly();
    const auto& netCfg = WifiManager::getConfig();
    bool useEthernet = false;
    if (BOARD.ethernet.enabled) {
#if defined(OPENHOP_ETHERNET_W5100S)
        // Start from a clean, independent HTTP socket lifecycle. begin() is
        // safe before DHCP succeeds; its loop restarts port 80 when the
        // W5100S path becomes usable after a cable/DHCP transition.
        W5100sHttpServer::end();
#endif
        EthernetManager::begin(WifiManager::getHostname(),
                               netCfg.useStaticIP,
                               netCfg.staticIP,
                               netCfg.gateway,
                               netCfg.subnet,
                               netCfg.dns1,
                               netCfg.dns2);   // waits up to 5 s for link + DHCP
#if defined(OPENHOP_ETHERNET_W5100S)
        W5100sHttpServer::begin();
#endif
        if (EthernetManager::isLinkUp()) {
            useEthernet = true;
            Serial.println("[NET] Ethernet link up — Wi-Fi will be skipped");
        } else {
#if defined(OPENHOP_ETHERNET_W5100S)
            // W5100S is the only network path on this nRF52 target. Keep the
            // transport initialized so loop() can detect a later cable insert
            // and retry DHCP, rather than permanently disabling Ethernet at boot.
            Serial.println("[NET] no Ethernet link — keeping W5100S active for link/DHCP retry");
#else
            if (BOARD.has_wifi) {
                Serial.println("[NET] no Ethernet link — falling back to Wi-Fi");
                EthernetManager::end(); // free RMII pins for the real fallback
            } else {
                Serial.println("[NET] no Ethernet link — keeping Ethernet active for later link/DHCP");
            }
#endif
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
#if defined(OPENHOP_ETHERNET_W5100S)
        String token = wcfg.tcpToken;
#else
        String token = (BOARD.has_wifi || RadioLayout::count() > 1) ? wcfg.tcpToken : String();
#endif
        tcpStarted = startRadioListeners(token);

        OTAManager::begin(deviceHostname, token);
        otaStarted = true;
    }

    oled.showStatus(0, 0,
                    BOARD.has_wifi ? WifiManager::getSSID() : "---",
                    EthernetManager::hasIP() ? EthernetManager::getIPString()
                                             : (BOARD.has_wifi ? WifiManager::getIPString() : "---"),
                    "BOOT", fwVersion.c_str());
    currentScreen = Screen::STATUS;
    oledWakeUntil = millis() + OLED_WAKE_DURATION_MS;
    lastAutoCycleMs = millis();   // first auto-cycle fires SCREEN_AUTO_CYCLE_MS after splash

#if defined(ARDUINO_ARCH_ESP32) || \
    (defined(OPENHOP_RAK4631_GPS_SERIAL_ENABLE) && OPENHOP_RAK4631_GPS_SERIAL_ENABLE)
    GPSManager::begin(WifiManager::getConfig().gpsEnabled);
#endif

    // Arm the task watchdog LAST — everything above may legitimately take
    // many seconds (radio/PMU/Ethernet initialization). From now on, any loop()
    // iteration that doesn't complete within LOOP_WDT_TIMEOUT_S triggers a
    // panic reboot. If the bootloader is OTA-aware, the rolled-back slot
    // would take over; on stock Arduino bootloader, the same image reboots.
    // ESP-IDF 5.x autostarts the task WDT on the IDLE tasks; we just
    // adjust the timeout + enable panic so a stuck loop() reboots.
    // nRF52 starts the WDT peripheral with the same timeout; it resets the
    // chip directly, and compatWdtReset() feeds it.
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
#else
    compatWdtStart(LOOP_WDT_TIMEOUT_S);
#endif

#if defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(ARDUINO_NRF52_ADAFRUIT)
    // A soft reset may inherit a different, locked timeout/configuration.
    // Report observed registers rather than claiming the requested defaults.
    const uint32_t wdtRunning = NRF_WDT->RUNSTATUS;
    const uint32_t wdtReload = NRF_WDT->CRV;
    const uint32_t wdtChannels = NRF_WDT->RREN;
    const uint32_t wdtConfig = NRF_WDT->CONFIG;
    Serial.printf("[BOOT] firmware %s on %s ready (loop WDT %s, timeout=%lums, RR=0x%02lX, sleep=%s, debug=%s)\n",
                  fwVersion.c_str(), BOARD.name,
                  (wdtRunning & Nrf52LoopWatchdog::RUNSTATUS_RUNNING) ? "running" : "inactive",
                  (unsigned long)Nrf52LoopWatchdog::timeoutMilliseconds(wdtReload),
                  (unsigned long)wdtChannels,
                  (wdtConfig & WDT_CONFIG_SLEEP_Msk) ? "run" : "pause",
                  (wdtConfig & WDT_CONFIG_HALT_Msk) ? "run" : "pause");
#else
    Serial.printf("[BOOT] firmware %s on %s ready (loop WDT %us)\n",
                  fwVersion.c_str(), BOARD.name, (unsigned)LOOP_WDT_TIMEOUT_S);
#endif
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
    updateRak3401ReadyLedHeartbeat();

    // Invalidate stale TCP ownership before any USB/UART callback can drain
    // deferred RF work. Socket.connected() alone can survive a STA outage.
    if (BOARD.has_wifi) WifiManager::loop();
#ifdef ARDUINO_ARCH_ESP32
    const uint32_t invalidSTA = WifiManager::consumeSTAInvalidation();
    if (invalidSTA) {
        // A real outage remains invalid even after same-address recovery.
        // WifiManager excludes gateway-only DHCP changes at the producer.
        TCPServer::invalidateInterface(IPAddress(invalidSTA));
    }
#endif

    // Invalidation precedes deferred mutations; every radio IRQ precedes host input.
    serviceRadioIrqs();
    kickStalledUsbSerialTx();
    serviceProtocolInput(Serial, serialParser, TransportSource::USB);
    if (uartEnabled) serviceProtocolInput(PROTO_UART, uartParser, TransportSource::UART);

    if (tcpStarted) TCPServer::loop();
#if defined(OPENHOP_ETHERNET_W5100S)
    W5100sHttpServer::loop();
#endif
    if (otaStarted) OTAManager::loop();
#if defined(ARDUINO_ARCH_ESP32) || \
    (defined(OPENHOP_RAK4631_GPS_SERIAL_ENABLE) && OPENHOP_RAK4631_GPS_SERIAL_ENABLE)
    GPSManager::loop();
#endif

    serviceRadioWork();
    updatePrimaryDisplay();
    EthernetManager::loop();
    // Low-priority I2C telemetry runs only after radio IRQs and all host
    // transports have been drained for this iteration.
    StationG3Power::loop();
    EnvironmentSensor::loop();
    BatteryMonitor::loop(BOARD.battery);

    // Lazy TCP + OTA start if STA or Ethernet came up after boot.
    bool netUp = WifiManager::isSTAConnected() || EthernetManager::hasIP();
    if (!tcpStarted && netUp) {
        const auto& wcfg = WifiManager::getConfig();
#if defined(OPENHOP_ETHERNET_W5100S)
        String token = wcfg.tcpToken;
#else
        String token = (BOARD.has_wifi || RadioLayout::count() > 1) ? wcfg.tcpToken : String();
#endif
        tcpStarted = startRadioListeners(token);
    }
    if (!otaStarted && netUp) {
        const auto& wcfg = WifiManager::getConfig();
#if defined(OPENHOP_ETHERNET_W5100S)
        String token = wcfg.tcpToken;
#else
        String token = (BOARD.has_wifi || RadioLayout::count() > 1) ? wcfg.tcpToken : String();
#endif
        OTAManager::begin(deviceHostname, token);
        otaStarted = true;
    }

    const RadioConfig currentConfig = radioAt(0) ? radioAt(0)->config() : RadioConfig{};
    StatusResp status = radioAt(0) ? radioAt(0)->status() : StatusResp{};

    // PRG short-tap: cycle SLEEP → STATUS → RADIO → DIAGNOSTICS → STATUS → …
    // (Wi-Fi setup reset on 5 s hold-at-boot is handled in setup()/checkResetButton.)
    // Boards with pin_user_button < 0 (e.g. ESP32-P4-Nano where the BOOT
    // button shares a pin with RMII Ethernet TXD1) skip polling entirely.
    bool btn = false;
    if (BOARD.pin_user_button >= 0) {
        btn = (digitalRead(BOARD.pin_user_button) == (BOARD.user_button_active_low ? LOW : HIGH));
    }

    if (btn && millis() > prgIgnoreUntil) {
        prgIgnoreUntil = millis() + PRG_DEBOUNCE_MS;
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
                uint16_t batteryMv = BatteryMonitor::readMilliVolts(BOARD.battery);
                const float boardTemperatureC = TBeam1WFan::temperatureC();
                status.battery_mv = batteryMv;
                oled.showStatus(status.rx_count, status.tx_count,
                                ssid, ip, stateTag, fwVersion.c_str(), batteryMv,
                                boardTemperatureC);
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
                const uint16_t batteryMv =
                    BatteryMonitor::readMilliVolts(BOARD.battery);
                const float boardTemperatureC = TBeam1WFan::temperatureC();
                oled.showDiagnostics(uptime, ip.c_str(), usb_idle,
                                     status.rx_count, status.tx_count,
                                     status.crc_errors, batteryMv,
                                     boardTemperatureC, fwVersion.c_str());
            }
        }
    }
}
