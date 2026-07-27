#include "battery_monitor.h"

#include <limits>

#ifdef ARDUINO
#include "board_config.h"

#include <Arduino.h>
#include <Wire.h>
#endif

namespace BatteryMonitor {
namespace {

bool checkedMultiply(uint64_t lhs, uint64_t rhs, uint64_t& result) {
    if (lhs != 0 && rhs > std::numeric_limits<uint64_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

bool validAdcConfig(const AdcConfig& config) {
    return config.referenceMillivolts > 0 &&
           config.resolutionBits > 0 && config.resolutionBits < 32 &&
           config.dividerNumerator > 0 && config.dividerDenominator > 0;
}

bool plausible(uint16_t millivolts, const AdcConfig& config) {
    return config.minimumPlausibleMillivolts <=
               config.maximumPlausibleMillivolts &&
           millivolts >= config.minimumPlausibleMillivolts &&
           millivolts <= config.maximumPlausibleMillivolts;
}

#ifdef ARDUINO
bool readFuelGaugeRegister(uint8_t address, uint8_t reg, uint16_t& value) {
#if defined(ARDUINO_ARCH_ESP32)
    Wire.setTimeOut(50);
#endif
    Wire.beginTransmission(address);
    Wire.write(reg);
    // MAX17048 accepts a STOP here. On ESP32-C6 this avoids a repeated-start
    // recovery path which can wedge when the gauge or pull-ups are absent.
    if (Wire.endTransmission(true) != 0) {
        return false;
    }
    if (Wire.requestFrom(address, static_cast<uint8_t>(2)) != 2) {
        return false;
    }
    value = (static_cast<uint16_t>(Wire.read()) << 8) | Wire.read();
    return true;
}

uint16_t cachedAdcMillivolts = MILLIVOLTS_UNAVAILABLE;
uint16_t adcSamples[32] = {};
uint8_t adcSampleCount = 0;
uint32_t nextAdcSampleAtMs = 0;

bool usesRawAdc(const BatterySenseConfig& config) {
    return config.pin >= 0 && config.adc_reference_mv > 0 &&
           config.adc_resolution_bits > 0 &&
           config.adc_divider_numerator > 0 &&
           config.adc_divider_denominator > 0 && config.sample_count > 0 &&
           config.sample_count <= 32;
}
#endif

}  // namespace

uint16_t convertAdcRawToMillivolts(uint32_t raw, const AdcConfig& config) {
    if (!validAdcConfig(config)) {
        return MILLIVOLTS_UNAVAILABLE;
    }

    const uint64_t adcCounts = uint64_t{1} << config.resolutionBits;
    if (raw >= adcCounts) {
        return MILLIVOLTS_UNAVAILABLE;
    }

    uint64_t numerator = raw;
    if (!checkedMultiply(numerator, config.referenceMillivolts, numerator) ||
        !checkedMultiply(numerator, config.dividerNumerator, numerator)) {
        return MILLIVOLTS_UNAVAILABLE;
    }

    uint64_t denominator = 0;
    if (!checkedMultiply(adcCounts, config.dividerDenominator, denominator) ||
        denominator == 0) {
        return MILLIVOLTS_UNAVAILABLE;
    }

    const uint64_t millivolts = numerator / denominator;
    if (millivolts >= MILLIVOLTS_UNAVAILABLE) {
        return MILLIVOLTS_UNAVAILABLE;
    }
    return static_cast<uint16_t>(millivolts);
}

uint16_t filterAdcSamplesToMillivolts(const uint16_t* samples,
                                      size_t sampleCount,
                                      const AdcConfig& config) {
    if (!samples || sampleCount == 0 || !validAdcConfig(config) ||
        config.minimumValidSamples == 0 ||
        config.minimumPlausibleMillivolts >
            config.maximumPlausibleMillivolts) {
        return MILLIVOLTS_UNAVAILABLE;
    }

    uint64_t rawTotal = 0;
    size_t validCount = 0;
    for (size_t i = 0; i < sampleCount; ++i) {
        const uint16_t millivolts = convertAdcRawToMillivolts(samples[i], config);
        if (millivolts == MILLIVOLTS_UNAVAILABLE ||
            !plausible(millivolts, config)) {
            continue;
        }
        if (rawTotal > std::numeric_limits<uint64_t>::max() - samples[i]) {
            return MILLIVOLTS_UNAVAILABLE;
        }
        rawTotal += samples[i];
        ++validCount;
    }

    if (validCount < config.minimumValidSamples) {
        return MILLIVOLTS_UNAVAILABLE;
    }
    return convertAdcRawToMillivolts(
        static_cast<uint32_t>(rawTotal / validCount), config);
}

void loop(const BatterySenseConfig& config) {
#ifdef ARDUINO
    if (!usesRawAdc(config)) return;
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - nextAdcSampleAtMs) < 0) return;

