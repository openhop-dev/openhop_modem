// MeshSmith EtherMesh-Duo: Waveshare ESP32-P4-ETH, two RAK13302/SX1262
// modules. GPIO mapping retained from the previously supplied user schematic;
// the current PCB layout screenshot is not a carrier netlist. Confirm actual
// copper, fitted module power revision/band and supply before flashing/TX.
//
// ANT_SW enables tied SKY66122 CSD/CPS, not an E22P power-rail hold.
// CTX is wired internally to SX1262 DIO2. Do not invent a boost-EN GPIO or
// infer module power-pin assignments from conflicting RAK publications.
#pragma once

inline const BoardConfig ETHERMESH_DUO_RF1 = {
    .name = "EtherMesh-Duo",
    .fw_suffix = "ethermesh_duo",
    .mdns_prefix = "ethermesh-duo",
    .pin_lora_nss = 20,
    .pin_lora_rst = 26,
    .pin_lora_busy = 32,
    .pin_lora_dio1 = 33,
    .pin_lora_sck = 21,
    .pin_lora_miso = 23,
    .pin_lora_mosi = 22,
    .rf_switch = {-1, 0, -1, -1, true},
    .pin_i2c_sda = 7,
    .pin_i2c_scl = 8,
    .pin_i2c_oled_rst = -1,
    .pin_vext_enable_low = -1,
    // GPIO35 shares RMII TXD1; never poll it as an active user button.
    .pin_user_button = -1,
    .user_button_active_low = true,
    .battery = {},
    // SX1262 drive only. The external PA can yield about 30 dBm output.
    .max_tx_power_dbm = 22,
    .use_dio3_tcxo = true,
    .tcxo_voltage = 1.8f,
    .sx126x_current_limit_ma = 140,
    .sx126x_rx_boosted_gain = true,
    .sx126x_register_patch = true,
    .has_lora_radio = true,
    .has_wifi = false,
    .wifi_antenna_switch = {},
    .has_network = true,
    .pin_protocol_uart_rx = -1,
    .pin_protocol_uart_tx = -1,
    .protocol_uart_baud = 921600,
    .ethernet = {
        .enabled = true,
        .phy_type = BoardConfig::EthernetPhy::IP101,
        .pin_mdc = 31,
        .pin_mdio = 52,
        .pin_phy_reset = 51,
        .phy_addr = 1,
        .rmii_clock_input = true,
        .use_static_ip = false,
        .static_ip = {192,168,5,10},
        .gateway = {192,168,5,1},
        .subnet = {255,255,255,0},
        .dns = {192,168,5,1},
    },
    .static_gpios = {{27, true}},
    .static_gpio_count = 1,
    // Both fitted modules are 915 MHz. RX only: explicit host SET_CONFIG
    // remains mandatory before any TX, including after resume/recovery.
    .boot_rx_frequency_hz = 915000000,
};

inline const BoardConfig ETHERMESH_DUO_RF2 = []() {
    BoardConfig radio = ETHERMESH_DUO_RF1;
    radio.pin_lora_nss = 15;
    radio.pin_lora_rst = 14;
    radio.pin_lora_busy = 6;
    radio.pin_lora_dio1 = 5;
    radio.pin_lora_sck = 18;
    radio.pin_lora_miso = 16;
    radio.pin_lora_mosi = 17;
    radio.static_gpios[0] = {19, true};
    return radio;
}();

inline const RadioDescriptor ETHERMESH_DUO_RADIOS[] = {
    {"RF1", &ETHERMESH_DUO_RF1, 5055, 0},  // P4 FSPI / SPI2
    {"RF2", &ETHERMESH_DUO_RF2, 5056, 1},  // P4 HSPI / SPI3
};

inline const BoardConfig BOARD = []() {
    BoardConfig device = ETHERMESH_DUO_RF1;
    device.radios = ETHERMESH_DUO_RADIOS;
    device.radio_count = sizeof(ETHERMESH_DUO_RADIOS) / sizeof(ETHERMESH_DUO_RADIOS[0]);
    device.supports_radio_selection = true;
    device.radio_default_enabled_mask = 1; // RF1-only is a normal fitted configuration.
    return device;
}();
