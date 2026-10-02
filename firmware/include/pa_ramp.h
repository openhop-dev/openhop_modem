#pragma once

#include <stdint.h>

template <typename Radio>
int16_t applyOutputPowerAndRamp(Radio& radio, int8_t power, uint8_t rampSetting) {
    const int16_t powerState = radio.setOutputPower(power);
    if (powerState != 0 || rampSetting == 0) return powerState;
    return radio.setPaRampTime(rampSetting);
}
