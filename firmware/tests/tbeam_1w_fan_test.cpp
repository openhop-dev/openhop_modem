#include "../include/tbeam_1w_fan.h"

#include <cassert>
#include <cmath>

int main() {
    assert(std::isfinite(TBeam1WFan::ntcTemperatureC(1650)));
    assert(std::isnan(TBeam1WFan::ntcTemperatureC(99)));
    assert(std::isnan(TBeam1WFan::ntcTemperatureC(3001)));

    TBeam1WFan::Hysteresis fan;
    assert(!fan.update(39.0f));
    assert(!fan.update(40.0f));
    assert(!fan.update(44.0f));
    assert(fan.update(45.0f));
    assert(fan.update(46.0f));
    assert(fan.update(NAN));
    assert(fan.update(44.0f));
    assert(!fan.update(39.0f));
    return 0;
}