    if (adcSampleCount == 0) {
        if (config.enable_pin >= 0) {
            pinMode(config.enable_pin, OUTPUT);
            digitalWrite(config.enable_pin,
                         config.enable_active_high ? HIGH : LOW);
        }
        pinMode(config.pin, INPUT);
#if defined(ARDUINO_ARCH_NRF52)
        // Match the authoritative RAK4631 battery example exactly.
        analogReference(AR_INTERNAL_3_0);
#endif
        analogReadResolution(config.adc_resolution_bits);
    }

    adcSamples[adcSampleCount++] = static_cast<uint16_t>(analogRead(config.pin));
    if (adcSampleCount < config.sample_count) {
        nextAdcSampleAtMs = now + 1;
        return;
    }

    const AdcConfig adc = {
        config.adc_reference_mv,
        config.adc_resolution_bits,
        config.adc_divider_numerator,
        config.adc_divider_denominator,
        config.minimum_plausible_mv,
        config.maximum_plausible_mv,
        config.minimum_valid_samples,
    };
    cachedAdcMillivolts = filterAdcSamplesToMillivolts(
        adcSamples, config.sample_count, adc);
    adcSampleCount = 0;
    nextAdcSampleAtMs = now + 5000;
#else
    (void)config;
#endif
}

uint16_t readMilliVolts(const BatterySenseConfig& config) {
#ifdef ARDUINO
    if (config.fuel_gauge_i2c_addr != 0) {
        uint16_t vcell = 0;
        if (!readFuelGaugeRegister(config.fuel_gauge_i2c_addr,
                                   config.fuel_gauge_vcell_reg, vcell)) {
            return MILLIVOLTS_UNAVAILABLE;
        }
        // MAX17048 VCELL is 78.125 uV/LSB: vcell * 5 / 64 mV.
        const uint32_t millivolts = (static_cast<uint32_t>(vcell) * 5U) / 64U;
        return millivolts >= MILLIVOLTS_UNAVAILABLE
                   ? MILLIVOLTS_UNAVAILABLE
                   : static_cast<uint16_t>(millivolts);
    }

    if (config.pin < 0) {
        return MILLIVOLTS_UNAVAILABLE;
    }

    if (usesRawAdc(config)) return cachedAdcMillivolts;

    if (config.enable_pin >= 0) {
        pinMode(config.enable_pin, OUTPUT);
        digitalWrite(config.enable_pin,
                     config.enable_active_high ? HIGH : LOW);
        delay(5);
    }

#if defined(ARDUINO_ARCH_ESP32)
    // Preserve the existing calibrated ESP analogReadMilliVolts path.
    if (config.multiplier <= 0.0f) {
        return MILLIVOLTS_UNAVAILABLE;
    }
    uint32_t totalMillivolts = 0;
    constexpr uint8_t sampleCount = 8;
    for (uint8_t i = 0; i < sampleCount; ++i) {
        totalMillivolts += analogReadMilliVolts(config.pin);
        delay(1);
    }
    const float packMillivolts =
        (totalMillivolts / static_cast<float>(sampleCount)) * config.multiplier;
    if (packMillivolts < 0.0f ||
        packMillivolts >= MILLIVOLTS_UNAVAILABLE) {
        return MILLIVOLTS_UNAVAILABLE;
    }
    return static_cast<uint16_t>(packMillivolts + 0.5f);
#else
    return MILLIVOLTS_UNAVAILABLE;
#endif
#else
    (void)config;
    return MILLIVOLTS_UNAVAILABLE;
#endif
}

bool readChargeRatePctPerHour(const BatterySenseConfig& config,
                              float& pctPerHour) {
#ifdef ARDUINO
    if (config.fuel_gauge_i2c_addr == 0 || config.fuel_gauge_crate_reg == 0) {
        return false;
    }
    uint16_t crate = 0;
    if (!readFuelGaugeRegister(config.fuel_gauge_i2c_addr,
                               config.fuel_gauge_crate_reg, crate)) {
        return false;
    }
    pctPerHour = static_cast<float>(static_cast<int16_t>(crate)) * 0.208f;
    return true;
#else
    (void)config;
    (void)pctPerHour;
    return false;
#endif
}

}  // namespace BatteryMonitor
