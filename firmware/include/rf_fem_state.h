#pragma once

#include <stdint.h>

#include "protocol.h"

namespace RFFrontEnd {

enum class FemApplyStatus : uint8_t {
    Applied = 0,
    Unsupported = 1,
};

struct FemState {
    uint8_t capability = 0;
    uint8_t value = 0;
};

// Bits in apply that the board cannot control, including reserved bits,
// reject the whole request. Accepted bits replace only those positions.
inline FemApplyStatus applyFemMask(uint8_t capability,
                                   uint8_t current,
                                   uint8_t apply,
                                   uint8_t requested,
                                   uint8_t& next) {
    if ((apply & static_cast<uint8_t>(~capability)) != 0) {
        next = current;
        return FemApplyStatus::Unsupported;
    }
    next = static_cast<uint8_t>((current & static_cast<uint8_t>(~apply)) |
                                (requested & apply));
    next &= capability;
    return FemApplyStatus::Applied;
}

}  // namespace RFFrontEnd
