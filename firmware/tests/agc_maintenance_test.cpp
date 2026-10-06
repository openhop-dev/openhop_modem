#include "agc_maintenance.h"

#include <cassert>
#include <cstdint>
#include <string>

namespace {

constexpr int16_t OK = 0;
constexpr int16_t FAILURE = -1;

void testFormIntervalParsing() {
    uint16_t value = 123;
    for (const auto& entry : {"0", "4", "3600", "0004"}) {
        assert(AgcMaintenance::parseIntervalSeconds(entry, std::string(entry).size(), value));
    }
    assert(value == 4);
    for (const auto& entry : {"", "-1", "4junk", "3.5", "3601", "65536", " 4", "4 "}) {
        assert(!AgcMaintenance::parseIntervalSeconds(entry, std::string(entry).size(), value));
        assert(value == 4);
    }
    assert(!AgcMaintenance::parseIntervalSeconds(nullptr, 1, value));
}

void testSuccessfulOrdering() {
    std::string calls;
    const auto result = AgcMaintenance::run(
        OK,
        [&calls]() { calls += "prepare "; },
        [&calls]() { calls += "standby "; return OK; },
        [&calls]() { calls += "reset "; return OK; },
        [&calls]() { calls += "rx"; return true; });

    assert(calls == "prepare standby reset rx");
    assert(result.succeeded(OK));
}

void testStandbyFailureSkipsResetAndRecoversRx() {
    std::string calls;
    const auto result = AgcMaintenance::run(
        OK,
        [&calls]() { calls += "prepare "; },
        [&calls]() { calls += "standby "; return FAILURE; },
        [&calls]() { calls += "reset "; return OK; },
        [&calls]() { calls += "rx"; return true; });

    assert(calls == "prepare standby rx");
    assert(!result.resetAttempted);
    assert(result.rxRestarted);
    assert(!result.succeeded(OK));
}

void testResetFailureRecoversRx() {
    std::string calls;
    const auto result = AgcMaintenance::run(
        OK,
        [&calls]() { calls += "prepare "; },
        [&calls]() { calls += "standby "; return OK; },
        [&calls]() { calls += "reset "; return FAILURE; },
        [&calls]() { calls += "rx"; return true; });

    assert(calls == "prepare standby reset rx");
    assert(result.resetAttempted);
    assert(result.resetState == FAILURE);
    assert(result.rxRestarted);
    assert(!result.succeeded(OK));
}

void testRxFailureDoesNotReportSuccess() {
    const auto result = AgcMaintenance::run(
        OK, []() {}, []() { return OK; }, []() { return OK; },
        []() { return false; });
    assert(!result.succeeded(OK));
}

AgcMaintenance::Conditions idleAt(uint32_t nowMs) {
    AgcMaintenance::Conditions conditions;
    conditions.radioReady = true;
    conditions.intervalSec = 4;
    conditions.nowMs = nowMs;
    conditions.lastPacketMs = nowMs - 1000U;
    conditions.postTxQuietMs = AgcMaintenance::STATION_POST_TX_QUIET_MS;
    return conditions;
}

void initialize(AgcMaintenance::Schedule& schedule) {
    auto conditions = idleAt(1000);
    assert(!AgcMaintenance::shouldAttempt(schedule, conditions,
                                          []() { return false; }));
}

void testBasicSchedulingAndIntervalChanges() {
    AgcMaintenance::Schedule schedule;
    initialize(schedule);

    auto conditions = idleAt(4999);
    assert(!AgcMaintenance::shouldAttempt(schedule, conditions,
                                          []() { return false; }));
    conditions.nowMs = 5000;
    conditions.lastPacketMs = 4000;
    assert(AgcMaintenance::shouldAttempt(schedule, conditions,
                                         []() { return false; }));
    schedule.recordAttempt(conditions.nowMs);
    conditions.nowMs = 5001;
    assert(!AgcMaintenance::shouldAttempt(schedule, conditions,
                                          []() { return false; }));

    conditions.nowMs = 9000;
    conditions.intervalSec = 2;
    assert(!AgcMaintenance::shouldAttempt(schedule, conditions,
                                          []() { return false; }));
    conditions.nowMs = 10999;
    assert(!AgcMaintenance::shouldAttempt(schedule, conditions,
                                          []() { return false; }));
    conditions.nowMs = 11000;
    conditions.lastPacketMs = 10000;
    assert(AgcMaintenance::shouldAttempt(schedule, conditions,
                                         []() { return false; }));

    conditions.intervalSec = 0;
    assert(!AgcMaintenance::shouldAttempt(schedule, conditions,
                                          []() { return false; }));
    conditions.nowMs += 60000;
    assert(!AgcMaintenance::shouldAttempt(schedule, conditions,
                                          []() { return false; }));
}

void testBusyConditionsDeferWithoutCheckingRx() {
    AgcMaintenance::Schedule schedule;
    initialize(schedule);
    auto conditions = idleAt(5000);
    int rxChecks = 0;
    auto isReceiving = [&rxChecks]() { ++rxChecks; return false; };

    conditions.radioReady = false;
    assert(!AgcMaintenance::shouldAttempt(schedule, conditions, isReceiving));
    conditions.radioReady = true;
    conditions.intentionalStandby = true;
    assert(!AgcMaintenance::shouldAttempt(schedule, conditions, isReceiving));
    conditions.intentionalStandby = false;
    conditions.txActive = true;
    assert(!AgcMaintenance::shouldAttempt(schedule, conditions, isReceiving));
    conditions.txActive = false;
    conditions.dio1Pending = true;
    assert(!AgcMaintenance::shouldAttempt(schedule, conditions, isReceiving));
    conditions.dio1Pending = false;
    conditions.lastPacketMs = conditions.nowMs - 499U;
    assert(!AgcMaintenance::shouldAttempt(schedule, conditions, isReceiving));
    assert(rxChecks == 0);

    conditions.lastPacketMs = conditions.nowMs - 500U;
    assert(AgcMaintenance::shouldAttempt(schedule, conditions, isReceiving));
    assert(rxChecks == 1);

    assert(!AgcMaintenance::shouldAttempt(
        schedule, conditions, []() { return true; }));
}

void testPostTxQuietWindowAndNoQueuedAttempts() {
    AgcMaintenance::Schedule schedule;
    initialize(schedule);
    auto conditions = idleAt(20000);
    conditions.lastTxCompleteMs = 10001;
    assert(!AgcMaintenance::shouldAttempt(schedule, conditions,
                                          []() { return false; }));
    conditions.nowMs = 20001;
    conditions.lastPacketMs = 19001;
    assert(AgcMaintenance::shouldAttempt(schedule, conditions,
                                         []() { return false; }));

    // Several intervals elapsed while busy, but the completed attempt starts
    // one fresh interval rather than leaving additional work queued.
    schedule.recordAttempt(conditions.nowMs);
    conditions.nowMs = 20002;
    assert(!AgcMaintenance::shouldAttempt(schedule, conditions,
                                          []() { return false; }));
    conditions.nowMs = 24001;
    conditions.lastPacketMs = 23001;
    assert(AgcMaintenance::shouldAttempt(schedule, conditions,
                                         []() { return false; }));
}

void testMillisWrap() {
    AgcMaintenance::Schedule schedule;
    schedule.intervalSec = 4;
    schedule.lastAttemptMs = UINT32_MAX - 2000U;
    auto conditions = idleAt(1999);
    conditions.lastPacketMs = 999;
    assert(AgcMaintenance::shouldAttempt(schedule, conditions,
                                         []() { return false; }));
}

}  // namespace

int main() {
    testFormIntervalParsing();
    testSuccessfulOrdering();
    testStandbyFailureSkipsResetAndRecoversRx();
    testResetFailureRecoversRx();
    testRxFailureDoesNotReportSuccess();
    testBasicSchedulingAndIntervalChanges();
    testBusyConditionsDeferWithoutCheckingRx();
    testPostTxQuietWindowAndNoQueuedAttempts();
    testMillisWrap();
    return 0;
}
