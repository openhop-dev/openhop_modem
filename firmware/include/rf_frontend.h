#pragma once

#include <Arduino.h>

#include "rf_fem_state.h"

namespace RFFrontEnd {

constexpr uint16_t MAX_AGC_RESET_INTERVAL_SEC = 3600;

void begin();
bool hasPaModeControl();
bool isPaHighPowerEnabled();
bool setPaHighPowerEnabled(bool enabled, bool persist);
bool hasStationG3LnaControl();
bool isStationG3LnaEnabled();
bool setStationG3LnaEnabled(bool enabled, bool persist);
bool setStationG3RfConfig(bool paHighPower, bool lnaEnabled, bool persist);
bool hasHeltecV43LnaControl();
bool isFemLnaBypassed();
bool isExternalLnaEnabled();
bool setFemLnaBypassed(bool bypass, bool persist);
bool hasAgcResetIntervalControl();
FemState getFemState();
bool setFemState(uint8_t apply, uint8_t value, bool persist, FemState& out);
void prepareTransmit();
void prepareReceive();
void prepareStandby();
uint16_t getAgcResetIntervalSec();
bool setAgcResetIntervalSec(uint16_t intervalSec, bool persist);

}  // namespace RFFrontEnd
