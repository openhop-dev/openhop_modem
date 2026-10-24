#include "usb_tx_stall_watch.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>

namespace {

constexpr uint32_t STALL_MS = 20;
constexpr int IDLE = 256;
constexpr int PENDING = 200;

UsbTxStallWatch idleWatch(uint32_t nowMs) {
    UsbTxStallWatch watch(STALL_MS);
    assert(!watch.update(IDLE, nowMs));
    return watch;
}

void testSameMillisecondDoesNotKick() {
    // Even and odd timestamps: the old `now | 1` stored a future time for
    // even ones, so a second check in the same millisecond wrapped and fired.
    for (uint32_t now : {1000u, 1001u}) {
        UsbTxStallWatch watch = idleWatch(now);
        assert(!watch.update(PENDING, now));
        assert(!watch.update(PENDING, now));
        assert(!watch.update(PENDING, now));
    }
}

void testKicksAtBoundary() {
    UsbTxStallWatch watch = idleWatch(5000);
    assert(!watch.update(PENDING, 5000));
    assert(!watch.update(PENDING, 5000 + STALL_MS - 1));
    assert(watch.update(PENDING, 5000 + STALL_MS));
}

void testStartsAtZero() {
    // millis() == 0 is a valid start time, not "timer off".
    UsbTxStallWatch watch = idleWatch(0);
    assert(!watch.update(PENDING, 0));
    assert(!watch.update(PENDING, STALL_MS - 1));
    assert(watch.update(PENDING, STALL_MS));
}

void testMillisWraparound() {
    const uint32_t start = UINT32_MAX - 5;
    UsbTxStallWatch watch = idleWatch(start);
    assert(!watch.update(PENDING, start));
    assert(!watch.update(PENDING, start + 10));  // wrapped to 4
    assert(!watch.update(PENDING, start + STALL_MS - 1));
    assert(watch.update(PENDING, start + STALL_MS));
}

void testDrainResetsTimer() {
    UsbTxStallWatch watch = idleWatch(100);
    assert(!watch.update(PENDING, 100));
    assert(!watch.update(IDLE, 110));  // drained before the timeout
    assert(!watch.update(PENDING, 115));
    assert(!watch.update(PENDING, 115 + STALL_MS - 1));
    assert(watch.update(PENDING, 115 + STALL_MS));
}

void testRearmsAfterKick() {
    UsbTxStallWatch watch = idleWatch(0);
    assert(!watch.update(PENDING, 0));
    assert(watch.update(PENDING, STALL_MS));
    // Still pending after the kick: wait a full interval before the next one.
    assert(!watch.update(PENDING, STALL_MS));
    assert(!watch.update(PENDING, 2 * STALL_MS - 1));
    assert(watch.update(PENDING, 2 * STALL_MS));
}

void testPrimedWatchSeesBytesStuckAtFirstCheck() {
    // Boot output can already be stuck when the loop first checks.
    UsbTxStallWatch watch(STALL_MS);
    watch.prime(IDLE);
    assert(!watch.update(PENDING, 0));
    assert(!watch.update(PENDING, STALL_MS - 1));
    assert(watch.update(PENDING, STALL_MS));
}

void testIdleSizeLearnedFromLargestFree() {
    UsbTxStallWatch watch(STALL_MS);
    // Unprimed, the first observation becomes the idle size until a larger
    // free count shows the real one.
    assert(!watch.update(PENDING, 0));
    assert(!watch.update(PENDING, STALL_MS));
    assert(!watch.update(IDLE, STALL_MS));
    assert(!watch.update(PENDING, 50));
    assert(watch.update(PENDING, 50 + STALL_MS));
}

}  // namespace

int main() {
    testSameMillisecondDoesNotKick();
    testKicksAtBoundary();
    testStartsAtZero();
    testMillisWraparound();
    testDrainResetsTimer();
    testRearmsAfterKick();
    testPrimedWatchSeesBytesStuckAtFirstCheck();
    testIdleSizeLearnedFromLargestFree();
    return 0;
}
