#pragma once

#include <Arduino.h>

namespace GPSManager {

struct SatelliteInView {
    String prn;
    int16_t elevationDegrees = -1;
    int16_t azimuthDegrees = -1;
    float snrDb = -1.0f;
    bool hasSnr = false;
};

struct Snapshot {
    bool enabled = false;
    bool seen = false;
    bool fixValid = false;
    uint8_t fixQuality = 0;
    uint8_t satellitesUsed = 0;
    uint8_t satellitesInViewCount = 0;
    uint8_t satellitesInViewStored = 0;
    SatelliteInView satellitesInView[32];
    float latitude = 0.0f;
    float longitude = 0.0f;
    float altitudeM = 0.0f;
    bool hasAltitude = false;
    float speedKmh = 0.0f;
    bool hasSpeed = false;
    float courseDegrees = 0.0f;
    bool hasCourse = false;
    String utcTime;
    String date;
    String datetimeUtc;
    String lastSentenceType;
    uint32_t lastUpdateMs = 0;
    uint32_t validSentenceCount = 0;
    uint32_t invalidChecksumCount = 0;
    uint32_t rawByteCount = 0;
    uint32_t configCommandCount = 0;
};

void begin();
void loop();
Snapshot snapshot();
String buildJson();

} // namespace GPSManager
