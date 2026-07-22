// =============================================================
// nrf_settings.h — persistent management settings for nRF52
// Ethernet targets.
// =============================================================
#pragma once

#include <Arduino.h>
#include "wifi_manager.h"

namespace NrfSettings {

// Load the on-flash record once. Invalid or missing records fall back to
// compile-time defaults without formatting the filesystem again.
void begin();

// Populate the shared network config used by main.cpp and the W5100S stack.
void loadNetworkConfig(WifiManager::Config& config);

// Persist the complete network/TCP configuration as one CRC-protected record.
bool saveNetworkConfig(const WifiManager::Config& config);

// HTTP Basic Auth password for the nRF52 management page.
const String& getHttpPassword();
bool saveHttpPassword(const String& password);

// Remove the stored record. Defaults are restored on the next begin()/boot.
bool clear();

}  // namespace NrfSettings
