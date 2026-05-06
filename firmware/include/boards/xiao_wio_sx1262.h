// =============================================================
// boards/xiao_wio_sx1262.h — Seeed XIAO ESP32-S3 + Wio-SX1262 expansion
//
// Hardware refs (verified from _incoming/):
//   * XIAO ESP32-S3 schematic: XIAO_ESP32S3_V1.3_SCH_260115.pdf
//   * Wio-SX1262 carrier:      Schematic_Diagram_Wio-SX1262_for_XIAO.pdf
//   * Module datasheet:        Wio-SX1262_Module_Datasheet.pdf
// Pin assignment cross-checked with MeshCore-main/variants/xiao_s3_wio.
//
// Wio-SX1262 is Seeed's bare SX1262 expansion that snaps onto the underside
// of the XIAO. No PA, no LNA bypass — +22 dBm peak from the SX1262 alone.
// USB-CDC native (XIAO has no CP2102 / CH340 bridge), so CDC_ON_BOOT=1.
//
// RF switch — hybrid scheme (different from every other board we support):
//   * SX1262 DIO2  → drives TX path internally (setDio2AsRfSwitch=true).
//   * GPIO38 (RXEN) → gates RX-only LNA path. RadioLib toggles this HIGH
//     during RX and LOW during TX via setRfSwitchPins(38, NC).
//   The firmware code in rfSwitchConfigureRadio() runs BOTH calls — the
//   else-if was widened to two independent ifs in 2026-05-06.
//
// Pinout (XIAO label → ESP32-S3 GPIO → role):
//   D7  → 41 → SX1262 NSS
//   D8  → 7  → SX1262 SCK
//   D9  → 8  → SX1262 MISO
//   D10 → 9  → SX1262 MOSI
//   PAD11 → 38 → SX1262 RXEN (LNA enable, host-driven)
//   PAD12 → 39 → SX1262 DIO1 (IRQ)
//   PAD13 → 40 → SX1262 BUSY
//   PAD14 → 42 → SX1262 RESET
//   D4  → 5  → I2C SDA   (XIAO default Wire pins; OLED if user solders one)
//   D5  → 6  → I2C SCL
//   BOOT button → 21
//   user LED   → 48
// =============================================================
#pragma once

inline const BoardConfig BOARD = {
    .name        = "XIAO Wio-SX1262",
    .fw_suffix   = "xiao_wio",
    .mdns_prefix = "xiao-wio",

    .pin_lora_nss  = 41,
    .pin_lora_rst  = 42,
    .pin_lora_busy = 40,
    .pin_lora_dio1 = 39,
    .pin_lora_sck  = 7,
    .pin_lora_miso = 8,
    .pin_lora_mosi = 9,

    .rf_switch = {
        .en_pin            = -1,     // no separate master-enable rail
        .en_low_hold_ms    = 0,
        .rx_pin            = 38,     // RXEN — RadioLib drives HIGH during RX
        .tx_pin            = -1,     // TX path comes from DIO2 internally
        .dio2_as_rf_switch = true,
    },

    // I2C — XIAO's default Wire pins (D4/D5 = GPIO5/6). Wio-SX1262 carrier
    // doesn't ship with an OLED, but if operator solders one to D4/D5/3V3/GND
    // these pins make it light up out of the box.
    .pin_i2c_sda      = 5,
    .pin_i2c_scl      = 6,
    .pin_i2c_oled_rst = -1,
    .pin_vext_enable_low = -1,         // 3V3 is always-on from XIAO regulator

    .pin_user_button        = 21,      // BOOT button (also wakes from deep sleep)
    .user_button_active_low = true,

    .max_tx_power_dbm = 22,            // bare SX1262, no PA

    .use_dio3_tcxo = true,
    .tcxo_voltage  = 1.8f,             // 32 MHz TCXO on the Wio carrier

    .has_lora_radio = true,
    .has_wifi       = true,
    .has_network    = true,
    // Protocol on USB-CDC (Serial); no dedicated UART for the binary protocol.
    .pin_protocol_uart_rx = -1,
    .pin_protocol_uart_tx = -1,
    .protocol_uart_baud   = 921600,
    .ethernet = { .enabled = false },
};
