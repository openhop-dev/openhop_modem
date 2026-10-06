#include "environment_sensor.h"

#if (defined(BOARD_STATION_G2) || defined(BOARD_STATION_G3)) && defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <Wire.h>
#include <bme280.h>
#include <cmath>

namespace EnvironmentSensor {
namespace {

constexpr uint32_t SAMPLE_INTERVAL_MS = 5000;
constexpr uint32_t REPROBE_INTERVAL_MS = 30000;
constexpr uint32_t MAX_SAMPLE_AGE_MS = 15000;
constexpr uint32_t CONVERSION_TIMEOUT_MS = 100;
Snapshot current;
bme280_dev device = {};
uint8_t address = 0;
bool detected = false;
bool measuring = false;
uint32_t lastProbeMs = 0;
uint32_t lastStartMs = 0;
uint32_t lastSampleMs = 0;
uint32_t conversionMs = 0;

BME280_INTF_RET_TYPE readRegisters(uint8_t reg, uint8_t* data,
                                  uint32_t length, void*) {
    Wire.beginTransmission(address);
    const bool queued = Wire.write(reg) == 1;
    const uint8_t result = Wire.endTransmission(true);
    if (!queued || result != 0) return -1;
    if (Wire.requestFrom(address, static_cast<uint8_t>(length)) != length) return -1;
    for (uint32_t i = 0; i < length; ++i) data[i] = Wire.read();

    // Bosch compensation clamps values to physical limits. Reject the raw
    // "measurement disabled/not yet taken" sentinels before compensation.
    if (reg == BME280_REG_DATA && length == BME280_LEN_P_T_H_DATA) {
        const uint32_t pressure = (uint32_t(data[0]) << 12) |
                                  (uint32_t(data[1]) << 4) | (data[2] >> 4);
        const uint32_t temperature = (uint32_t(data[3]) << 12) |
                                     (uint32_t(data[4]) << 4) | (data[5] >> 4);
        const uint16_t humidity = (uint16_t(data[6]) << 8) | data[7];
        if (pressure == 0x80000 || temperature == 0x80000 || humidity == 0x8000) return -1;
    }
    return BME280_INTF_RET_SUCCESS;
}

BME280_INTF_RET_TYPE writeRegisters(uint8_t reg, const uint8_t* data,
                                   uint32_t length, void*) {
    Wire.beginTransmission(address);
    const bool registerQueued = Wire.write(reg) == 1;
    const bool dataQueued = Wire.write(data, length) == length;
    const uint8_t result = Wire.endTransmission(true);
    return registerQueued && dataQueued && result == 0 ? BME280_INTF_RET_SUCCESS : -1;
}

void delayUs(uint32_t period, void*) {
    // Only the driver's bounded initialization/reset waits use this callback.
    delayMicroseconds(period);
}

void offline() {
    current = {};
    detected = false;
    measuring = false;
    lastProbeMs = millis();
}

void probe() {
    offline();
    device = {};
    device.intf = BME280_I2C_INTF;
    device.read = readRegisters;
    device.write = writeRegisters;
    device.delay_us = delayUs;
    // Bosch requires a non-null interface context, even though there is only
    // one sensor on this bus and the callbacks use the address above.
    device.intf_ptr = &address;

    bme280_settings settings = {};
    settings.osr_t = BME280_OVERSAMPLING_1X;
    settings.osr_p = BME280_OVERSAMPLING_1X;
    settings.osr_h = BME280_OVERSAMPLING_1X;
    uint32_t conversionUs = 0;
    bme280_cal_meas_delay(&conversionUs, &settings);
    conversionMs = (conversionUs + 999) / 1000;

    // Grove defaults to 0x76; its address pads can select 0x77. init() checks
    // chip ID 0x60 before reset/configuration, so BMP280/BME680 are not claimed.
    const uint8_t addresses[] = {0x76, 0x77};
    for (uint8_t candidate : addresses) {
        address = candidate;
        if (bme280_init(&device) != BME280_OK) continue;
        if (device.calib_data.dig_t1 == 0 || device.calib_data.dig_t1 == 0xFFFF ||
            device.calib_data.dig_p1 == 0 || device.calib_data.dig_p1 == 0xFFFF) continue;
        // v3.5.1 can overwrite a humidity-setting error with the result of
        // setting pressure/temperature. Keep these calls separate so every
        // failed transaction prevents publishing measurements.
        if (bme280_set_sensor_settings(BME280_SEL_OSR_HUM, &settings, &device) != BME280_OK ||
            bme280_set_sensor_settings(BME280_SEL_OSR_TEMP | BME280_SEL_OSR_PRESS,
                                       &settings, &device) != BME280_OK) continue;
        detected = true;
        lastStartMs = millis() - SAMPLE_INTERVAL_MS;
        Serial.printf("[ENV] BME280 detected at 0x%02X\n", address);
        return;
    }
}

}  // namespace

void begin() {
    // Do not reinitialize Wire or change its pins/clock: OLED owns bus startup.
    Wire.setTimeOut(50);
    probe();
    if (!detected) Serial.println("[ENV] No BME280 detected; modem startup continues");
}

void loop() {
    const uint32_t now = millis();
    if (!detected) {
        if (uint32_t(now - lastProbeMs) >= REPROBE_INTERVAL_MS) probe();
        return;
    }
    if (!measuring) {
        if (uint32_t(now - lastStartMs) < SAMPLE_INTERVAL_MS) return;
        if (bme280_set_sensor_mode(BME280_POWERMODE_FORCED, &device) != BME280_OK) {
            offline();
            return;
        }
        lastStartMs = millis();
        measuring = true;
        return;
    }
    if (uint32_t(now - lastStartMs) < conversionMs) return;

    uint8_t status = 0;
    if (bme280_get_regs(BME280_REG_STATUS, &status, 1, &device) != BME280_OK ||
        (status & BME280_STATUS_IM_UPDATE)) {
        offline();
        return;
    }
    if (status & BME280_STATUS_MEAS_DONE) {  // Bit is set while conversion is busy.
        if (uint32_t(now - lastStartMs) >= CONVERSION_TIMEOUT_MS) offline();
        return;
    }
    bme280_data data = {};
    if (bme280_get_sensor_data(BME280_ALL, &data, &device) != BME280_OK ||
        !std::isfinite(data.temperature) || !std::isfinite(data.humidity) ||
        !std::isfinite(data.pressure) || data.temperature < -40.0 || data.temperature > 85.0 ||
        data.humidity < 0.0 || data.humidity > 100.0 ||
        data.pressure < 30000.0 || data.pressure > 110000.0) {
        offline();
        return;
    }
    current.sensor = "bme280";
    current.available = true;
    current.temperatureC = data.temperature;
    current.humidityPct = data.humidity;
    current.pressureHpa = data.pressure / 100.0;  // Bosch returns Pa.
    // Timestamp the conversion, not the read: a delayed loop must not make an
    // old measurement appear fresh simply by copying it into the cache later.
    lastSampleMs = lastStartMs;
    measuring = false;
}

Snapshot snapshot() {
    if (!current.available || uint32_t(millis() - lastSampleMs) >= MAX_SAMPLE_AGE_MS) return {};
    return current;
}

}  // namespace EnvironmentSensor
#else
namespace EnvironmentSensor {
void begin() {}
void loop() {}
Snapshot snapshot() { return {}; }
}  // namespace EnvironmentSensor
#endif
