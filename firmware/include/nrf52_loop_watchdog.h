#pragma once

#include <stdint.h>

// Loop watchdog on the nRF52 WDT peripheral. The WDT counts the 32.768 kHz
// LFCLK and only a reset stops it. It also survives a soft reset
// (NVIC_SystemReset, DFU entry); the Adafruit nRF52 bootloader feeds it while
// it waits for DFU. Templated on the register block so host tests can pass a
// plain struct instead of NRF_WDT.
namespace Nrf52LoopWatchdog {

constexpr uint32_t LFCLK_HZ = 32768;
constexpr uint32_t RUNSTATUS_RUNNING = 1u << 0;
constexpr uint32_t RREN_RR0 = 1u << 0;
// SLEEP=Run (bit 0): keep counting while the CPU sleeps.
// HALT=Pause (bit 3 clear): pause while a debugger halts the CPU.
constexpr uint32_t CONFIG_SLEEP_RUN_HALT_PAUSE = 1u << 0;
constexpr uint32_t RELOAD_REQUEST = 0x6E524635u;
constexpr int RR_COUNT = 8;

constexpr uint32_t reloadValue(uint32_t timeoutS) {
    return timeoutS * LFCLK_HZ - 1u;
}

// Decode an observed CRV, including one inherited from an earlier image.
// CRV=UINT32_MAX is valid; widen before adding one or multiplying.
constexpr uint32_t timeoutMilliseconds(uint32_t reload) {
    return static_cast<uint32_t>((static_cast<uint64_t>(reload) + 1u) * 1000u / LFCLK_HZ);
}

// Every enabled reload register must be written before the timeout. A WDT
// left running by the previous image may have more than RR[0] enabled.
template <typename Wdt>
void feed(Wdt& wdt) {
    const uint32_t enabled = wdt.RREN;
    for (int i = 0; i < RR_COUNT; i++) {
        if (enabled & (1u << i)) wdt.RR[i] = RELOAD_REQUEST;
    }
}

// CRV, RREN and CONFIG cannot be changed while the WDT runs, so a WDT that
// is already running keeps the previous image's settings and is only fed.
template <typename Wdt>
void start(Wdt& wdt, uint32_t timeoutS) {
    if (wdt.RUNSTATUS & RUNSTATUS_RUNNING) {
        feed(wdt);
        return;
    }
    wdt.CONFIG = CONFIG_SLEEP_RUN_HALT_PAUSE;
    wdt.CRV = reloadValue(timeoutS);
    wdt.RREN = RREN_RR0;
    wdt.TASKS_START = 1;
}

}  // namespace Nrf52LoopWatchdog
