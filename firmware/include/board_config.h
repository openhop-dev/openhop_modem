// =============================================================
// board_config.h — Per-board hardware abstraction
//
// Each supported board ships a header in boards/ that instantiates a
// `BoardConfig` constant called `BOARD`. main.cpp / oled_display.cpp /
// ota_manager.cpp pull pin numbers and policies from BOARD.* instead
// of using hard-coded #defines, so adding a new board is a one-file job.
//
// Board selection happens at compile time via a `-DBOARD_<name>` flag
// in platformio.ini's build_flags. Exactly one must be defined.
// =============================================================
#pragma once

#include <stdint.h>

// ─── RF switch policy ───────────────────────────────────────
// Different SX1262 carrier boards control the RF switch / PA / LNA
// in different ways. This struct lets each board declare its policy
// without firmware changes elsewhere.
//
//   en_pin            GPIO that gates the entire RF switch (the "EN"
//                     column of the E22 datasheet truth table). The
//                     firmware drives it LOW for `en_low_hold_ms`
//                     during boot to let the module power up cleanly,
//                     then raises it HIGH and never lowers it again.
//                     Set to -1 if the board has no external switch
//                     (e.g. bare SX1262 on Heltec V3 — the SX1262's
//                     own RF switch is fully internal).
//
//   en_low_hold_ms    Boot-time hold duration for `en_pin` LOW. Ebyte
//                     E22P modules need ≥5000 ms for the LDOs and
//                     PA bias to settle before EN goes HIGH; ignored
//                     when en_pin == -1.
//
//   rx_pin / tx_pin   GPIOs that RadioLib should auto-toggle on each
//                     transmit/receive (LOW/HIGH per direction).
//                     Use these for boards with two separate MCU-driven
//                     switch lines. Leave at -1 to skip.
//
//   dio2_as_rf_switch When true, configure SX1262 to drive its DIO2
//                     pin HIGH during TX. On boards where DIO2 is
//                     wired (PCB trace) to the module's T/R CTRL pin
//                     this gives auto TX-path switching with zero MCU
//                     involvement. Heltec V3 uses DIO2 for the
//                     SX1262's internal switch; Ikoka Stick wires
//                     DIO2 ↔ TXEN externally.
struct RfSwitchPolicy {
    int8_t   en_pin;
    uint16_t en_low_hold_ms;
    int8_t   rx_pin;
    int8_t   tx_pin;
    bool     dio2_as_rf_switch;
};

struct StaticGpioLevel {
    int8_t pin;
    bool   level_high;
};

struct BatterySenseConfig {
    int8_t pin;                 // ADC pin, -1 when no battery sense exists
    int8_t enable_pin;          // optional divider/ADC gate, -1 when always on
    bool   enable_active_high;
    float  multiplier;          // raw pin voltage -> pack voltage
    uint8_t fuel_gauge_i2c_addr = 0;      // MAX17048-style fuel gauge, 0=none
    uint8_t fuel_gauge_vcell_reg = 0x02;  // VCELL register (78.125 uV/LSB)
    uint8_t fuel_gauge_crate_reg = 0;     // CRATE register, 0=not exposed
};

struct WifiAntennaSwitchConfig {
    bool   enabled = false;      // true only on boards with a Wi-Fi RF switch
    int8_t gpio3_pin = -1;       // ESP32-C6 Wi-Fi antenna select line 1
    int8_t gpio14_pin = -1;      // ESP32-C6 Wi-Fi antenna select line 2
};

// ─── Full per-board config ──────────────────────────────────
struct BoardConfig {
    const char* name;            // Display name on the OLED splash
    const char* fw_suffix;       // Appended to FW_VERSION (e.g. "ikoka")
    const char* mdns_prefix;     // mDNS hostname stem; final form is
                                 // "<prefix>-<mac3>.local"

    // SX1262 SPI + control pins. SCK/MISO/MOSI fall back to the
    // board variant's default SPI bus when set to -1; otherwise the
    // firmware calls SPI.begin(sck, miso, mosi, nss) explicitly so
    // boards that remap the bus (LilyGO T3-S3 etc.) just work.
    int8_t pin_lora_nss;
    int8_t pin_lora_rst;
    int8_t pin_lora_busy;
    int8_t pin_lora_dio1;        // DIO1 → MCU (IRQ line)
    int8_t pin_lora_sck;         // -1 = use board default SPI
    int8_t pin_lora_miso;
    int8_t pin_lora_mosi;

    RfSwitchPolicy rf_switch;

    // Optional LoRa TX activity LED. Boards without one leave pin at -1.
    int8_t pin_lora_tx_led = -1;
    bool   lora_tx_led_active_high = true;

