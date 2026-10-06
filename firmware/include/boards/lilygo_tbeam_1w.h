// =============================================================
// boards/lilygo_tbeam_1w.h — LilyGO T-Beam 1W
//
// ESP32-S3 + SX1262 + external 1 W PA, L76K GNSS, SH1106 OLED, and
// board NTC/fan. The SX1262 is driven at its 22 dBm chip limit; the
// external PA provides the antenna-side 1 W output.
//
// GPIO21 is the PA module's CTRL/LNA line, not a PA enable. DIO2 is the
// RF switch: DIO2=1/CTRL=0 for TX and DIO2=0/CTRL=1 for RX.
// =============================================================
#pragma once

inline const BoardConfig BOARD = {
    .name        = "LilyGO T-Beam 1W",
    .fw_suffix   = "lilygo_tbeam_1w",
    .mdns_prefix = "lilygo-tbeam-1w",

    .pin_lora_nss  = 15,
    .pin_lora_rst  = 3,
    .pin_lora_busy = 38,
    .pin_lora_dio1 = 1,
    .pin_lora_sck  = 13,
    .pin_lora_miso = 12,
    .pin_lora_mosi = 11,

    .rf_switch = {
        .en_pin            = 40,  // radio/PA LDO enable
        .en_low_hold_ms    = 0,
        .rx_pin            = 21,  // CTRL/LNA; LOW before radio power
        .tx_pin            = -1,
        .dio2_as_rf_switch = true,
    },

    .pin_lora_tx_led = 18,
    .lora_tx_led_active_high = true,

    .pin_i2c_sda      = 8,
    .pin_i2c_scl      = 9,
    .pin_i2c_oled_rst = -1,
    .pin_vext_enable_low = -1,

    .pin_user_button        = 0,
    .user_button_active_low = true,

    .battery = {
        .pin = 4,
        .enable_pin = -1,
        .enable_active_high = false,
        .multiplier = 3.0f,
        .sample_count = 8,
        .minimum_plausible_mv = 5500,
        .maximum_plausible_mv = 8600,
        .adc_attenuation_db = 11,
    },

    .max_tx_power_dbm = 22,

    .use_dio3_tcxo = true,
    .tcxo_voltage  = 3.0f,
    .sx126x_current_limit_ma = 140,
    .sx126x_rx_boosted_gain = true,

    .has_lora_radio = true,
    .has_wifi       = true,
    .has_network    = true,
    .pin_protocol_uart_rx = -1,
    .pin_protocol_uart_tx = -1,
    .protocol_uart_baud   = 921600,

    .pin_gps_uart_rx = 5,
    .pin_gps_uart_tx = 6,
    .gps_uart_baud   = 9600,
    .pin_gps_enable  = 16,

    .ethernet = { .enabled = false },
    .static_gpios = {
        { 10, true },  // SD CS shares the radio SPI bus; keep it deselected.
        { 21, false }, // CTRL/LNA off before GPIO40 powers the module.
    },
    .static_gpio_count = 2,
    .thermistor = { .ntc_pin = 14, .fan_pin = 41 },
    .pa_ramp_time_us = 1700,
};
