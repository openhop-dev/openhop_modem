// =============================================================
// boards/esp32_p4_nano.h — WaveShare ESP32-P4-NANO
//
// Hardware refs:
//   * _incoming/esp32P4Nano/Esp32-p4_datasheet_en.pdf
//   * _incoming/esp32P4Nano/ESP32-P4-NANO-schematic.pdf
//   * _incoming/esp32P4Nano/ESP32-P4-NANO_Demo/ESP-IDF/04_ethernetbasic
//   * _incoming/esp32P4Nano/ESP32-P4-NANO_Demo/ESP-IDF/08_eth2ap
//
// Architecture (different from every other board in this repo):
//   * MCU = ESP32-P4 (RISC-V dual-core HP + LP) — has NO native
//     Wi-Fi or Bluetooth and NO LoRa.
//   * Wi-Fi/BT comes from an on-board ESP32-C6-MINI-1 co-processor
//     bridged over SDIO (esp_hosted firmware on the C6). Arduino's
//     WiFi.h works transparently — same API as on ESP32-S3.
//   * Ethernet 10/100 Mbps via internal EMAC + external IP101GRI PHY
//     on RMII (MDC=GPIO31, MDIO=GPIO52, RST=GPIO51, ADDR=1, ref-clk
//     50 MHz INPUT from PHY). Confirmed against the demo's
//     sdkconfig.defaults in 08_eth2ap.
//   * Debug UART exits through CH343P on a USB-C "UART" port —
//     UART0 is on GPIO37 (TX) / GPIO38 (RX). Native USB-OTG goes
//     to the USB type-A port (host mode), not used by the firmware.
//   * No LoRa attached on day one (`has_lora_radio = false`). When
//     an E22-P868M30S is wired later, flip the flag and uncomment
//     the pin block below — wiring matches §5.1 of the E22 datasheet.
//
// Pin allocation summary:
//   GPIO 7, 8                 — I2C0 (codec, exposed)
//   GPIO 9-13                 — I2S0 (audio codec)
//   GPIO 14-19, 24            — esp_hosted SDIO bridge to ESP32-C6
//   GPIO 28-35, 49-52         — RMII Ethernet (incl. MDC=31, MDIO=52)
//   GPIO 37, 38               — UART0 / CH343P / debug serial
//   GPIO 39-44                — MicroSD slot
//   GPIO 53                   — Audio amp PA_CTRL
//   GPIO 0                    — BOOT button
//   GPIO 20-23, 45-48, 54     — exposed on the P1/P2 headers (free)
// =============================================================
#pragma once

inline const BoardConfig BOARD = {
    .name        = "ESP32-P4-NANO",
    .fw_suffix   = "esp32p4",
    .mdns_prefix = "p4nano",

    // Future-LoRa pins are listed below for documentation, but
    // .has_lora_radio = false makes main.cpp skip every radio path.
    // When the E22-P module is wired (per the connection table in
    // README/INSTALL), flip has_lora_radio to true and these pins
    // become live.
    .pin_lora_nss  = 45,
    .pin_lora_rst  = 20,
    .pin_lora_busy = 23,
    .pin_lora_dio1 = 22,
    .pin_lora_sck  = 46,
    .pin_lora_miso = 47,
    .pin_lora_mosi = 48,

    .rf_switch = {
        .en_pin            = 21,    // RXEN — held HIGH after 5 s LOW boot hold
        .en_low_hold_ms    = 5000,
        .rx_pin            = -1,
        .tx_pin            = -1,
        .dio2_as_rf_switch = true,  // T/R CTRL ↔ DIO2 wired on the carrier
    },

    // External SSD1306 OLED over I2C0 (the same bus the on-board
    // audio codec sits on; both addresses coexist). Pin assignment
    // matches the WaveShare reference: SDA=GPIO7, SCL=GPIO8.
    .pin_i2c_sda      = 7,
    .pin_i2c_scl      = 8,
    .pin_i2c_oled_rst = -1,
    .pin_vext_enable_low = -1,

    // BOOT button (Key1) is wired to GPIO35 on the WaveShare schematic,
    // BUT the same GPIO35 is shared with the IP101GRI PHY's RMII TXD1
    // line (pin 8 of the PHY). Whenever Ethernet is brought up — which
    // we do unconditionally on this board — the EMAC peripheral drives
    // GPIO35 as a high-speed RMII output and the button input is gone.
    // Hardware conflict, no software workaround. We disable the button
    // (pin = -1 → loop() skips polling it). Either accept losing the
    // BOOT-button screen cycle, or disable Ethernet to get it back.
    // Factory-reset-on-hold is also a no-op; users can wipe Wi-Fi via
    // CMD_WIFI_RESET over the protocol or by erasing flash.
    .pin_user_button        = -1,
    .user_button_active_low = true,

    .max_tx_power_dbm = 30,         // E22-P868M30S ceiling (when fitted)

    .use_dio3_tcxo = true,
    .tcxo_voltage  = 1.8f,

    .has_lora_radio = true,         // E22-P868M30S wired

    // Both Wi-Fi and Ethernet are *capability* flags. main.cpp does
    // runtime selection in setup(): Ethernet is tried first, and if a
    // cable is plugged in (link up within ~5 s) Wi-Fi is skipped. With
    // no Ethernet link, EMAC is torn down and Wi-Fi takes over. This
    // sidesteps a hardware conflict on this carrier — running C6 SDIO
    // (Wi-Fi) + RMII EMAC + radio simultaneously couples enough noise
    // into the SDIO clock to crash the bridge every ~25 s. With only
    // one of the two networks live, the chip is stable.
    .has_wifi = true,
    .has_network = true,
    // Protocol on USB-CDC (Serial) — no dedicated UART for the
    // binary protocol. Sector-mode boards override these.
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
        .rmii_clock_input = true,   // 50 MHz reference comes from the IP101GRI
        // DHCP — switch supplies a lease. Static fields kept as
        // placeholders for an offline/no-DHCP fallback later.
        .use_static_ip    = false,
        .static_ip        = {192, 168, 5, 10},
        .gateway          = {192, 168, 5, 1},
        .subnet           = {255, 255, 255, 0},
        .dns              = {192, 168, 5, 1},
    },
};
