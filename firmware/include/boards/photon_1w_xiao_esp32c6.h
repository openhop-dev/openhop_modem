// =============================================================
// boards/photon_1w_xiao_esp32c6.h — MeshSmith Photon 1W
// Seeed XIAO ESP32-C6 + SX1262/E22P class 1W LoRa front end.
//
// Dedicated Photon C6 variant.  This keeps the generic XIAO ESP32-C6
// and Wio-SX1262 variants untouched while matching the Photon nRF/XIAO
// physical pinout used by the existing Photon hardware:
//   D1  -> SX1262 DIO1 (IRQ)
//   D2  -> SX1262 RESET
//   D3  -> SX1262 BUSY
//   D4  -> SX1262 NSS / CS
//   D5  -> RXEN / LNA enable
//   D6  -> I2C SCL
//   D7  -> I2C SDA
//   D8  -> SPI SCK
//   D9  -> SPI MISO
//   D10 -> SPI MOSI
//   GPIO3/GPIO14 -> ESP32-C6 Wi-Fi antenna switch
//
// Arduino-ESP32 XIAO ESP32-C6 pin map:
//   D1=GPIO1, D2=GPIO2, D3=GPIO21, D4=GPIO22, D5=GPIO23,
//   D6=GPIO16, D7=GPIO17, D8=GPIO19, D9=GPIO20, D10=GPIO18.
//
// RF switch follows the Photon/Wio style: SX1262 DIO2 drives the TX
// path, and D5/RXEN is toggled HIGH during RX by RadioLib.
// =============================================================
#pragma once

inline const BoardConfig BOARD = {
    .name        = "Photon 1W XIAO ESP32-C6",
    .fw_suffix   = "photon_1w_xiao_esp32c6",
    .mdns_prefix = "photon-c6",

    .pin_lora_nss  = 22,   // D4
    .pin_lora_rst  = 2,    // D2
    .pin_lora_busy = 21,   // D3
    .pin_lora_dio1 = 1,    // D1
    .pin_lora_sck  = 19,   // D8
    .pin_lora_miso = 20,   // D9
    .pin_lora_mosi = 18,   // D10

    .rf_switch = {
        .en_pin            = -1,
        .en_low_hold_ms    = 0,
        .rx_pin            = 23,     // D5 / RXEN — RadioLib drives HIGH during RX
        .tx_pin            = -1,     // TX path comes from SX1262 DIO2
        .dio2_as_rf_switch = true,
    },

    // Photon pinout reserves XIAO D6/D7 for I2C.  There is no required
    // onboard OLED for this modem, but these pins match the Photon header.
    .pin_i2c_sda      = 17,   // D7
    .pin_i2c_scl      = 16,   // D6
    .pin_i2c_oled_rst = -1,
    .pin_vext_enable_low = -1,

    .pin_user_button        = 9,     // BOOT button on Seeed XIAO ESP32-C6
    .user_button_active_low = true,

    .battery = { .pin = -1 },

    .max_tx_power_dbm = 30,          // Photon 1W / E22P class front end

    .use_dio3_tcxo = true,
    .tcxo_voltage  = 1.8f,

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
