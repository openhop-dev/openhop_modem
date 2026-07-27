#pragma once

#include <stdint.h>

namespace RuntimeStatsValues {

constexpr uint16_t BATTERY_MILLIVOLTS_UNAVAILABLE = UINT16_MAX;
constexpr int8_t CPU_TEMPERATURE_UNAVAILABLE_C = INT8_MIN;

inline int8_t cpuTemperatureC(float value) {
    if (value != value) return CPU_TEMPERATURE_UNAVAILABLE_C;
    if (value > static_cast<float>(INT8_MAX)) return INT8_MAX;
    // Reserve INT8_MIN as the protocol's unavailable sentinel.
    if (value < -127.0f) return -127;
    return static_cast<int8_t>(value);
}

}  // namespace RuntimeStatsValues
