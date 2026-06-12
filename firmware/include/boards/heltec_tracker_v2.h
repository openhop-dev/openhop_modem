// =============================================================
// boards/heltec_tracker_v2.h — Heltec Wireless Tracker V2
// ESP32-S3 + SX1262 + KCT8103L PA/FEM, Wi-Fi/TCP modem build.
// =============================================================
#pragma once

inline const BoardConfig BOARD = {
    .name        = "Heltec Tracker V2",
    .fw_suffix   = "heltec_tracker_v2",
    .mdns_prefix = "heltec-tracker-v2",

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
        .dio2_as_rf_switch = true,
    },

    .pin_lora_tx_led = 18,
    .lora_tx_led_active_high = true,

    // Tracker V2 uses an ST7735 TFT on a dedicated write-only SPI bus.
    // VEXT powers the display/GPS peripheral rail; this build only claims
    // it while the display is on.
    .pin_i2c_sda      = -1,
    .pin_i2c_scl      = -1,
    .pin_i2c_oled_rst = -1,
    .pin_vext_enable_low = -1,
    .pin_tft_sda = 42,
    .pin_tft_scl = 41,
    .pin_tft_dc  = 40,
    .pin_tft_rst = 39,
    .pin_tft_cs  = 38,
    .pin_tft_bl  = 21,
    .tft_bl_active_high = true,
    .pin_tft_power = 3,
    .tft_power_active_high = true,

    .pin_user_button        = 0,
    .user_button_active_low = true,

    // MeshCore distinguishes the default LORA_TX_POWER=9 dBm from the board
    // ceiling MAX_LORA_TX_POWER=22 dBm. This clamp is the SX1262 chip-side
    // maximum; the KCT8103L PA/FEM path is estimated around 28 dBm at antenna.
    .max_tx_power_dbm = 22,

    .use_dio3_tcxo = true,
    .tcxo_voltage  = 1.8f,
    .sx126x_current_limit_ma = 140,
    .sx126x_rx_boosted_gain = true,
    .sx126x_register_patch = true,

    .has_lora_radio = true,
    .has_wifi       = true,
    .has_network    = true,
    .pin_protocol_uart_rx = -1,
    .pin_protocol_uart_tx = -1,
    .protocol_uart_baud   = 921600,
    .ethernet = { .enabled = false },
    .static_gpios = {
        { 7, true },  // P_LORA_PA_POWER / VFEM_Ctrl
        { 4, true },  // P_LORA_KCT8103L_PA_CSD
        { 5, true },  // P_LORA_KCT8103L_PA_CTX: LNA bypass, MeshCore default
    },
    .static_gpio_count = 3,
};
