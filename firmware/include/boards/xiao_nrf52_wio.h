// =============================================================
// boards/xiao_nrf52_wio.h — Seeed XIAO nRF52840 + Wio-SX1262
// Meshtastic kit (SKU 102010710).
//
// Hardware refs:
//   * Seeed product page + datasheet (102010710.pdf in _incoming/
//     once vendored)
//   * MeshCore xiao_nrf52 variant (build_flags pinout)
//   * Adafruit nRF52 bootloader OTAFIX xiao_nrf52840_ble (USB IDs,
//     P0.18 BTN0, P0.26 LED_RED reference)
//   * Custom variant: firmware/variants/XIAO_nRF52_Wio/ provides
//     an identity Arduino-pin ↔ raw-nRF-GPIO map, so the numbers
//     below match the silicon directly.
//
// Pin map (raw nRF52840 GPIO):
//   P0.04 = SX1262 NSS  (XIAO label D4)
//   P0.28 = SX1262 RST  (D2)
//   P0.29 = SX1262 BUSY (D3)
//   P0.03 = SX1262 DIO1 (D1, IRQ)
//   P0.05 = SX1262 RXEN (D5, LNA gate — RadioLib drives HIGH on RX)
//   P1.13 = SX1262 SCK  (D8)
//   P1.14 = SX1262 MISO (D9)
//   P1.15 = SX1262 MOSI (D10)
//   P0.18 = USER button (also Adafruit bootloader DFU button)
//
// No Wi-Fi, no Ethernet, no onboard display — USB-CDC transport
// only (XIAO has native USB on nRF52 USB peripheral). BLE 5.0
// hardware present but not driven by this firmware.
//
// RF switch: SX1262 controls TX path via DIO2 (internal switch),
// and the Wio-SX1262 carrier adds an external LNA whose enable
// rides on D5 (RXEN). Both must be wired in rf_switch{}.
//
// Max chip TX power on bare SX1262: 22 dBm. No external PA.
//
// Keep this aggregate positional rather than C++ designated-initialized:
// the nRF52 toolchain uses GCC 7 and does not support non-trivial
// designated initializers in C++.
// =============================================================
#pragma once

inline const BoardConfig BOARD = {
    "XIAO nRF52840 + Wio-SX1262",
    "xiao_nrf52_wio",
    "xiao-nrf",   // unused — nRF52 has no Wi-Fi/mDNS

    // SX1262 control pins (raw nRF52 GPIO via the identity variant).
    4,   // pin_lora_nss  = D4
    28,  // pin_lora_rst  = D2
    29,  // pin_lora_busy = D3
    3,   // pin_lora_dio1 = D1
    // SPI line numbers are informational on nRF52 (firmware calls
    // SPI.begin() without arguments — the variant.h PIN_SPI_* macros
    // pick the bus pins) but must be != -1 so main.cpp's SPI-init
    // branch fires.
    45,  // pin_lora_sck  = D8  = P1.13
    46,  // pin_lora_miso = D9  = P1.14
    47,  // pin_lora_mosi = D10 = P1.15

    {-1, 0, 5, -1, true},  // RXEN on D5/P0.05; TX via DIO2

    -1,    // pin_lora_tx_led
    true,  // lora_tx_led_active_high

    // No I2C peripheral on this kit. D4/D5 are claimed by NSS/RXEN,
    // so the silkscreen "SDA/SCL" labels are not honoured here.
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

    18,    // pin_user_button = P0.18 (Adafruit BTN0 / DFU)
    true,  // user_button_active_low

    {-1, -1, true, 0.0f},  // no battery sense

    22,  // max_tx_power_dbm — bare SX1262 chip ceiling

    true,  // use_dio3_tcxo
    1.8f,  // tcxo_voltage — 32 MHz TCXO on Wio-SX1262

    -1,     // sx126x_current_limit_ma
    false,  // sx126x_rx_boosted_gain
    false,  // sx126x_register_patch

    true,   // has_lora_radio
    false,  // has_wifi — nRF52 has BLE but not Wi-Fi
    {},     // wifi_antenna_switch
    false,  // has_network — no WiFi/TCP/OTA stack on this build

    // No dedicated protocol UART — USB-CDC only.
    -1,      // pin_protocol_uart_rx
    -1,      // pin_protocol_uart_tx
    921600,  // protocol_uart_baud

    -1,     // pin_gps_uart_rx
    -1,     // pin_gps_uart_tx
    9600,   // gps_uart_baud
    -1,     // pin_gps_enable
    true,   // gps_enable_active_high
    -1,     // pin_gps_reset
    false,  // gps_reset_active_high
    true,   // gps_send_casic_config

    {false, BoardConfig::EthernetPhy::NONE, -1, -1, -1, -1, false, false,
     {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},

    {{-1, false}, {-1, false}, {-1, false}, {-1, false}},
    0,
};
