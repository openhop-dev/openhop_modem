#include "radio_settings.h"
#include "radio_layout.h"
#ifdef ARDUINO_ARCH_ESP32
#include <Preferences.h>
#endif
namespace RadioSettings {
bool supported() {
#if defined(ARDUINO_ARCH_ESP32)
    return BOARD.supports_radio_selection && RadioLayout::valid() && RadioLayout::count() > 1;
#else
    return false;
#endif
}
uint8_t declaredMask() {
    if (!RadioLayout::valid()) return 0;
    uint8_t mask = 0;
    for (size_t i=0; i<RadioLayout::count(); ++i)
        if (RadioLayout::at(i).hardware->has_lora_radio) mask |= uint8_t(1U << i);
    return mask;
}
static uint8_t defaultMask() {
    return supported() ? uint8_t(declaredMask() & BOARD.radio_default_enabled_mask) : declaredMask();
}
static uint8_t active = defaultMask();
void load() {
    active = defaultMask();
#ifdef ARDUINO_ARCH_ESP32
    if (!supported()) return;
    Preferences prefs;
    if (!prefs.begin("oh-radios", true)) return;
    const uint8_t stored = prefs.getUChar("enabled", 0xff);
    prefs.end();
    // 0 is an explicit valid selection. Corrupt/foreign bits are not owners.
    if ((stored & uint8_t(~declaredMask())) == 0) active = stored;
#endif
}
uint8_t activeMask() { return active; }
bool save(uint8_t mask) {
    if (!supported() || (mask & uint8_t(~declaredMask()))) return false;
#ifdef ARDUINO_ARCH_ESP32
    Preferences prefs;
    if (!prefs.begin("oh-radios", false)) return false;
    const bool ok = prefs.putUChar("enabled", mask) == sizeof(uint8_t) &&
                    prefs.getUChar("enabled", 0xff) == mask;
    prefs.end();
    return ok;
#else
    return false;
#endif
}
bool enabled(size_t slot) { return slot < RadioLayout::count() && slot < RadioLayout::CAPACITY && (active & (1U << slot)); }
}
