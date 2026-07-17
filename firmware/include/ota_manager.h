// =============================================================
// ota_manager.h — network management service.
//
// ESP32 builds expose the management UI, JSON API, ArduinoOTA, and HTTP
// firmware upload. The network-capable nRF52 W5100S build implements the
// same lifecycle interface for a lightweight management UI and status API,
// but intentionally omits network firmware upload and rollback handling.
// =============================================================
#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace OTAManager {

// Start the platform's network management service on port 80.
// HTTP requires Basic auth with user="admin" and a persisted password that
// defaults to "password". ESP32 builds also start ArduinoOTA and /update;
// nRF52 W5100S builds provide management only. Call after the network has IP.
void begin(const String& hostname, const String& token);

// Service pending management and, where supported, OTA transactions.
// Call every loop().
void loop();

// Notify the ESP32 rollback guard that a valid host frame was processed.
// This is a no-op on nRF52. Cheap and safe to invoke on every frame.
void notifyValidFrame();

// Current configured hostname. ESP32 builds also publish it through mDNS;
// the W5100S nRF52 build retains it for status/configuration only.
const char* getHostname();

} // namespace OTAManager