    // I2C bus for the SSD1306 OLED (same driver across all boards).
    int8_t pin_i2c_sda;
    int8_t pin_i2c_scl;
    int8_t pin_i2c_oled_rst;     // -1 if the OLED has no reset line

    // Heltec V3 routes its 3.3 V VEXT rail (which powers the OLED)
    // through a P-MOSFET enabled by a LOW level on GPIO 36; Ikoka
    // and other boards power the OLED straight from 3V3 and leave
    // this at -1 so the firmware skips the dance.
    int8_t pin_vext_enable_low;

    // Optional SPI TFT. Boards without one leave these pins at -1.
    int8_t pin_tft_sda = -1;
    int8_t pin_tft_scl = -1;
    int8_t pin_tft_dc = -1;
    int8_t pin_tft_rst = -1;
    int8_t pin_tft_cs = -1;
    int8_t pin_tft_bl = -1;
    bool   tft_bl_active_high = true;
    int8_t pin_tft_power = -1;
    bool   tft_power_active_high = true;

    // PRG / user button. active_low = pressed pulls LOW.
    int8_t pin_user_button;
    bool   user_button_active_low;

    // Optional LiPo battery monitor. `battery.pin = -1` reports null/unknown.
    // Voltage is sampled in millivolts and exposed in status/API stats.
    BatterySenseConfig battery;

    // Hardware RF ceiling. Firmware clamps any requested TX power to
    // this value; lets the host config drive everything below.
    int8_t max_tx_power_dbm;

    // SX1262 TCXO control. All Ebyte/Heltec carrier boards use a
    // 32 MHz TCXO powered by SX1262 DIO3 at 1.8 V.
    bool  use_dio3_tcxo;
    float tcxo_voltage;

    // Optional SX126x board-level tuning applied after radio.begin().
    int16_t sx126x_current_limit_ma = -1;
    bool    sx126x_rx_boosted_gain = false;
    bool    sx126x_register_patch = false;

    // Some carriers (ESP32-P4-Nano) ship without an SX1262 — the
    // module is added later. When false, main.cpp / wifi_manager skip
    // every radio code path: no SX1262 init, no SET_CONFIG, no CAD,
    // no RX worker. CMD_GET_CONFIG / CMD_STATUS still answer with
    // current cached state so pymc_repeater can probe the modem.
    bool has_lora_radio;

    // ESP32-P4 has no native Wi-Fi/BT — it relies on an ESP32-C6
    // co-processor running esp_hosted firmware over SDIO. If the C6
    // hasn't been provisioned, calling WiFi.mode()/softAP() panics
    // the chip. Set has_wifi = false to skip WifiManager::begin()
    // entirely (Ethernet still comes up). Flip back to true once
    // esp_hosted is flashed on the C6.
    bool has_wifi;

    // Optional Wi-Fi antenna switch. The ESP32-C6 Photon board uses two
    // GPIOs to select the external Wi-Fi antenna path. Other boards leave
    // this disabled so no extra settings or GPIO writes appear there.
    WifiAntennaSwitchConfig wifi_antenna_switch;

    // Whole-board network capability gate. ESP32 targets use the native
    // Wi-Fi/Ethernet managers; the RAK4631 nRF52840 target uses a W5100S
    // adapter that exposes the same TCP/HTTP lifecycle. USB-only nRF52840
    // boards set `has_network = false`, so the protocol talks only over
    // USB-CDC and the optional dedicated UART (`pin_protocol_uart_*`).
    bool has_network;

    // Optional dedicated UART for the binary protocol (sector-mode
    // wiring, T114 over UART, etc.). When pin_protocol_uart_rx >= 0
    // and pin_protocol_uart_tx >= 0 the firmware listens for SYNC
    // frames on a HardwareSerial(1) configured on those pins instead
    // of Arduino `Serial`; `Serial` (USB-CDC / UART0) stays free for
    // printf debug. Pin = -1 → protocol on `Serial` (USB-CDC default).
    int8_t   pin_protocol_uart_rx;
    int8_t   pin_protocol_uart_tx;
    uint32_t protocol_uart_baud;

    // Optional on-board GPS receiver. When both pins are >= 0 the firmware
    // reads NMEA from this UART and exposes parsed data in /api/stats -> gps.
    int8_t   pin_gps_uart_rx = -1;
    int8_t   pin_gps_uart_tx = -1;
    uint32_t gps_uart_baud = 9600;
    int8_t   pin_gps_enable = -1;       // optional GPS/GNSS power-enable GPIO
    bool     gps_enable_active_high = true;
    int8_t   pin_gps_reset = -1;        // optional GPS reset GPIO
    bool     gps_reset_active_high = false;
    bool     gps_send_casic_config = true; // false for modules/boards that should be left at defaults

