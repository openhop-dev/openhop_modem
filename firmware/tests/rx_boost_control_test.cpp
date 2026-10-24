#include "rx_boost_control.h"

#include <cassert>
#include <cstdint>
#include <string>

namespace {

constexpr int16_t OK = 0;
constexpr int16_t FAIL = -2;

// Fake SX1262 with one failure switch per transition. Each call is logged so
// tests can check the order and that nothing runs that should not.
struct FakeRadio {
    int16_t standbyResult = OK;
    int16_t writeResult = OK;     // first write (the requested mode)
    int16_t rollbackResult = OK;  // second write (the previous mode)
    bool rxResult = true;
    bool chipBoosted = false;     // what the register holds
    int writes = 0;
    std::string calls;

    RxBoostControl::Result set(RxBoostControl::State& state, bool requested,
                               bool resumeRx) {
        return RxBoostControl::set(
            state, requested, resumeRx, OK,
            [this]() { calls += "standby "; return standbyResult; },
            [this](bool on) {
                calls += on ? "write(1) " : "write(0) ";
                const int16_t result = writes++ == 0 ? writeResult : rollbackResult;
                if (result == OK) chipBoosted = on;
                return result;
            },
            [this]() { calls += "rx "; return rxResult; });
    }
};

void expectReply(const RxBoostControl::Reply& reply, uint8_t error, uint8_t enabled) {
    assert(reply.error == error);
    assert(reply.enabled == enabled);
}

void testApplied() {
    FakeRadio radio;
    RxBoostControl::State state;
    const auto result = radio.set(state, true, true);
    assert(radio.calls == "standby write(1) rx ");
    assert(result.outcome == RxBoostControl::Outcome::Applied);
    assert(state.enabled && !state.fault && radio.chipBoosted);
    expectReply(RxBoostControl::replyFor(state, result.outcome), 0, 1);
    expectReply(RxBoostControl::replyForGet(state), 0, 1);
}

void testAppliedWhileRadioInStandby() {
    FakeRadio radio;
    RxBoostControl::State state;
    const auto result = radio.set(state, true, false);
    assert(radio.calls == "standby write(1) ");
    assert(result.outcome == RxBoostControl::Outcome::Applied);
    assert(!result.rxRestartAttempted);
}

void testStandbyFailureSkipsWrite() {
    FakeRadio radio;
    radio.standbyResult = FAIL;
    RxBoostControl::State state;
    const auto result = radio.set(state, true, true);
    assert(radio.calls == "standby rx ");
    assert(!result.writeAttempted);
    assert(result.outcome == RxBoostControl::Outcome::Rejected);
    assert(!state.enabled && !state.fault);
    expectReply(RxBoostControl::replyFor(state, result.outcome), ERR_RADIO_INIT, 0);
}

void testStandbyAndRxRestartFailureIsFault() {
    FakeRadio radio;
    radio.standbyResult = FAIL;
    radio.rxResult = false;
    RxBoostControl::State state;
    const auto result = radio.set(state, true, true);
    assert(result.outcome == RxBoostControl::Outcome::Fault);
    assert(state.fault);
    expectReply(RxBoostControl::replyFor(state, result.outcome), ERR_RADIO_FAULT, 0);
}

void testWriteFailureRollsBack() {
    FakeRadio radio;
    radio.writeResult = FAIL;
    RxBoostControl::State state;
    const auto result = radio.set(state, true, true);
    assert(radio.calls == "standby write(1) write(0) rx ");
    assert(result.rollbackAttempted && result.rollbackState == OK);
    assert(result.outcome == RxBoostControl::Outcome::Rejected);
    assert(!state.enabled && !state.fault && !radio.chipBoosted);
    expectReply(RxBoostControl::replyFor(state, result.outcome), ERR_RADIO_INIT, 0);
    expectReply(RxBoostControl::replyForGet(state), 0, 0);
}

void testSameModeWriteFailureStillRollsBack() {
    // RadioLib has already recorded the mode, so rewrite it even when the
    // request matched the cached mode.
    FakeRadio radio;
    radio.writeResult = FAIL;
    RxBoostControl::State state;
    state.enabled = true;
    radio.set(state, true, true);
    assert(radio.calls == "standby write(1) write(1) rx ");
}

void testRollbackFailureIsFault() {
    FakeRadio radio;
    radio.writeResult = FAIL;
    radio.rollbackResult = FAIL;
    RxBoostControl::State state;
    const auto result = radio.set(state, true, true);
    assert(radio.calls == "standby write(1) write(0) rx ");
    assert(result.outcome == RxBoostControl::Outcome::Fault);
    assert(state.fault);
    expectReply(RxBoostControl::replyFor(state, result.outcome), ERR_RADIO_FAULT, 0);
    expectReply(RxBoostControl::replyForGet(state), ERR_RADIO_FAULT, 0);
}

void testRxRestartFailureAfterWriteIsFault() {
    FakeRadio radio;
    radio.rxResult = false;
    RxBoostControl::State state;
    const auto result = radio.set(state, true, true);
    assert(result.outcome == RxBoostControl::Outcome::Fault);
    assert(state.enabled && state.fault);
    expectReply(RxBoostControl::replyFor(state, result.outcome), ERR_RADIO_FAULT, 0);
    expectReply(RxBoostControl::replyForGet(state), ERR_RADIO_FAULT, 0);
}

void testRxRestartFailureAfterRollbackIsFault() {
    FakeRadio radio;
    radio.writeResult = FAIL;
    radio.rxResult = false;
    RxBoostControl::State state;
    const auto result = radio.set(state, true, true);
    assert(result.outcome == RxBoostControl::Outcome::Fault);
    assert(state.fault);
}

void testSuccessfulSetClearsFault() {
    RxBoostControl::State state;
    state.fault = true;
    FakeRadio radio;
    const auto result = radio.set(state, false, true);
    assert(result.outcome == RxBoostControl::Outcome::Applied);
    assert(!state.fault && !state.enabled);
    expectReply(RxBoostControl::replyForGet(state), 0, 0);
}

void testStandbyFailureKeepsFault() {
    // Nothing was written, so a mode that was unknown stays unknown.
    RxBoostControl::State state;
    state.fault = true;
    FakeRadio radio;
    radio.standbyResult = FAIL;
    const auto result = radio.set(state, false, true);
    assert(result.outcome == RxBoostControl::Outcome::Fault);
    assert(state.fault);
}

void testSuccessfulRollbackClearsFault() {
    // The rollback writes a known mode, so the chip state is known again.
    RxBoostControl::State state;
    state.fault = true;
    FakeRadio radio;
    radio.writeResult = FAIL;
    const auto result = radio.set(state, true, true);
    assert(result.outcome == RxBoostControl::Outcome::Rejected);
    assert(!state.fault && !state.enabled);
}

void testAgcResetFailuresRaiseFault() {
    struct Case { bool attempted, resetOk, rxOk, fault; };
    const Case cases[] = {
        {true, true, true, false},
        {false, true, true, false},   // standby failed, RX resumed
        {true, false, true, true},    // resetAGC failed: gain write in doubt
        {true, true, false, true},    // RX did not resume
        {false, true, false, true},
    };
    for (const Case& c : cases) {
        RxBoostControl::State state;
        state.enabled = true;
        RxBoostControl::noteAgcReset(state, c.attempted, c.resetOk, c.rxOk);
        assert(state.fault == c.fault);
        assert(state.enabled);
    }
}

void testAgcResetSuccessKeepsFault() {
    RxBoostControl::State state;
    state.fault = true;
    RxBoostControl::noteAgcReset(state, true, true, true);
    assert(state.fault);
    assert(RxBoostControl::replyForGet(state).error == ERR_RADIO_FAULT);
}

}  // namespace

int main() {
    testApplied();
    testAppliedWhileRadioInStandby();
    testStandbyFailureSkipsWrite();
    testStandbyAndRxRestartFailureIsFault();
    testWriteFailureRollsBack();
    testSameModeWriteFailureStillRollsBack();
    testRollbackFailureIsFault();
    testRxRestartFailureAfterWriteIsFault();
    testRxRestartFailureAfterRollbackIsFault();
    testSuccessfulSetClearsFault();
    testStandbyFailureKeepsFault();
    testSuccessfulRollbackClearsFault();
    testAgcResetFailuresRaiseFault();
    testAgcResetSuccessKeepsFault();
    return 0;
}
