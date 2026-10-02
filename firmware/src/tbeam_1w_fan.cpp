#include "tbeam_1w_fan.h"

#include "board_config.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(BOARD_LILYGO_TBEAM_1W)
#include <Arduino.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace TBeam1WFan {
namespace {
portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;
float cachedTemperature = NAN;
uint32_t cachedAt = 0;
bool cachedFanEnabled = true;
bool shutdown = false;

void task(void*) {
    pinMode(BOARD.thermistor.fan_pin, OUTPUT);
    digitalWrite(BOARD.thermistor.fan_pin, HIGH);

    adc_unit_t unitId;
    adc_channel_t channel;
    esp_err_t error = adc_oneshot_io_to_channel(
        BOARD.thermistor.ntc_pin, &unitId, &channel);
    if (error != ESP_OK || unitId != ADC_UNIT_2) {
        Serial.printf("ERROR: T-Beam 1W NTC pin mapping failed: %d; fan stays ON\n",
                      error);
        vTaskDelete(nullptr);
        return;
    }

    adc_oneshot_unit_handle_t adc = nullptr;
    const adc_oneshot_unit_init_cfg_t unitConfig = {
        .unit_id = unitId,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    error = adc_oneshot_new_unit(&unitConfig, &adc);
    if (error != ESP_OK) {
        Serial.printf("ERROR: T-Beam 1W NTC ADC setup failed: %d; fan stays ON\n",
                      error);
        vTaskDelete(nullptr);
        return;
    }

    const adc_oneshot_chan_cfg_t channelConfig = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    error = adc_oneshot_config_channel(adc, channel, &channelConfig);
    if (error != ESP_OK) {
        Serial.printf("ERROR: T-Beam 1W NTC channel setup failed: %d; fan stays ON\n",
                      error);
        adc_oneshot_del_unit(adc);
        vTaskDelete(nullptr);
        return;
    }

    adc_cali_handle_t calibration = nullptr;
    const adc_cali_curve_fitting_config_t calibrationConfig = {
        .unit_id = unitId,
        .chan = channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    error = adc_cali_create_scheme_curve_fitting(
        &calibrationConfig, &calibration);
    if (error != ESP_OK) {
        Serial.printf("ERROR: T-Beam 1W NTC calibration failed: %d; fan stays ON\n",
                      error);
        adc_oneshot_del_unit(adc);
        vTaskDelete(nullptr);
        return;
    }

    Hysteresis control;
    for (;;) {
        uint32_t sum = 0;
        error = ESP_OK;
        for (unsigned i = 0; i < 8; ++i) {
            int mv = 0;
            error = adc_oneshot_get_calibrated_result(
                adc, calibration, channel, &mv);
            if (error != ESP_OK) break;
            if (mv <= 0) {
                error = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            if (!std::isfinite(ntcTemperatureC(mv))) {
                error = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            sum += mv;
        }
        const float temperature = error == ESP_OK
            ? ntcTemperatureC(sum / 8) : NAN;
        const bool enabled = control.update(temperature);
        portENTER_CRITICAL(&mutex);
        if (shutdown) {
            portEXIT_CRITICAL(&mutex);
            break;
        }
        digitalWrite(BOARD.thermistor.fan_pin, enabled ? HIGH : LOW);
        cachedTemperature = temperature;
        cachedAt = millis();
        cachedFanEnabled = enabled;
        portEXIT_CRITICAL(&mutex);
        if (!std::isfinite(temperature)) {
            Serial.printf("ERROR: T-Beam 1W NTC reading failed: %d; fan ON\n",
                          error);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    adc_cali_delete_scheme_curve_fitting(calibration);
    adc_oneshot_del_unit(adc);
    vTaskDelete(nullptr);
}
}  // namespace

void begin() {
    if (BOARD.thermistor.ntc_pin < 0 || BOARD.thermistor.fan_pin < 0) return;
    pinMode(BOARD.thermistor.fan_pin, OUTPUT);
    digitalWrite(BOARD.thermistor.fan_pin, HIGH);
    cachedFanEnabled = true;
    shutdown = false;
    if (xTaskCreate(task, "tbeam_fan", 4096, nullptr, 1, nullptr) != pdPASS) {
        Serial.println("ERROR: T-Beam 1W fan task could not start; fan stays ON");
    }
}

void powerOff() {
    if (BOARD.thermistor.fan_pin < 0) return;
    portENTER_CRITICAL(&mutex);
    shutdown = true;
    cachedTemperature = NAN;
    cachedAt = 0;
    cachedFanEnabled = false;
    digitalWrite(BOARD.thermistor.fan_pin, LOW);
    portEXIT_CRITICAL(&mutex);
}

float temperatureC() {
    if (BOARD.thermistor.ntc_pin < 0) return NAN;
    portENTER_CRITICAL(&mutex);
    const float value = cachedTemperature;
    const uint32_t at = cachedAt;
    portEXIT_CRITICAL(&mutex);
    return (millis() - at < 3000) ? value : NAN;
}

bool isEnabled() {
    if (BOARD.thermistor.fan_pin < 0) return false;
    portENTER_CRITICAL(&mutex);
    const bool enabled = cachedFanEnabled;
    portEXIT_CRITICAL(&mutex);
    return enabled;
}
}  // namespace TBeam1WFan
#else
namespace TBeam1WFan {
float temperatureC() { return NAN; }
bool isEnabled() { return false; }
void begin() {}
void powerOff() {}
}  // namespace TBeam1WFan
#endif
