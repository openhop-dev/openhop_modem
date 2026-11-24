#pragma once
#include "frame_parser.h"

// Retain the exact originating session across deferred command work. Generation
// zero never identifies a TCP owner; radio is the index passed to beginRadios.
struct ResponseRoute {
    TransportSource transport;
    uint8_t radio;
    uint32_t generation;
};
using RadioCommandHandler = void (*)(uint8_t, const uint8_t*, uint16_t, const ResponseRoute&);
