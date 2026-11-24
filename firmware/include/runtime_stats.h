#pragma once

#include <Arduino.h>
#include "protocol.h"
#include "runtime_stats_values.h"
#if defined(BOARD_STATION_G2) || defined(BOARD_STATION_G3)
#include "environment_sensor.h"
#endif

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
    bool stationG3PowerMonitorAvailable;
    bool stationG3PowerValid;
    float stationG3InputVoltageV;
    float stationG3CurrentMa;
    float stationG3PowerW;
    float stationG3MinimumInputVoltageV;
    float stationG3MaximumCurrentMa;
#if defined(BOARD_STATION_G2) || defined(BOARD_STATION_G3)
    uint32_t agcResetCount;
    uint32_t lastAgcResetMsAgo;
    EnvironmentSensor::Snapshot environment;
#endif
    bool hasBoardTemperature;
    float boardTemperatureC;
    bool hasBoardFan;
    bool boardFanEnabled;
};

// Per-radio state is distinct from shared device telemetry. Existing capture()
// remains the primary-radio projection for older management clients.
struct RadioSnapshot {
    uint8_t id = 0;
    const char* label = "";
    uint16_t tcpPort = 0;
    bool present = false;
    bool enabled = true;
    bool ready = false;
    bool txReady = false;
    bool standby = false;
    bool busy = false;
    bool autoCadEnabled = false;
    RadioConfig radio = {};
    StatusResp status = {};
};

Snapshot capture();
size_t radioCount();
bool captureRadio(size_t index, RadioSnapshot& out);

}
