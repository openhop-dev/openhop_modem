#include "runtime_stats_values.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

void testCpuTemperatureUsesProtocolIntegerUnits() {
    assert(RuntimeStatsValues::cpuTemperatureC(24.9f) == 24);
    assert(RuntimeStatsValues::cpuTemperatureC(-12.9f) == -12);
}

void testCpuTemperatureClampsToProtocolRange() {
    assert(RuntimeStatsValues::cpuTemperatureC(200.0f) == INT8_MAX);
    assert(RuntimeStatsValues::cpuTemperatureC(-200.0f) == -127);
}

void testInvalidCpuTemperatureIsTruthfullyUnavailable() {
    assert(RuntimeStatsValues::cpuTemperatureC(
               std::numeric_limits<float>::quiet_NaN()) ==
           RuntimeStatsValues::CPU_TEMPERATURE_UNAVAILABLE_C);
}

void testBatteryUnavailableUsesProtocolSentinel() {
    assert(RuntimeStatsValues::BATTERY_MILLIVOLTS_UNAVAILABLE == 0xFFFFU);
}

}  // namespace

int main() {
    testCpuTemperatureUsesProtocolIntegerUnits();
    testCpuTemperatureClampsToProtocolRange();
    testInvalidCpuTemperatureIsTruthfullyUnavailable();
    testBatteryUnavailableUsesProtocolSentinel();
    std::cout << "runtime stats value tests passed\n";
    return 0;
}
