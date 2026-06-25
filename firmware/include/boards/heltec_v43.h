// =============================================================
// boards/heltec_v43.h — Heltec WiFi LoRa 32 V4.3
// ESP32-S3 + SX1262 + KCT8103L PA/FEM, with FEM LNA bypassed.
//
// The core board, display, battery, and LoRa pin map match heltec_v4.h.
// V4.3 swaps the front-end to KCT8103L; recent MeshCore firmware bypasses
// its receive LNA because the PA/FEM can raise the receive noise floor.
// This dedicated openHop modem variant keeps SX1262 boosted RX gain enabled
// while defaulting KCT8103L CTX high for FEM LNA bypass. The device web UI can
// toggle CTX low to enable the external RX LNA when an operator wants it, and
// can set agc.reset.interval for periodic RX AGC resets during long idle time.
// =============================================================
#pragma once

inline const BoardConfig BOARD = {
    .name        = "Heltec V4.3",
    .fw_suffix   = "heltec-v43",
    .mdns_prefix = "heltec-v43",

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
    // SX1262. V4.3's front-end can amplify RF output, but RadioLib still
    // configures SX1262 output power, so clamp the chip command to 22 dBm.
    .max_tx_power_dbm = 22,

    .use_dio3_tcxo = true,
    .tcxo_voltage  = 1.8f,

    // Goal by default: radio.rxgain on + radio.fem.rxgain off.
    .sx126x_rx_boosted_gain = true,

    .has_lora_radio = true,
    .has_wifi       = true,
    .has_network    = true,

    .pin_protocol_uart_rx = -1,
    .pin_protocol_uart_tx = -1,
    .protocol_uart_baud   = 921600,

    // Heltec V4.3 GNSS connector, matching MeshCore's working runtime mapping.
    // MeshCore defines PIN_GPS_RX=38 and PIN_GPS_TX=39, then calls
    // Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX), so Arduino's RX argument is
    // GPIO39 and TX argument is GPIO38.
    //   GPIO34 VGNSS_Ctrl: active-low GNSS power enable
    //   GPIO42 RST_GPS: active-low GNSS reset
    .pin_gps_uart_rx = 39,
    .pin_gps_uart_tx = 38,
    .gps_uart_baud   = 9600,
    .pin_gps_enable = 34,
    .gps_enable_active_high = false,
    .pin_gps_reset = 42,
    .gps_reset_active_high = false,
    .gps_send_casic_config = false,

    .ethernet = { .enabled = false },

    // Heltec V4.3 KCT8103L front-end support:
    //   GPIO7 VFEM_Ctrl / LDO enable: HIGH = front-end rail on
    //   GPIO2 KCT8103L CSD/chip enable: HIGH = PA/FEM enabled
    //   GPIO5 KCT8103L CTX: HIGH = RX LNA bypass, LOW = RX LNA on
    // GPIO46 is GC1109-only on V4.2 and is intentionally not driven here.
    // SX1262 DIO2 remains the automatic TX/RX path-select line.
    .static_gpios = {
        { 7, true },
        { 2, true },
        { 5, true },  // boot default; RFFrontEnd applies the saved web UI setting
    },
    .static_gpio_count = 3,
};
