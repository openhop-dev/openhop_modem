#pragma once

#include <Arduino.h>
#include "protocol.h"
#include "runtime_stats_values.h"

namespace RuntimeStats {

struct Snapshot {
    StatusResp status;
    RadioConfig radio;
    String firmwareVersion;
    bool radioStandby;
    bool autoCadEnabled;
    bool hasBatteryChargeRatePctPerHour;
    bool batteryChargeRatePctPerHourValid;
    float batteryChargeRatePctPerHour;
};

Snapshot capture();

}
