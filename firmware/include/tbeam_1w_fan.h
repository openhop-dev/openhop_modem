#pragma once

#include <cmath>
#include <cstdint>

namespace TBeam1WFan {

inline float ntcTemperatureC(uint32_t millivolts) {
    if (millivolts < 100 || millivolts > 3000) return NAN;
    const float ratio = 3300.0f / static_cast<float>(millivolts) - 1.0f;
    return 1.0f / (std::log(ratio) / 3950.0f + 1.0f / 298.15f) - 273.15f;
}

class Hysteresis {
    bool enabled_ = true;

public:
    bool update(float temperatureC) {
        if (!std::isfinite(temperatureC) || temperatureC >= 45.0f) {
            enabled_ = true;
        } else if (temperatureC < 40.0f) {
            enabled_ = false;
        }
        return enabled_;
    }
};

float temperatureC();
bool isEnabled();
void begin();
void powerOff();

}  // namespace TBeam1WFan
