// =============================================================
// boards/rak4631_wismesh_eth.h — RAKwireless WisMesh Ethernet Gateway
//
// Target hardware:
//   * RAK4631 Core Module: Nordic nRF52840 + Semtech SX1262
//   * RAK19007 Base Board
//   * RAK13800 Ethernet Module: WIZnet W5100S on WisBlock IO SPI
//   * Optional RAK19018 PoE module
//
// Pin sources:
//   * RAK4631 datasheet internal SX1262 connections:
//       NSS=P1.10, SCK=P1.11, MOSI=P1.12, MISO=P1.13,
//       BUSY=P1.14, DIO1=P1.15, NRESET=P1.06.
//   * RAK-nRF52-Arduino WisCore_RAK4631_Board variant:
//       WB_IO2=34, WB_IO3=21, WB_SPI_CS=26,
//       WB_SPI_CLK=3, WB_SPI_MISO=29, WB_SPI_MOSI=30.
//   * RAK13800 quickstart:
//       WB_IO2 enables module power, WB_IO3 resets W5100S,
//       Ethernet.init(SS) selects WB_SPI_CS.
//
// This target intentionally keeps the openHop Modem wire protocol unchanged and
// exposes it over TCP on W5100S Ethernet. USB-CDC remains available for
// flashing, recovery and logs.
//
// nRF52 GPIO numbering follows the repo's identity variant convention:
// raw pin number N maps to Arduino pin N. P1.x is encoded as 32 + x.
// =============================================================
#pragma once

// W5100S / RAK13800 wiring on WisBlock IO slot.
#ifndef PYMC_ETH_POWER_PIN
#  define PYMC_ETH_POWER_PIN 34   // WB_IO2 — RAK13800 power enable
#endif
#ifndef PYMC_ETH_RESET_PIN
#  define PYMC_ETH_RESET_PIN 21   // WB_IO3 — RAK13800 W5100S reset
#endif
#ifndef PYMC_ETH_CS_PIN
#  define PYMC_ETH_CS_PIN 26      // WB_SPI_CS / SS
#endif
#ifndef PYMC_ETH_INT_PIN
#  define PYMC_ETH_INT_PIN 10     // WB_IO6 / RAK13800 INTn, currently unused
#endif
#ifndef PYMC_ETH_SPI_SCK
#  define PYMC_ETH_SPI_SCK 3      // WB_SPI_CLK
#endif
#ifndef PYMC_ETH_SPI_MISO
#  define PYMC_ETH_SPI_MISO 29    // WB_SPI_MISO
#endif
#ifndef PYMC_ETH_SPI_MOSI
#  define PYMC_ETH_SPI_MOSI 30    // WB_SPI_MOSI
#endif
#ifndef PYMC_RAK4631_SX126X_POWER_EN
#  define PYMC_RAK4631_SX126X_POWER_EN 37  // P1.05 — RAK4631 SX1262 power enable
#endif

// MeshCore's RAK4631 board implementation and the official RAK variant both
// identify WB_A0 as raw pin 5 / P0.05 / AIN3. These hooks permit a downstream
// hardware revision to disable or recalibrate the divider without source edits.
#ifndef PYMC_RAK4631_BATTERY_ADC_PIN
#  define PYMC_RAK4631_BATTERY_ADC_PIN 5
#endif
#ifndef PYMC_RAK4631_BATTERY_ADC_REFERENCE_MV
#  define PYMC_RAK4631_BATTERY_ADC_REFERENCE_MV 3000
#endif
#ifndef PYMC_RAK4631_BATTERY_DIVIDER_NUMERATOR
#  define PYMC_RAK4631_BATTERY_DIVIDER_NUMERATOR 173
#endif
#ifndef PYMC_RAK4631_BATTERY_DIVIDER_DENOMINATOR
#  define PYMC_RAK4631_BATTERY_DIVIDER_DENOMINATOR 100
#endif

// Serial1 is authoritatively mapped to MCU RX=15/TX=16 by the RAK4631
// Arduino variant, and RAK documents Slot A as UART-capable. Leave support off
// by default because UART presence cannot prove that a GPS is fitted and no
// safe module power/reset control is established for this Ethernet product.
#ifndef PYMC_RAK4631_GPS_SERIAL_ENABLE
#  define PYMC_RAK4631_GPS_SERIAL_ENABLE 0
#endif
#ifndef PYMC_RAK4631_GPS_DEFAULT_ENABLED
#  define PYMC_RAK4631_GPS_DEFAULT_ENABLED PYMC_RAK4631_GPS_SERIAL_ENABLE
#endif

