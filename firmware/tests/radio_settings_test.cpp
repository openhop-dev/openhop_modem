#include <Preferences.h>
#include "radio_settings.h"
#include <cassert>
#include <iostream>
int main() {
    RadioSettings::load();
    assert(RadioSettings::activeMask()==1);
    assert(RadioSettings::save(0) && "explicit both-off must be persisted");
    assert(RadioSettings::activeMask()==1); // no live teardown
    RadioSettings::load(); assert(RadioSettings::activeMask()==0);
    assert(RadioSettings::save(2)); RadioSettings::load();
    assert(!RadioSettings::enabled(0) && RadioSettings::enabled(1));
    preferenceWrite=false;
    assert(!RadioSettings::save(3) && RadioSettings::activeMask()==2);
    preferenceWrite=true;
    assert(!RadioSettings::save(4) && !RadioSettings::save(255));
    RadioSettings::load(); assert(RadioSettings::activeMask()==2);
    preferences["enabled"]={'c',128}; RadioSettings::load();
    assert(RadioSettings::activeMask()==1 && !RadioSettings::enabled(7));
    preferences["enabled"]={'b',0}; RadioSettings::load();
    assert(RadioSettings::activeMask()==1); // invalid stored type
    preferenceOpen=false;
    assert(!RadioSettings::save(0)); RadioSettings::load();
    assert(RadioSettings::activeMask()==1);
    std::cout << "PASS checked byte persistence, explicit zero, invalid bits/type, open/write failures, immutable active selection\n";
}