    // ─── On-board Ethernet (RMII PHY) ───────────────────────
    // Set ethernet.enabled = true on boards with an internal EMAC +
    // external PHY (currently only ESP32-P4-Nano with IP101GRI). The
    // RMII data lines are bound to the EMAC peripheral by the
    // chip's GPIO matrix; we only need to configure the management
    // interface (MDC/MDIO), the PHY reset pin, and the PHY address.
    enum class EthernetPhy : uint8_t {
        NONE = 0,
        IP101 = 1,
        // Add LAN8720, RTL8201, KSZ8081, DM9051 etc. as needed.
    };
    struct EthernetConfig {
        bool        enabled;
        EthernetPhy phy_type;
        int8_t      pin_mdc;
        int8_t      pin_mdio;
        int8_t      pin_phy_reset;     // -1 if not wired
        int8_t      phy_addr;          // RMII address (set by AD0/AD3 strap pins)
        bool        rmii_clock_input;  // true: 50 MHz clock comes FROM the PHY
        // Static IP (optional). When use_static_ip = true the
        // EthernetManager calls ETH.config() before ETH.begin() so
        // the interface comes up with the configured address instead
        // of waiting for DHCP. All fields are stored as 4-byte octet
        // arrays so the config remains aggregate-initialised.
        bool        use_static_ip;
        uint8_t     static_ip[4];
        uint8_t     gateway[4];
        uint8_t     subnet[4];
        uint8_t     dns[4];
    };
    EthernetConfig ethernet;

    // Optional board-specific GPIO levels that must be asserted before radio
    // init and then left alone. Heltec V4 uses these for the PA/LNA front-end
    // mode pins; older boards leave static_gpio_count at 0.
    StaticGpioLevel static_gpios[4];
    uint8_t static_gpio_count;
};

extern const BoardConfig BOARD;

// ─── Board selection ────────────────────────────────────────
#if defined(BOARD_HELTEC_V3)
#  include "boards/heltec_v3.h"
#elif defined(BOARD_HELTEC_V4)
#  include "boards/heltec_v4.h"
#elif defined(BOARD_HELTEC_V42)
#  include "boards/heltec_v42.h"
#elif defined(BOARD_HELTEC_V43)
#  include "boards/heltec_v43.h"
#elif defined(BOARD_IKOKA_STICK)
#  include "boards/ikoka_stick.h"
#elif defined(BOARD_LILYGO_T3S3)
#  include "boards/lilygo_t3s3.h"
#elif defined(BOARD_RAK3112_WISMESH)
#  include "boards/rak3112_wismesh.h"
#elif defined(BOARD_ESP32_P4_NANO)
#  include "boards/esp32_p4_nano.h"
#elif defined(BOARD_ETHERMESH_1W)
#  include "boards/ethermesh_1w.h"
#elif defined(BOARD_HELTEC_T114)
#  include "boards/heltec_t114.h"
#elif defined(BOARD_HELTEC_TRACKER_V2)
#  include "boards/heltec_tracker_v2.h"
#elif defined(BOARD_XIAO_WIO_SX1262)
#  include "boards/xiao_wio_sx1262.h"
#elif defined(BOARD_PHOTON_1W_XIAO_ESP32C6)
#  include "boards/photon_1w_xiao_esp32c6.h"
#elif defined(BOARD_XIAO_NRF52_WIO)
#  include "boards/xiao_nrf52_wio.h"
#elif defined(BOARD_RAK4631_WISMESH_ETH)
#  include "boards/rak4631_wismesh_eth.h"
#elif defined(BOARD_STATION_G2)
#  include "boards/station_g2.h"
#else
#  error "No board selected — add one of -DBOARD_HELTEC_V3 / -DBOARD_HELTEC_V4 / -DBOARD_HELTEC_V42 / -DBOARD_HELTEC_V43 / -DBOARD_IKOKA_STICK / -DBOARD_LILYGO_T3S3 / -DBOARD_RAK3112_WISMESH / -DBOARD_ESP32_P4_NANO / -DBOARD_ETHERMESH_1W / -DBOARD_HELTEC_T114 / -DBOARD_HELTEC_TRACKER_V2 / -DBOARD_XIAO_WIO_SX1262 / -DBOARD_PHOTON_1W_XIAO_ESP32C6 / -DBOARD_XIAO_NRF52_WIO / -DBOARD_RAK4631_WISMESH_ETH / -DBOARD_STATION_G2 to platformio.ini build_flags"
#endif
