#pragma once

#include <stdint.h>

// Decides when bytes waiting in the USB TX buffer have stalled. The caller
// reports how much TX space is free on each loop; while it is below the
// largest value seen (the idle size), bytes are pending. Once they have been
// pending for stallMs, update() returns true and the caller kicks the FIFO.
// Call prime() while the buffer is known to be empty; otherwise the first
// sample sets the idle size, and bytes already stuck then are never seen.
struct UsbTxStallWatch {
    uint32_t stallMs;
    int idleFree = 0;
    uint32_t pendingSince = 0;
    bool armed = false;

    explicit UsbTxStallWatch(uint32_t stallMs) : stallMs(stallMs) {}

    void prime(int emptyFree) {
        if (emptyFree > idleFree) idleFree = emptyFree;
    }

    bool update(int freeBytes, uint32_t nowMs) {
        if (freeBytes > idleFree) idleFree = freeBytes;
        if (freeBytes >= idleFree) {
            armed = false;
            return false;
        }
        if (!armed) {
            armed = true;
            pendingSince = nowMs;
            return false;
        }
        if ((uint32_t)(nowMs - pendingSince) < stallMs) return false;
        armed = false;
        return true;
    }
};
