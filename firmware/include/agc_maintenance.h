#pragma once

#include <stddef.h>
#include <stdint.h>

namespace AgcMaintenance {

// Strict HTTP form value parsing; do not accept toInt()'s partial matches.
inline bool parseIntervalSeconds(const char* text, size_t length, uint16_t& value) {
    if (text == nullptr || length == 0 || length > 4) return false;
    uint16_t parsed = 0;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        parsed = parsed * 10 + (text[i] - '0');
    }
    if (parsed > 3600) return false;
    value = parsed;
    return true;
}

constexpr uint32_t RECENT_PACKET_GUARD_MS = 500;
constexpr uint32_t STATION_POST_TX_QUIET_MS = 10000;

struct Schedule {
    uint32_t lastAttemptMs = 0;
    uint16_t intervalSec = UINT16_MAX;

    void recordAttempt(uint32_t nowMs) { lastAttemptMs = nowMs; }
};

struct Conditions {
    bool radioReady = false;
    bool intentionalStandby = false;
    bool txActive = false;
    bool dio1Pending = false;
    uint16_t intervalSec = 0;
    uint32_t nowMs = 0;
    uint32_t lastPacketMs = 0;
    uint32_t lastTxCompleteMs = 0;
    uint32_t postTxQuietMs = 0;
};

// The receiver callback is evaluated last because checking SX126x IRQ state
// requires an SPI transaction. Earlier, cheaper deferrals should avoid it.
template <typename IsReceivingFn>
bool shouldAttempt(Schedule& schedule, const Conditions& conditions,
                   IsReceivingFn isReceiving) {
    if (!conditions.radioReady || conditions.intentionalStandby ||
        conditions.txActive) return false;

    // Apply interval changes immediately, but start a fresh interval instead
    // of firing an overdue attempt as soon as maintenance is enabled/shortened.
    if (conditions.intervalSec != schedule.intervalSec) {
        schedule.intervalSec = conditions.intervalSec;
        schedule.lastAttemptMs = conditions.nowMs;
        return false;
    }
    if (conditions.intervalSec == 0) return false;

    const uint32_t intervalMs = (uint32_t)conditions.intervalSec * 1000U;
    if ((uint32_t)(conditions.nowMs - schedule.lastAttemptMs) < intervalMs) {
        return false;
    }
    if ((uint32_t)(conditions.nowMs - conditions.lastPacketMs) <
        RECENT_PACKET_GUARD_MS) return false;
    if (conditions.dio1Pending) return false;
    if (conditions.lastTxCompleteMs != 0 && conditions.postTxQuietMs != 0 &&
        (uint32_t)(conditions.nowMs - conditions.lastTxCompleteMs) <
            conditions.postTxQuietMs) return false;
    return !isReceiving();
}

struct RunResult {
    int16_t standbyState = 0;
    int16_t resetState = 0;
    bool resetAttempted = false;
    bool rxRestarted = false;

    bool succeeded(int16_t successState) const {
        return standbyState == successState && resetAttempted &&
               resetState == successState && rxRestarted;
    }
};

// Put the board front end and radio in standby before resetAGC() sends its
// warm-sleep command. RX restart is unconditional so every failure path makes
// a best-effort attempt to restore the radio and external LNA state.
template <typename PrepareStandbyFn, typename StandbyFn,
          typename ResetFn, typename RestartRxFn>
RunResult run(int16_t successState, PrepareStandbyFn prepareStandby,
              StandbyFn standby, ResetFn resetAgc, RestartRxFn restartRx) {
    RunResult result;
    result.resetState = successState;
    prepareStandby();
    result.standbyState = standby();
    if (result.standbyState == successState) {
        result.resetAttempted = true;
        result.resetState = resetAgc();
    }
    result.rxRestarted = restartRx();
    return result;
}

}  // namespace AgcMaintenance
