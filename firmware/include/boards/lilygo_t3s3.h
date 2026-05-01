// =============================================================
// boards/lilygo_t3s3.h — LilyGO T-Lora T3-S3 v1.2 (ESP32-S3 + bare SX1262)
//
// Hardware ref: https://github.com/Xinyuan-LilyGO/TTGO-LoRa32-T3S3
// LoRa front end: bare SX1262 (no PA / no LNA, ~22 dBm max),
//                 internal RF switch driven by SX1262 DIO2 — no external
//                 EN pin to manage and no boot-time hold needed.
//
// SPI is remapped versus the ESP32-S3 default (FSPI 11/13/12), so the
// firmware drives SPI.begin(SCK, MISO, MOSI, NSS) explicitly.
//
// Pin map matches the Meshtastic upstream "lilygo_tlora_t3s3" variant.
// =============================================================
#pragma once

inline const BoardConfig BOARD = {
    .name        = "LilyGO T3-S3 v1.2",
    .fw_suffix   = "lilygo_t3s3",
    .mdns_prefix = "lilygo-t3s3",

    // SX1262 control + remapped SPI bus
    .pin_lora_nss  = 7,
    .pin_lora_rst  = 8,
    .pin_lora_busy = 34,
    .pin_lora_dio1 = 33,
    .pin_lora_sck  = 5,
    .pin_lora_miso = 3,
    .pin_lora_mosi = 6,

    .rf_switch = {
        .en_pin            = -1,    // bare SX1262 — RF switch is internal
        .en_low_hold_ms    = 0,
        .rx_pin            = -1,
        .tx_pin            = -1,
        .dio2_as_rf_switch = true,  // DIO2 drives the SX1262's internal switch
    },

    // Built-in 0.96" SSD1306 OLED on the I2C breakout
    .pin_i2c_sda      = 18,
    .pin_i2c_scl      = 17,
    .pin_i2c_oled_rst = 21,           // some v1.2 boards skip this — try -1 if OLED stays blank
    .pin_vext_enable_low = -1,        // OLED powered directly from 3V3

    .pin_user_button        = 0,      // BOOT button on GPIO0
    .user_button_active_low = true,

    .max_tx_power_dbm = 22,           // SX1262 spec ceiling

    .use_dio3_tcxo = true,
    .tcxo_voltage  = 1.8f,

    .has_lora_radio = true,
    .has_wifi       = true,
    .has_network = true,
    // Protocol on USB-CDC (Serial) — no dedicated UART for the
    // binary protocol. Sector-mode boards override these.
    .pin_protocol_uart_rx = -1,
    .pin_protocol_uart_tx = -1,
    .protocol_uart_baud   = 921600,
    .ethernet = { .enabled = false },
};
