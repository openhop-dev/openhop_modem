#include "nrf52_loop_watchdog.h"

#include <cassert>
#include <cstdint>

using namespace Nrf52LoopWatchdog;

namespace {

// Same field names as NRF_WDT_Type. RREN resets to RR[0] enabled.
struct FakeWdt {
    uint32_t TASKS_START = 0;
    uint32_t RUNSTATUS = 0;
    uint32_t CRV = 0xFFFFFFFFu;
    uint32_t RREN = RREN_RR0;
    uint32_t CONFIG = 0x1u;
    uint32_t RR[RR_COUNT] = {};
};

void testReloadValueCountsLfclkTicks() {
    static_assert(reloadValue(1) == 32767u, "1 s is 32768 ticks");
    static_assert(reloadValue(30) == 983039u, "30 s is 983040 ticks");
}

void testObservedReloadConvertsToMillisecondsWithoutOverflow() {
    static_assert(timeoutMilliseconds(32767u) == 1000u, "one second");
    static_assert(timeoutMilliseconds(327679u) == 10000u, "inherited ten seconds");
    static_assert(timeoutMilliseconds(983039u) == 30000u, "default thirty seconds");
    static_assert(timeoutMilliseconds(0u) == 0u, "sub-millisecond count is rounded down");
    static_assert(timeoutMilliseconds(UINT32_MAX) == 131072000u, "maximum CRV must not wrap");
}

void testStartConfiguresAndStarts() {
    FakeWdt wdt;
    start(wdt, 30);
    assert(wdt.CONFIG == CONFIG_SLEEP_RUN_HALT_PAUSE);
    assert(wdt.CRV == reloadValue(30));
    assert(wdt.RREN == RREN_RR0);
    assert(wdt.TASKS_START == 1);
}

void testStartLeavesRunningWdtSettings() {
    // Left running by the previous image across a soft reset.
    FakeWdt wdt;
    wdt.RUNSTATUS = RUNSTATUS_RUNNING;
    wdt.CRV = reloadValue(10);
    wdt.RREN = RREN_RR0 | (1u << 3);
    start(wdt, 30);
    assert(wdt.TASKS_START == 0);
    assert(wdt.CRV == reloadValue(10));
    assert(wdt.RR[0] == RELOAD_REQUEST);
    assert(wdt.RR[3] == RELOAD_REQUEST);
}

void testFeedWritesOnlyEnabledRegisters() {
    FakeWdt wdt;
    wdt.RREN = (1u << 0) | (1u << 2) | (1u << 7);
    feed(wdt);
    for (int i = 0; i < RR_COUNT; i++) {
        const bool enabled = wdt.RREN & (1u << i);
        assert(wdt.RR[i] == (enabled ? RELOAD_REQUEST : 0u));
    }
}

}  // namespace

int main() {
    testReloadValueCountsLfclkTicks();
    testObservedReloadConvertsToMillisecondsWithoutOverflow();
    testStartConfiguresAndStarts();
    testStartLeavesRunningWdtSettings();
    testFeedWritesOnlyEnabledRegisters();
    return 0;
}
