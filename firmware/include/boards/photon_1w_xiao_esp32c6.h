// =============================================================
// boards/photon_1w_xiao_esp32c6.h — MeshSmith Photon 1W
// Seeed XIAO ESP32-C6 + SX1262/E22P class 1W LoRa front end.
//
// Dedicated Photon C6 variant.  This keeps the generic XIAO ESP32-C6
// and Wio-SX1262 variants untouched while matching the Photon nRF/XIAO
// physical pinout used by the existing Photon nRF hardware:
//   D0  -> SX1262 DIO1 (IRQ)
//   D1  -> SX1262 RESET
//   D2  -> SX1262 BUSY
//   D3  -> SX1262 NSS / CS
//   D4  -> I2C SDA
//   D5  -> I2C SCL
//   D8  -> SPI SCK
//   D9  -> SPI MISO
//   D10 -> SPI MOSI
//   GPIO3/GPIO14 -> ESP32-C6 Wi-Fi antenna switch
//
// Arduino-ESP32 XIAO ESP32-C6 pin map:
//   D1=GPIO1, D2=GPIO2, D3=GPIO21, D4=GPIO22, D5=GPIO23,
//   D6=GPIO16, D7=GPIO17, D8=GPIO19, D9=GPIO20, D10=GPIO18.
//
// RF switch follows the Photon nRF variant: SX1262 DIO2 drives the RF
// switch path; no MCU RXEN/TXEN GPIO is wired.
// =============================================================
#pragma once

inline const BoardConfig BOARD = {
    .name        = "Photon 1W XIAO ESP32-C6",
    .fw_suffix   = "photon_1w_xiao_esp32c6",
    .mdns_prefix = "photon-c6",

    .pin_lora_nss  = 21,   // D3
    .pin_lora_rst  = 1,    // D1
    .pin_lora_busy = 2,    // D2
    .pin_lora_dio1 = 0,    // D0
    .pin_lora_sck  = 19,   // D8
    .pin_lora_miso = 20,   // D9
    .pin_lora_mosi = 18,   // D10

    .rf_switch = {
        .en_pin            = -1,
        .en_low_hold_ms    = 0,
        .rx_pin            = -1,
        .tx_pin            = -1,     // TX path comes from SX1262 DIO2
        .dio2_as_rf_switch = true,
    },

    // Photon pinout reserves XIAO D4/D5 for I2C.  There is no required
    // onboard OLED for this modem, but these pins match the Photon header.
    .pin_i2c_sda      = 22,   // D4
    .pin_i2c_scl      = 23,   // D5
    .pin_i2c_oled_rst = -1,
    .pin_vext_enable_low = -1,

    .pin_user_button        = 9,     // BOOT button on Seeed XIAO ESP32-C6
    .user_button_active_low = true,

    // Original MeshCore Photon firmware reads battery voltage from the
    // MAX17048 fuel gauge on the Photon I2C bus (D4/D5), address 0x36,
    // VCELL register 0x02.  No ADC divider is needed on the C6 module.
    .battery = {
        .pin = -1,
        .enable_pin = -1,
        .enable_active_high = false,
        .multiplier = 0.0f,
        .fuel_gauge_i2c_addr = 0x36,
        .fuel_gauge_vcell_reg = 0x02,
    },

    .max_tx_power_dbm = 30,          // Photon 1W / E22P class front end

    .use_dio3_tcxo = true,
    .tcxo_voltage  = 1.8f,

    .sx126x_current_limit_ma = 140,
    .sx126x_rx_boosted_gain = true,

    .has_lora_radio = true,
    .has_wifi       = true,

    // External Wi-Fi antenna path: GPIO3 LOW and GPIO14 HIGH.
    .wifi_antenna_switch = {
        .enabled = true,
        .gpio3_pin = 3,
        .gpio14_pin = 14,
    },
    .has_network    = true,

    // Protocol on USB-CDC by default; TCP is also available after Wi-Fi setup.
    .pin_protocol_uart_rx = -1,
    .pin_protocol_uart_tx = -1,
    .protocol_uart_baud   = 921600,

    .ethernet = { .enabled = false },
    .static_gpios = {},
    .static_gpio_count = 0,
};
