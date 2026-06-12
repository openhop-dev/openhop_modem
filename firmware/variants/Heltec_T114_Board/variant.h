// =============================================================
// variants/Heltec_T114_Board/variant.h — Adafruit nRF52 BSP
// pin map for the Heltec Mesh Node T114 (HT-n5262).
//
// Adapted from MeshCore (heltec_t114 variant). Identity pin map
// in variant.cpp lets us use raw nRF52 GPIO numbers everywhere
// (P0.X for X<32, P1.(X-32) for X>=32). The Adafruit Feather
// variant that ships with the BSP only exposes ~35 Arduino pins
// and remaps them, which collides with the T114's wiring — hence
// the dedicated variant.
// =============================================================
#pragma once

#include "WVariant.h"

// 32.768 kHz XTAL on board.
#define USE_LFXO
#define VARIANT_MCK             (64000000ul)

#define WIRE_INTERFACES_COUNT   (2)

// 3.3 V peripheral rail enable (drives the LDO that feeds GPS,
// TFT, etc.). Pulling it high during initVariant() so the TFT
// VDD has time to come up before the SPI traffic starts.
#define PIN_3V3_EN              (38)

// Battery sense (P0.04, A0). Not used by pymc_modem yet but kept
// so analogRead() works if we ever expose it.
#define BATTERY_PIN             (4)
#define ADC_MULTIPLIER          (4.90F)
#define ADC_RESOLUTION          (14)
#define BATTERY_SENSE_RES       (12)
#define AREF_VOLTAGE            (3.0)

// Identity-mapped pin space — every Arduino pin index 2..47 maps
// straight to the same raw nRF GPIO. P0.0 / P0.1 (XTAL) sit at
// indices 0 / 1 and are unmapped (0xFF in variant.cpp).
#define PINS_COUNT              (48)
#define NUM_DIGITAL_PINS        (48)
#define NUM_ANALOG_INPUTS       (1)
#define NUM_ANALOG_OUTPUTS      (0)

// UART1 = primary debug UART (Serial). MeshCore wires the GPS to
// these — when no GPS is attached they're free for general use.
#define PIN_SERIAL1_RX          (37)
#define PIN_SERIAL1_TX          (39)

// UART2 (Serial2) = the protocol header pins silked 0.09/0.10.
// pymc_modem uses these for the host-facing UART when the board
// is wired to a sector-array controller instead of USB.
#define PIN_SERIAL2_RX          (9)
#define PIN_SERIAL2_TX          (10)

#define PIN_WIRE_SDA            (26) // P0.26
#define PIN_WIRE_SCL            (27) // P0.27
#define PIN_WIRE1_SDA           (7)  // P0.07
#define PIN_WIRE1_SCL           (8)  // P0.08

// Default SPI bus = SX1262 (and the optional ST7789 TFT, which
// shares the same lines and is selected via PIN_TFT_CS).
#define SPI_INTERFACES_COUNT    (2)
#define PIN_SPI_MISO            (23)
#define PIN_SPI_MOSI            (22)
#define PIN_SPI_SCK             (19)
#define PIN_SPI_NSS             (24)

// Secondary SPI bus reserved by Heltec for an optional dedicated
// TFT wiring (V2 silk PIN_TFT_SCL/SDA). Not used by this firmware
// but defined so SPI1.begin() works if a future feature needs it.
#define PIN_SPI1_MISO           (43)
#define PIN_SPI1_MOSI           (41)
#define PIN_SPI1_SCK            (40)

// LED + button (active low).
#define LED_BUILTIN             (35)
#define PIN_LED                 LED_BUILTIN
#define LED_RED                 LED_BUILTIN
#define LED_BLUE                (-1)
#define LED_PIN                 LED_BUILTIN
#define LED_STATE_ON            LOW

#define PIN_NEOPIXEL            (14)
#define NEOPIXEL_NUM            (2)

#define PIN_BUTTON1             (42)
#define BUTTON_PIN              PIN_BUTTON1
#define PIN_USER_BTN            BUTTON_PIN

// External QSPI flash (MX25R1635F on T114) — required by the
// Adafruit BSP's Adafruit_LittleFS init even if we never write
// to it from pymc_modem.
#define EXTERNAL_FLASH_DEVICES  MX25R1635F
#define EXTERNAL_FLASH_USE_QSPI

// SX1262 control pins (separate from the SPI lines above).
#define USE_SX1262
#define LORA_CS                 (24)
#define SX126X_DIO1             (20)
#define SX126X_BUSY             (17)
#define SX126X_RESET            (25)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// ST7789 1.14" TFT (LH114T-IF03). On T114 V2 the panel rides
// the same SPI bus as the SX1262 — only CS / DC / RST / BL are
// dedicated. PIN_TFT_VDD_CTL gates the panel's logic rail and
// must be driven HIGH before any SPI traffic to the controller.
#define PIN_TFT_RST             (2)   // P0.02
#define PIN_TFT_VDD_CTL         (3)   // P0.03 — TFT logic VDD enable
#define PIN_TFT_CS              (11)  // P0.11
#define PIN_TFT_DC              (12)  // P0.12
#define PIN_TFT_LEDA_CTL        (15)  // P0.15 — backlight LED anode
