// =============================================================
// boards/heltec_v4.h — Heltec WiFi LoRa 32 V4 (ESP32-S3 + SX1262 + FEM)
//
// Heltec V4 keeps the V3-style OLED and SX1262 modem layout, but uses an
// explicit ESP32-S3 board definition/variant because PlatformIO does not ship
// a V4 board package yet. LoRa pins match Heltec's V4 PlatformIO guide and
// Meshtastic's V4 hardware mapping.
// =============================================================
#pragma once

inline const BoardConfig BOARD = {
    .name        = "Heltec V4",
    .fw_suffix   = "heltec-v4",
    .mdns_prefix = "heltec-v4",

    // SX1262 control + SPI pins. Unlike V3, the V4 variant is repo-local, so
    // bind SPI explicitly to the Heltec V4 pins from pins_arduino.h.
    .pin_lora_nss  = 8,
    .pin_lora_rst  = 12,
    .pin_lora_busy = 13,
    .pin_lora_dio1 = 14,
    .pin_lora_sck  = 9,
    .pin_lora_miso = 11,
    .pin_lora_mosi = 10,

    .rf_switch = {
        .en_pin            = -1,
        .en_low_hold_ms    = 0,
        .rx_pin            = -1,
        .tx_pin            = -1,
        .dio2_as_rf_switch = true,  // DIO2 drives the SX1262/FEM TX/RX path
    },

    // Built-in 0.96" SSD1306 OLED on the VEXT rail.
    .pin_i2c_sda      = 17,
    .pin_i2c_scl      = 18,
    .pin_i2c_oled_rst = 21,
    .pin_vext_enable_low = 36,

    .pin_user_button         = 0,
    .user_button_active_low  = true,

    // LiPo monitor from Heltec/Meshtastic mapping: GPIO1 through a high-
    // impedance divider, gated by GPIO37 HIGH. Multiplier includes the
    // divider ratio plus the calibration factor used by the upstream map.
    .battery = {
        .pin = 1,
        .enable_pin = 37,
        .enable_active_high = true,
        .multiplier = 4.90f * 1.045f,
    },

    // Keep the firmware's host-visible setting equivalent to Heltec V3/bare
    // SX1262. V4's front-end can amplify RF output, but RadioLib still configures
    // SX1262 output power, so clamp the chip command to its normal ceiling.
    .max_tx_power_dbm = 22,

    .use_dio3_tcxo = true,
    .tcxo_voltage  = 1.8f,

    .has_lora_radio = true,
    .has_wifi       = true,
    .has_network    = true,

    .pin_protocol_uart_rx = -1,
    .pin_protocol_uart_tx = -1,
    .protocol_uart_baud   = 921600,
    .ethernet = { .enabled = false },

    // Heltec V4.x front-end support:
    //   GPIO7  VFEM_Ctrl / LDO enable: HIGH = front-end rail on
    //   GPIO2  FEM CSD/chip enable:    HIGH = PA/LNA enabled
    //   GPIO46 GC1109 CPS:             HIGH = full PA mode on V4.2
    //   GPIO5  KCT8103L CTX:           LOW  = RX LNA path on V4.3+
    // SX1262 DIO2 remains the automatic TX/RX path-select line.
    .static_gpios = {
        { 7,  true  },
        { 2,  true  },
        { 46, true  },
        { 5,  false },
    },
    .static_gpio_count = 4,
};
