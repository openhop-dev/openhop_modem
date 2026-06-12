// =============================================================
// boards/station_g2.h — B&Q Consulting Station G2
//
// Hardware refs:
//   * https://wiki.uniteng.com/en/meshtastic/station-g2
//
// ESP32-S3 + SX1262 with an external high-power PA/LNA front end. The
// final RF output can be much higher than the SX1262 drive level, so keep
// max_tx_power_dbm at Meshtastic's conservative chip-drive cap (19 dBm),
// not the advertised PA output power.
// =============================================================
#pragma once

inline const BoardConfig BOARD = {
    .name        = "Station G2",
    .fw_suffix   = "station_g2",
    .mdns_prefix = "station-g2",

    // SX1262 control + remapped SPI bus from Meshtastic station-common.
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

    // Built-in OLED is SH1107. Disable until the display backend supports it.
    .pin_i2c_sda      = -1,
    .pin_i2c_scl      = -1,
    .pin_i2c_oled_rst = -1,
    .pin_vext_enable_low = -1,

    .pin_user_button        = 38,
    .user_button_active_low = true,

    .max_tx_power_dbm = 19,

    .use_dio3_tcxo = true,
    .tcxo_voltage  = 1.8f,

    .has_lora_radio = true,
    .has_wifi       = true,
    .has_network    = true,
    .pin_protocol_uart_rx = -1,
    .pin_protocol_uart_tx = -1,
    .protocol_uart_baud   = 921600,
    .ethernet = { .enabled = false },

    // Station G2 does not need additional fixed FEM GPIO levels beyond the
    // SX1262 DIO2/DIO3 radio controls above. Keep this explicit now that
    // BoardConfig supports Heltec V4 static front-end GPIO setup.
    .static_gpios = {},
    .static_gpio_count = 0,
};
