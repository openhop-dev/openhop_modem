/*
  Minimal RAK4631 variant for pyMC_modem.
  Based on RAKWireless/RAK-nRF52-Arduino variants/WisCore_RAK4631_Board.
*/
#ifndef _VARIANT_RAK4631_PYMC_
#define _VARIANT_RAK4631_PYMC_

#define RAK4631
#define VARIANT_MCK (64000000ul)
#define USE_LFXO

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

static const uint8_t WB_IO1 = 17;
static const uint8_t WB_IO2 = 34;
static const uint8_t WB_IO3 = 21;
static const uint8_t WB_IO4 = 4;
static const uint8_t WB_IO5 = 9;
static const uint8_t WB_IO6 = 10;
static const uint8_t WB_SW1 = 33;
static const uint8_t WB_A0 = 5;
static const uint8_t WB_A1 = 31;
static const uint8_t WB_I2C1_SDA = 13;
static const uint8_t WB_I2C1_SCL = 14;
static const uint8_t WB_I2C2_SDA = 24;
static const uint8_t WB_I2C2_SCL = 25;
static const uint8_t WB_SPI_CS = 26;
static const uint8_t WB_SPI_CLK = 3;
static const uint8_t WB_SPI_MISO = 29;
static const uint8_t WB_SPI_MOSI = 30;
#define SX126X_POWER_EN (37)

#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (6)
#define NUM_ANALOG_OUTPUTS (0)

#define PIN_LED1 (35)
#define PIN_LED2 (36)
#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2
#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2
#define LED_STATE_ON 1

#define PIN_A0 (5)
#define PIN_A1 (31)
#define PIN_A2 (28)
#define PIN_A3 (29)
#define PIN_A4 (30)
#define PIN_A5 (31)
#define PIN_A6 (0xff)
#define PIN_A7 (0xff)
static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
static const uint8_t A6 = PIN_A6;
static const uint8_t A7 = PIN_A7;
#define ADC_RESOLUTION 14

#define PIN_AREF (2)
#define PIN_NFC1 (9)
#define PIN_NFC2 (10)
static const uint8_t AREF = PIN_AREF;

#define PIN_SERIAL1_RX (15)
#define PIN_SERIAL1_TX (16)
#define PIN_SERIAL2_RX (19)
#define PIN_SERIAL2_TX (20)

// Global SPI is the WisBlock IO slot bus used by RAK13800/W5100S.
// LoRa uses an explicit SPIClass instance on NRF_SPIM2 in main.cpp.
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO (29)
#define PIN_SPI_MOSI (30)
#define PIN_SPI_SCK (3)
static const uint8_t SS = 26;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

#define WIRE_INTERFACES_COUNT 2
#define PIN_WIRE_SDA (13)
#define PIN_WIRE_SCL (14)
#define PIN_WIRE1_SDA (24)
#define PIN_WIRE1_SCL (25)

// WisMesh Ethernet Gateway uses WB_SPI_* pins for the RAK13800/W5100S.
// Do not declare external QSPI flash here: the old QSPI definitions reused
// the same pins as WB_SPI and can make the BSP initialise the Ethernet bus
// as flash during startup.

#ifdef __cplusplus
}
#endif

#endif
