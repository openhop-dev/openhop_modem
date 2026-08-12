#pragma once

#include <Arduino.h>
#include "protocol.h"

namespace RuntimeStats {

constexpr uint8_t MAX_VIRTUAL_RADIO_SLOTS = 4;

struct VirtualSlotSnapshot {
    uint8_t slot;
    uint16_t port;
    bool active;
    bool onAir;
    bool standby;
    bool autoCadEnabled;
    bool cadCustom;
    uint8_t cadSymNum;
    uint8_t cadDetPeak;
    uint8_t cadDetMin;
    uint8_t cadExitMode;
    RadioConfig radio;
    String clientIP;
    uint8_t receiveMirroringCount;
    uint8_t receiveMirroring[MAX_VIRTUAL_RADIO_SLOTS];
};

struct Snapshot {
    StatusResp status;
    uint32_t suppressedRxCount;
    RadioConfig radio;
    String firmwareVersion;
    bool radioStandby;
    bool autoCadEnabled;
    bool hasBatteryChargeRatePctPerHour;
    bool batteryChargeRatePctPerHourValid;
    float batteryChargeRatePctPerHour;
    uint8_t virtualSlotCount;
    VirtualSlotSnapshot virtualSlots[MAX_VIRTUAL_RADIO_SLOTS];
};

Snapshot capture();

}
