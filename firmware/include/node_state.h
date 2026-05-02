// =============================================================
// node_state.h — non-volatile per-node state on T114 (nRF52840).
//
// Persists between reboots so the modem boots into the same state
// the controller last assigned, even if the controller takes
// seconds to come up and re-push.
//
// Two fields:
//   - displayName: ASCII name shown big on the TFT
//   - radioStandby: hard standby flag (skip startReceive on boot)
//
// Backed by Adafruit_LittleFS on the internal flash. The controller
// still pushes both fields on boot / OFFLINE→ONLINE — this just
// gives the modem something sensible to show during the gap.
// =============================================================
#pragma once

#if defined(BOARD_HELTEC_T114)

#include <stdint.h>
#include <stddef.h>

namespace NodeState {

void begin();                                   // load from LittleFS at boot

bool        getStandby();
const char* getDisplayName();                   // returns "" if never set

void setDisplayName(const char* name);          // saves to LittleFS
void setStandby(bool on);                       // saves to LittleFS

}   // namespace NodeState

#endif   // BOARD_HELTEC_T114
