// =============================================================
// boards/rak3112_wismesh.h — RAK3112 WisDuo module on a WisMesh carrier
//
// Hardware ref: https://docs.rakwireless.com/Product-Categories/WisDuo/RAK3112-Module/
// LoRa: bare SX1262 (no PA, +22 dBm max), but with an external 50 Ω RF
// switch on the carrier board. The switch's enable line is wired to
// ESP32-S3 GPIO4 ("ANT_SW") — datasheet flags this pin as
// "used internally and not available", meaning firmware MUST drive it
// HIGH or there's no antenna path.
//
// SPI is remapped versus ESP32-S3 default (FSPI 11/13/12), so we call
// SPI.begin(SCK, MISO, MOSI, NSS) explicitly. The bus pins happen to
// match the LilyGO T3-S3 (5/3/6/7) — different chip, identical pinout.
//
// 16 MB flash + 8 MB octal PSRAM; native USB-CDC straight to GPIO19/20
// (no UART bridge), so CDC_ON_BOOT=1 like Ikoka and T3-S3.
//
// Internal connections (datasheet table p.2):
//   SX1262 SPI_NSS  → GPIO7
//   SX1262 SPI_SCK  → GPIO5
//   SX1262 SPI_MISO → GPIO3
//   SX1262 SPI_MOSI → GPIO6
//   SX1262 NRESET   → GPIO8
//   SX1262 ANT_SW   → GPIO4   (RF switch enable, internal-only pin)
//   SX1262 DIO1     → GPIO47
//   SX1262 BUSY     → GPIO48
//
// OLED I2C pins are NOT defined by the module itself — they live on the
// WisMesh carrier-board. The defaults below use the I2C2 pair brought
// out by the module (SCL=GPIO18, SDA=GPIO17) which is what most RAK
// WisMesh tracker carriers wire the SSD1306 to. If the OLED stays
// blank after flash, switch to I2C1 (SCL=GPIO40, SDA=GPIO9).
// =============================================================
#pragma once

inline const BoardConfig BOARD = {
    .name        = "RAK3112 WisMesh",
    .fw_suffix   = "rak3112",
    .mdns_prefix = "rak3112",

    .pin_lora_nss  = 7,
    .pin_lora_rst  = 8,
    .pin_lora_busy = 48,
    .pin_lora_dio1 = 47,
    .pin_lora_sck  = 5,
    .pin_lora_miso = 3,
    .pin_lora_mosi = 6,

    .rf_switch = {
        .en_pin            = 4,        // ANT_SW — must be HIGH for any RF
        .en_low_hold_ms    = 0,        // bare SX1262, no PA peaks → no settle delay
        .rx_pin            = -1,
        .tx_pin            = -1,
        .dio2_as_rf_switch = true,     // SX1262 DIO2 drives T/R CTRL internally
    },

    // OLED on the WisMesh carrier — best guess pin pair, verify on board
    .pin_i2c_sda      = 17,           // I2C2 SDA
    .pin_i2c_scl      = 18,           // I2C2 SCL
    .pin_i2c_oled_rst = -1,           // RAK carriers usually skip the RST line
    .pin_vext_enable_low = -1,        // OLED powered straight from 3V3 rail

    .pin_user_button        = 0,      // BOOT button on GPIO0
    .user_button_active_low = true,

    .max_tx_power_dbm = 22,           // RAK3112 datasheet: programmable up to +22 dBm

    .use_dio3_tcxo = true,
    .tcxo_voltage  = 1.8f,             // 32 MHz TCXO, powered from DIO3 at 1.8 V

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