inline const BoardConfig BOARD = {
    "RAK4631 WisMesh Ethernet",
    "rak4631_wismesh_eth",
    "pymc-raketh",

    // SX1262 internal LoRa bus/control pins.
    42,  // pin_lora_nss  = P1.10
    38,  // pin_lora_rst  = P1.06
    46,  // pin_lora_busy = P1.14
    47,  // pin_lora_dio1 = P1.15
    43,  // pin_lora_sck  = P1.11
    45,  // pin_lora_miso = P1.13
    44,  // pin_lora_mosi = P1.12

    // RAK4631 uses SX1262 DIO2 for the RF switch. RAK docs note that
    // P1.07/ANT_SW should not be initialized for custom SX1262 firmware.
    {-1, 0, -1, -1, true},

    -1,    // pin_lora_tx_led
    true,  // lora_tx_led_active_high

    // No display in this firmware target.
    -1,  // pin_i2c_sda
    -1,  // pin_i2c_scl
    -1,  // pin_i2c_oled_rst
    -1,  // pin_vext_enable_low

    -1,    // pin_tft_sda
    -1,    // pin_tft_scl
    -1,    // pin_tft_dc
    -1,    // pin_tft_rst
    -1,    // pin_tft_cs
    -1,    // pin_tft_bl
    true,  // tft_bl_active_high
    -1,    // pin_tft_power
    true,  // tft_power_active_high

    -1,    // pin_user_button — RAK19007 reset is hardware reset, no PRG GPIO
    true,  // user_button_active_low

    // WB_A0 / P0.05 / AIN3 battery divider. The official RAK example uses
    // the 3.0 V internal reference, 12-bit ADC, and a 1.73 divider scale.
    // Take eight nonblocking samples. Reject samples outside a
    // deliberately broad LiPo range; five valid readings are required.
    {
        PYMC_RAK4631_BATTERY_ADC_PIN, -1, true, 0.0f,
        0, 0x02, 0,
        PYMC_RAK4631_BATTERY_ADC_REFERENCE_MV, 12,
        PYMC_RAK4631_BATTERY_DIVIDER_NUMERATOR,
        PYMC_RAK4631_BATTERY_DIVIDER_DENOMINATOR,
        8, 2500, 5000, 5,
    },

    22,  // max_tx_power_dbm — RAK4631/SX1262 PA_BOOST ceiling

    true,  // use_dio3_tcxo
    1.8f,  // tcxo_voltage

    140,   // sx126x_current_limit_ma — SX1262 PA_BOOST/RF switch path
    true,  // sx126x_rx_boosted_gain — improve weak-packet receive margin
    true,  // sx126x_register_patch — same SX126x sensitivity patch as tracker boards

    true,   // has_lora_radio
    false,  // has_wifi — nRF52840 has BLE but no Wi-Fi
    {},     // wifi_antenna_switch
    true,   // has_network — W5100S Ethernet transport

    // USB-CDC only for the openHop Modem protocol fallback; Ethernet is primary.
    -1,      // pin_protocol_uart_rx
    -1,      // pin_protocol_uart_tx
    921600,  // protocol_uart_baud

#if PYMC_RAK4631_GPS_SERIAL_ENABLE
    15,     // pin_gps_uart_rx — Serial1 / Slot A UART RX
    16,     // pin_gps_uart_tx — Serial1 / Slot A UART TX
#else
    -1,     // pin_gps_uart_rx — unavailable until explicitly enabled
    -1,     // pin_gps_uart_tx
#endif
    9600,   // gps_uart_baud
    -1,     // pin_gps_enable — no proven safe module power control
    true,   // gps_enable_active_high
    -1,     // pin_gps_reset — no proven safe reset mapping for this product
    false,  // gps_reset_active_high
    false,  // gps_send_casic_config — generic NMEA / u-blox, never CASIC writes

    // `ethernet.enabled` gates network bring-up in main.cpp. The rest of
    // this struct describes RMII PHYs on ESP32-P4 and is not used by W5100S.
    {true, BoardConfig::EthernetPhy::NONE, -1, -1, -1, -1, false, false,
     {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},

    // Official MeshCore RAK4631 firmware drives SX126X_POWER_EN high before
    // radio init. Keep that as a fixed board GPIO rather than overloading the
    // RF-switch EN delay path.
    {{PYMC_RAK4631_SX126X_POWER_EN, true}, {-1, false}, {-1, false}, {-1, false}},
    1,
};
