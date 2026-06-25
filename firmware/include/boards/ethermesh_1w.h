// =============================================================
// boards/ethermesh_1w.h — MeshSmith EtherMesh-1W
//
// Hardware:
//   * Base board: Waveshare ESP32-P4-ETH
//   * Radio: Ebyte E22P/SX1262 1 W module
//   * Network: onboard IP101GRI Ethernet PHY only
//
// The ESP32-P4-ETH has no ESP32-C6 Wi-Fi coprocessor, so this variant
// brings the network up over wired Ethernet. The USB-C programming/debug
// port is a CH343P USB-UART bridge on UART0, so Arduino USB CDC is off.
//
// E22P pinout:
//   NSS=GPIO20, SCK=GPIO21, MOSI=GPIO22, MISO=GPIO23
//   RST=GPIO26, RF_EN=GPIO27, BUSY=GPIO32, DIO1=GPIO33
//
// RF_EN must be HIGH for the board's lifetime. en_low_hold_ms=0 makes
// main.cpp drive it HIGH immediately before radio init.
// =============================================================
#pragma once

inline const BoardConfig BOARD = {
    .name        = "EtherMesh-1W",
    .fw_suffix   = "ethermesh_1w",
    .mdns_prefix = "ethermesh-1w",

    .pin_lora_nss  = 20,
    .pin_lora_rst  = 26,
    .pin_lora_busy = 32,
    .pin_lora_dio1 = 33,
    .pin_lora_sck  = 21,
    .pin_lora_miso = 23,
    .pin_lora_mosi = 22,

    .rf_switch = {
        .en_pin            = 27,
        .en_low_hold_ms    = 0,
        .rx_pin            = -1,
        .tx_pin            = -1,
        .dio2_as_rf_switch = true,
    },

    // ESP32-P4-ETH exposes I2C on the same pins as the P4-Nano reference.
    // If no external SSD1306 is fitted, the display driver remains inert.
    .pin_i2c_sda      = 7,
    .pin_i2c_scl      = 8,
    .pin_i2c_oled_rst = -1,
    .pin_vext_enable_low = -1,

    // BOOT shares GPIO35 with RMII TXD1 once Ethernet is enabled.
    .pin_user_button        = -1,
    .user_button_active_low = true,

    .battery = { .pin = -1 },

    .max_tx_power_dbm = 30,

    .use_dio3_tcxo = true,
    .tcxo_voltage  = 1.8f,
    .sx126x_current_limit_ma = 140,
    .sx126x_rx_boosted_gain = true,

    .has_lora_radio = true,
    .has_wifi       = false,
    .wifi_antenna_switch = { .enabled = false },
    .has_network    = true,

    .pin_protocol_uart_rx = -1,
    .pin_protocol_uart_tx = -1,
    .protocol_uart_baud   = 921600,

    .ethernet = {
        .enabled          = true,
        .phy_type         = BoardConfig::EthernetPhy::IP101,
        .pin_mdc          = 31,
        .pin_mdio         = 52,
        .pin_phy_reset    = 51,
        .phy_addr         = 1,
        .rmii_clock_input = true,
        .use_static_ip    = false,
        .static_ip        = {192, 168, 5, 10},
        .gateway          = {192, 168, 5, 1},
        .subnet           = {255, 255, 255, 0},
        .dns              = {192, 168, 5, 1},
    },

    .static_gpios = {},
    .static_gpio_count = 0,
};
