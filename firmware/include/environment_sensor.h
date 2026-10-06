#pragma once

namespace EnvironmentSensor {

// Cached external environmental measurements, separate from CPU temperature.
// Values are usable only when available is true; sensor is otherwise nullptr.
struct Snapshot {
    const char* sensor = nullptr;
    bool available = false;
    float temperatureC = 0.0f;
    float humidityPct = 0.0f;
    float pressureHpa = 0.0f;
};

// Uses the Wire bus already initialized by OledDisplay::begin().
// Hardware support is currently limited to Station G2/G3; other boards are no-ops.
void begin();
void loop();
Snapshot snapshot();  // Cache only: never performs I2C transactions.

}  // namespace EnvironmentSensor
