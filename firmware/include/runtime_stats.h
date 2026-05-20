#pragma once

#include <Arduino.h>
#include "protocol.h"

namespace RuntimeStats {

struct Snapshot {
    StatusResp status;
    RadioConfig radio;
    String firmwareVersion;
    bool radioStandby;
    bool autoCadEnabled;
};

Snapshot capture();

}
