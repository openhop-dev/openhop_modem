#pragma once
#include <stddef.h>
#include <stdint.h>

// Independent from network settings. Active selection changes only at boot.
namespace RadioSettings {
bool supported();
uint8_t declaredMask();
void load();
uint8_t activeMask();
bool enabled(size_t slot);
bool save(uint8_t mask);
}
