// =============================================================
// boards/station_g3.h — BQ Voyage Station G3 (BQESP32V1M)
//
// Hardware refs:
//   * https://wiki.bqvoy.com/en/devkits/station-g3
//   * https://tools.bqvoy.com/stationg3/pinout/?board=BQESP32V1M-40pin
//
// The ESP32-S3 daughterboard preserves Station G2's radio, display, button,
// and optional GPS wiring. Station G3 additionally routes PA PL1 and the
// primary-slot LNA mode to GPIO9/GPIO10. Firmware keeps PA PL1 at the safe
// low-power level and enables the external LNA only in RX. Physical PA PL1
// and LNA P jumpers must be removed for GPIO control to apply.
// =============================================================
#pragma once

inline const BoardConfig BOARD = {
    .name        = "Station G3",
    .fw_suffix   = "station_g3",
    .mdns_prefix = "station-g3",

    .pin_lora_nss  = 11,
    .pin_lora_rst  = 21,
    .pin_lora_busy = 47,
    .pin_lora_dio1 = 48,
    .pin_lora_sck  = 12,
    .pin_lora_miso = 14,
    .pin_lora_mosi = 13,

    .rf_switch = {
        .en_pin            = -1,
        .en_low_hold_ms    = 0,
        .rx_pin            = -1,
        .tx_pin            = -1,
        .dio2_as_rf_switch = true,
    },

    .pin_i2c_sda      = 5,
    .pin_i2c_scl      = 6,
    .pin_i2c_oled_rst = -1,
    .pin_vext_enable_low = -1,

    .pin_user_button        = 38,
    .user_button_active_low = true,

    // BQ's G3 tables permit up to 22 dBm chip drive, but the Station G2-
    // compatible 19 dBm ceiling is conservative across PA jumper modes.
    // The external PA's antenna output is much higher than this value.
    .max_tx_power_dbm = 19,

    .use_dio3_tcxo = true,
    .tcxo_voltage  = 1.8f,
    .sx126x_current_limit_ma = 140,
    .sx126x_rx_boosted_gain = false,

    .has_lora_radio = true,
    .has_wifi       = true,
    .has_network    = true,
    .pin_protocol_uart_rx = -1,
    .pin_protocol_uart_tx = -1,
    .protocol_uart_baud   = 921600,

    // Optional GROVE GPS socket: IO7 receives GPS TX, IO15 transmits to GPS RX.
    // Do not send vendor-specific CASIC setup commands to an arbitrary module.
    .pin_gps_uart_rx = 7,
    .pin_gps_uart_tx = 15,
    .gps_uart_baud   = 9600,
    .gps_send_casic_config = false,

    .ethernet = { .enabled = false },
    .static_gpios = {},
    .static_gpio_count = 0,

    // GPIO9 LOW selects the lower software PA mode; PA PL2 still determines
    // the physical level within that mode. GPIO10 is LOW for LNA on and HIGH
    // for LNA off.
    .rf_frontend = {
        .pa_mode_pin = 9,
        .pa_high_active_high = true,
        .pa_default_high = false,
        .pa_user_selectable = true,
        .lna_mode_pin = 10,
        .lna_enabled_active_high = false,
        .lna_default_enabled = true,
        .lna_user_selectable = true,
    },
    .sx126x_agc_reset_interval_ms = 0,
};
