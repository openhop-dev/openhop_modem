#pragma once

#include <Arduino.h>

namespace RFFrontEnd {

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
void prepareTransmit();
void prepareReceive();
void prepareStandby();
uint16_t getAgcResetIntervalSec();
bool setAgcResetIntervalSec(uint16_t intervalSec, bool persist);

}  // namespace RFFrontEnd
