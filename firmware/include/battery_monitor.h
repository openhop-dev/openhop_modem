#pragma once

#include <stddef.h>
#include <stdint.h>

struct BatterySenseConfig;

namespace BatteryMonitor {

constexpr uint16_t MILLIVOLTS_UNAVAILABLE = UINT16_MAX;

// Integer-only ADC model. The input pin voltage is referenceMillivolts at
// 2^resolutionBits counts; dividerNumerator/dividerDenominator scales that
// pin voltage to pack voltage. Plausibility limits are applied per sample by
// filterAdcSamplesToMillivolts().
struct AdcConfig {
    uint32_t referenceMillivolts;
    uint8_t resolutionBits;
    uint32_t dividerNumerator;
    uint32_t dividerDenominator;
    uint16_t minimumPlausibleMillivolts;
    uint16_t maximumPlausibleMillivolts;
    uint8_t minimumValidSamples;
};

uint16_t convertAdcRawToMillivolts(uint32_t raw, const AdcConfig& config);
uint16_t filterAdcSamplesToMillivolts(const uint16_t* samples,
                                      size_t sampleCount,
                                      const AdcConfig& config);

// Firmware-facing shared battery readers. Host builds return unavailable for
// hardware reads while retaining the pure conversion/filter functions above.
// loop() advances at most one raw-ADC sample and never waits for a full batch.
void loop(const BatterySenseConfig& config);
uint16_t readMilliVolts(const BatterySenseConfig& config);
bool readChargeRatePctPerHour(const BatterySenseConfig& config,
                              float& pctPerHour);

}  // namespace BatteryMonitor
