#pragma once

#include <Arduino.h>

namespace RFFrontEnd {

void begin();
bool hasHeltecV43LnaControl();
bool isFemLnaBypassed();
bool isExternalLnaEnabled();
bool setFemLnaBypassed(bool bypass, bool persist);
uint16_t getAgcResetIntervalSec();
bool setAgcResetIntervalSec(uint16_t intervalSec, bool persist);

}  // namespace RFFrontEnd
