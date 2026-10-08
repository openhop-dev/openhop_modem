#include "rf_request_queue.h"

#include <cassert>
#include <iostream>
#include <vector>

namespace {

using RfRequests::Kind;
using RfRequests::Target;
using Request = RfRequests::Request<uint8_t>;
using Queue = RfRequests::Queue<uint8_t, 4>;

Request get(uint8_t src) { return Request{ Kind::Get, Target::Fem, 0, 0, src }; }
Request set(uint8_t apply, uint8_t value, uint8_t src) { return Request{ Kind::Set, Target::Fem, apply, value, src }; }
Request error(uint8_t code, uint8_t src) { return Request{ Kind::Error, Target::Fem, 0, code, src }; }
Request boostSet(uint8_t value, uint8_t src) { return Request{ Kind::Set, Target::RxBoost, 0, value, src }; }

std::vector<Request> drain(Queue& queue, bool busy) {
    std::vector<Request> answered;
    queue.drain([&]() { return busy; }, [&](const Request& r) { answered.push_back(r); });
    return answered;
}

void testIdleAnswersImmediately() {
    Queue queue;
    assert(queue.push(set(0x01, 0x01, 7)));
    const auto answered = drain(queue, false);
    assert(answered.size() == 1);
    assert(answered[0].kind == Kind::Set);
    assert(answered[0].apply == 0x01 && answered[0].value == 0x01 && answered[0].src == 7);
    assert(queue.size() == 0);
}

void testGetAndErrorDoNotWait() {
    Queue queue;
    assert(queue.push(get(1)));
    assert(queue.push(error(0x06, 1)));
    const auto answered = drain(queue, true);
    assert(answered.size() == 2);
    assert(answered[0].kind == Kind::Get);
    assert(answered[1].kind == Kind::Error && answered[1].value == 0x06);
}

void testSetHoldsLaterRequestsInOrder() {
    Queue queue;
    assert(queue.push(get(1)));
    assert(queue.push(set(0x02, 0x02, 1)));
    assert(queue.push(get(2)));
    assert(queue.push(error(0x06, 1)));

    auto answered = drain(queue, true);
    assert(answered.size() == 1);
    assert(answered[0].kind == Kind::Get && answered[0].src == 1);
    assert(queue.size() == 3);

    answered = drain(queue, false);
    assert(answered.size() == 3);
    assert(answered[0].kind == Kind::Set);
    assert(answered[1].kind == Kind::Get && answered[1].src == 2);
    assert(answered[2].kind == Kind::Error);
    assert(queue.size() == 0);
}

void testFullQueueRejectsAndWraps() {
    Queue queue;
    for (uint8_t i = 0; i < 4; ++i) assert(queue.push(set(0x01, i & 0x01, i)));
    assert(!queue.push(get(9)));
    assert(drain(queue, true).empty());

    auto answered = drain(queue, false);
    assert(answered.size() == 4);
    for (uint8_t i = 0; i < 4; ++i) assert(answered[i].src == i);

    // Leave the head mid-ring so later pushes wrap past the end.
    assert(queue.push(get(20)));
    assert(queue.push(set(0x01, 0x01, 21)));
    assert(queue.push(get(22)));
    answered = drain(queue, true);
    assert(answered.size() == 1 && answered[0].src == 20);
    assert(queue.push(get(23)));
    assert(queue.push(error(0x06, 24)));
    assert(!queue.push(get(25)));
    answered = drain(queue, false);
    assert(answered.size() == 4);
    assert(answered[0].src == 21 && answered[1].src == 22);
    assert(answered[2].src == 23 && answered[3].src == 24);
}

void testRxBoostSetHoldsFemRepliesInOrder() {
    Queue queue;
    assert(queue.push(boostSet(1, 1)));
    assert(queue.push(get(2)));
    assert(drain(queue, true).empty());

    const auto answered = drain(queue, false);
    assert(answered.size() == 2);
    assert(answered[0].target == Target::RxBoost && answered[0].value == 1);
    assert(answered[1].target == Target::Fem && answered[1].kind == Kind::Get);
}

}  // namespace

int main() {
    testIdleAnswersImmediately();
    testGetAndErrorDoNotWait();
    testSetHoldsLaterRequestsInOrder();
    testFullQueueRejectsAndWraps();
    testRxBoostSetHoldsFemRepliesInOrder();
    std::cout << "rf request queue tests passed\n";
    return 0;
}
