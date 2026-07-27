#include "../include/battery_monitor.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

constexpr BatteryMonitor::AdcConfig rakConfig() {
    return {
        3000,    // ADC reference, millivolts
        12,      // ADC resolution
        173,     // official RAK battery-divider numerator
        100,     // divider denominator
        2500,    // plausible LiPo minimum
        5000,    // plausible LiPo maximum
        5,       // at least five of eight samples must be usable
    };
}

void testKnownRakConversionVector() {
    const auto config = rakConfig();
    assert(BatteryMonitor::convertAdcRawToMillivolts(3324, config) == 4211);
}

void testConversionAcceptsRawEndpointsWithoutOverflow() {
    auto config = rakConfig();
    assert(BatteryMonitor::convertAdcRawToMillivolts(0, config) == 0);
    assert(BatteryMonitor::convertAdcRawToMillivolts(4095, config) == 5188);
    assert(BatteryMonitor::convertAdcRawToMillivolts(4096, config) ==
           BatteryMonitor::MILLIVOLTS_UNAVAILABLE);

    config.referenceMillivolts = std::numeric_limits<uint32_t>::max();
    config.dividerNumerator = std::numeric_limits<uint32_t>::max();
    config.dividerDenominator = 1;
    assert(BatteryMonitor::convertAdcRawToMillivolts(4095, config) ==
           BatteryMonitor::MILLIVOLTS_UNAVAILABLE);
}

void testInvalidConversionConfigurationIsUnavailable() {
    auto config = rakConfig();
    config.referenceMillivolts = 0;
    assert(BatteryMonitor::convertAdcRawToMillivolts(2800, config) ==
           BatteryMonitor::MILLIVOLTS_UNAVAILABLE);

    config = rakConfig();
    config.resolutionBits = 0;
    assert(BatteryMonitor::convertAdcRawToMillivolts(2800, config) ==
           BatteryMonitor::MILLIVOLTS_UNAVAILABLE);

    config = rakConfig();
    config.resolutionBits = 32;
    assert(BatteryMonitor::convertAdcRawToMillivolts(2800, config) ==
           BatteryMonitor::MILLIVOLTS_UNAVAILABLE);

    config = rakConfig();
    config.dividerDenominator = 0;
    assert(BatteryMonitor::convertAdcRawToMillivolts(2800, config) ==
           BatteryMonitor::MILLIVOLTS_UNAVAILABLE);
}

void testFilterAveragesValidSamplesAndRejectsImplausibleValues() {
    const auto config = rakConfig();
    const uint16_t samples[] = {3322, 3324, 3326, 3325, 3323, 0, 4095, 100};
    assert(BatteryMonitor::filterAdcSamplesToMillivolts(samples, 8, config) == 4211);
}

void testFilterRequiresConfiguredValidSampleCount() {
    const auto config = rakConfig();
    const uint16_t tooFewValid[] = {3324, 3324, 3324, 3324, 0, 0, 4095, 4095};
    assert(BatteryMonitor::filterAdcSamplesToMillivolts(tooFewValid, 8, config) ==
           BatteryMonitor::MILLIVOLTS_UNAVAILABLE);
    assert(BatteryMonitor::filterAdcSamplesToMillivolts(nullptr, 0, config) ==
           BatteryMonitor::MILLIVOLTS_UNAVAILABLE);
}

void testPlausibilityBoundariesAreInclusive() {
    auto config = rakConfig();
    config.minimumPlausibleMillivolts = 2499;
    config.maximumPlausibleMillivolts = 5000;
    config.minimumValidSamples = 2;

    const uint16_t accepted[] = {1974, 3946};
    assert(BatteryMonitor::filterAdcSamplesToMillivolts(accepted, 2, config) == 3750);

    const uint16_t rejected[] = {1972, 3948};
    assert(BatteryMonitor::filterAdcSamplesToMillivolts(rejected, 2, config) ==
           BatteryMonitor::MILLIVOLTS_UNAVAILABLE);
}

void testInvalidFilterConfigurationIsUnavailable() {
    auto config = rakConfig();
    config.minimumPlausibleMillivolts = 5001;
    config.maximumPlausibleMillivolts = 5000;
    const uint16_t samples[] = {3324, 3324, 3324, 3324, 3324};
    assert(BatteryMonitor::filterAdcSamplesToMillivolts(samples, 5, config) ==
           BatteryMonitor::MILLIVOLTS_UNAVAILABLE);

    config = rakConfig();
    config.minimumValidSamples = 0;
    assert(BatteryMonitor::filterAdcSamplesToMillivolts(samples, 5, config) ==
           BatteryMonitor::MILLIVOLTS_UNAVAILABLE);
}

}  // namespace

int main() {
    testKnownRakConversionVector();
    testConversionAcceptsRawEndpointsWithoutOverflow();
    testInvalidConversionConfigurationIsUnavailable();
    testFilterAveragesValidSamplesAndRejectsImplausibleValues();
    testFilterRequiresConfiguredValidSampleCount();
    testPlausibilityBoundariesAreInclusive();
    testInvalidFilterConfigurationIsUnavailable();
    std::cout << "battery monitor tests passed\n";
    return 0;
}
