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
    bool sx126xRegister08B5Valid;
    uint8_t sx126xRegister08B5;
    bool hasBatteryChargeRatePctPerHour;
    bool batteryChargeRatePctPerHourValid;
    float batteryChargeRatePctPerHour;
    bool stationG3PowerMonitorAvailable;
    bool stationG3PowerValid;
    float stationG3InputVoltageV;
    float stationG3CurrentMa;
    float stationG3PowerW;
    float stationG3MinimumInputVoltageV;
    float stationG3MaximumCurrentMa;
};

Snapshot capture();

}
