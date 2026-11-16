#pragma once

#include <stdint.h>

#include "protocol.h"

namespace RxBoostControl {

// What the SX1262 is known to be doing after a boosted-gain change.
struct State {
    bool enabled = false;  // mode in effect; meaningless while fault is set
    bool fault = false;    // gain mode unknown, or RX failed to restart
};

enum class Outcome : uint8_t {
    Applied,   // requested mode written, RX resumed if it was running
    Rejected,  // write refused; previous mode restored, RX resumed
    Fault,     // rollback or RX restart failed; the radio state is unknown
};

struct Result {
    Outcome outcome = Outcome::Applied;
    int16_t standbyState = 0;
    int16_t writeState = 0;
    int16_t rollbackState = 0;
    bool writeAttempted = false;
    bool rollbackAttempted = false;
    bool rxRestartAttempted = false;
    bool rxRestarted = false;
};

// The gain register can only be written from standby, so drop to standby,
// write, and resume RX. RadioLib records the requested mode before writing,
// and resetAGC() re-applies that record, so a failed write is rolled back
// with a second write even when the register may not have changed. RX
// restart is attempted on every path that left a running receiver.
//
// A successful write or rollback leaves the mode known, so it clears an
// earlier fault: a host recovers by sending SET_RX_BOOST again, and the AGC
// reset on non-Station boards re-applies the mode through here too.
template <typename StandbyFn, typename WriteFn, typename RestartRxFn>
Result set(State& state, bool requested, bool resumeRx, int16_t successState,
           StandbyFn standby, WriteFn write, RestartRxFn restartRx) {
    Result result;
    result.standbyState = standby();
    bool modeKnown = !state.fault;
    if (result.standbyState == successState) {
        result.writeAttempted = true;
        result.writeState = write(requested);
        if (result.writeState == successState) {
            state.enabled = requested;
            modeKnown = true;
        } else {
            result.rollbackAttempted = true;
            result.rollbackState = write(state.enabled);
            modeKnown = result.rollbackState == successState;
        }
    }

    bool rxOk = true;
    if (resumeRx) {
        result.rxRestartAttempted = true;
        result.rxRestarted = restartRx();
        rxOk = result.rxRestarted;
    }

    state.fault = !modeKnown || !rxOk;
    if (state.fault) {
        result.outcome = Outcome::Fault;
    } else if (result.writeAttempted && result.writeState == successState) {
        result.outcome = Outcome::Applied;
    } else {
        result.outcome = Outcome::Rejected;
    }
    return result;
}

// Station boards reset AGC with RadioLib's resetAGC(), which re-applies the
// boosted-gain mode RadioLib last recorded. A failed reset may leave that
// write undone, and a failed RX restart leaves the receiver down, so either
// marks a fault. Success does not clear one: after a failed rollback,
// RadioLib's recorded mode is not known to match state.enabled.
inline void noteAgcReset(State& state, bool resetAttempted, bool resetOk,
                         bool rxRestarted) {
    if ((resetAttempted && !resetOk) || !rxRestarted) state.fault = true;
}

// Reply to SET_RX_BOOST / GET_RX_BOOST: an error code, or 0 and the mode.
struct Reply {
    uint8_t error = 0;
    uint8_t enabled = 0;
};

inline Reply replyFor(const State& state, Outcome outcome) {
    Reply reply;
    if (outcome == Outcome::Fault || state.fault) {
        reply.error = ERR_RADIO_FAULT;
    } else if (outcome == Outcome::Rejected) {
        reply.error = ERR_RADIO_INIT;
    } else {
        reply.enabled = state.enabled ? 1 : 0;
    }
    return reply;
}

inline Reply replyForGet(const State& state) {
    return replyFor(state, Outcome::Applied);
}

}  // namespace RxBoostControl
