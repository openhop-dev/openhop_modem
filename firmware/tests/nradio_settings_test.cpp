#include <Preferences.h>
#include "radio_layout.h"
#include "radio_settings.h"
#include <cassert>
#include <iostream>
int main() {
    assert(RadioLayout::valid() && RadioLayout::count() == NRADIO_COUNT);
    const uint8_t declared = (1U << NRADIO_COUNT) - 1U;
    assert(RadioSettings::declaredMask() == declared);
    RadioSettings::load();
    if (NRADIO_COUNT == 1 || !NRADIO_SELECTABLE) {
        assert(!RadioSettings::supported());
        assert(RadioSettings::activeMask() == declared && !RadioSettings::save(0));
        std::cout << "PASS legacy-safe nonselectable policy N=" << NRADIO_COUNT << "\n";
        return 0;
    }
    assert(RadioSettings::supported() && "non-Duo multi-radio selection must be board policy");
    const uint8_t initial = NRADIO_DEFAULT_MASK & declared;
    assert(RadioSettings::activeMask() == initial && "do not inherit Duo's RF1-only default");
    for (uint8_t mask = 0; mask <= declared; ++mask) {
        const auto before = RadioSettings::activeMask();
        assert(RadioSettings::save(mask));
        assert(RadioSettings::activeMask() == before); // saved, not live teardown
        RadioSettings::load();
        assert(RadioSettings::activeMask() == mask);
        for (size_t i = 0; i < NRADIO_COUNT; ++i) {
            assert(RadioSettings::enabled(i) == bool(mask & (1U << i)));
            assert(RadioLayout::port(i) == 5055 + i); // holes never renumber
        }
    }
    const auto before = RadioSettings::activeMask();
    assert(!RadioSettings::save(0x80) && RadioSettings::activeMask() == before);
    preferenceWrite = false;
    assert(!RadioSettings::save(0) && RadioSettings::activeMask() == before);
    preferenceWrite = true;
    preferences["enabled"] = {'c', 0x80};
    RadioSettings::load(); assert(RadioSettings::activeMask() == initial);
    preferences["enabled"] = {'b', 0};
    RadioSettings::load(); assert(RadioSettings::activeMask() == initial);
    preferenceOpen = false;
    assert(!RadioSettings::save(0));
    RadioSettings::load(); assert(RadioSettings::activeMask() == initial);
    std::cout << "PASS non-Duo policy N=" << NRADIO_COUNT
              << ": every sparse/high/zero mask, checked persistence and stable slots\n";
}
