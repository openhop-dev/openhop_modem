#pragma once

#include <stdint.h>

namespace RfRequests {

enum class Kind : uint8_t {
    Get,
    Set,
    Error,
};

enum class Target : uint8_t {
    Fem,
    RxBoost,
};

template <typename Source>
struct Request {
    Kind kind;
    Target target;
    uint8_t apply;   // FEM Set
    uint8_t value;   // Set; error code for Error
    Source src;
};

// FEM and RX boost replies, errors included, go out in arrival order: ERROR
// frames do not name a command, so the host matches them to requests by
// order. A Set waits while mustWait() holds, and every request queued behind
// it waits too.
template <typename Source, uint8_t Depth>
class Queue {
public:
    bool push(const Request<Source>& request) {
        if (count_ >= Depth) return false;
        items_[(head_ + count_) % Depth] = request;
        ++count_;
        return true;
    }

    template <typename MustWaitFn, typename AnswerFn>
    void drain(MustWaitFn mustWait, AnswerFn answer) {
        drain(mustWait, answer, [](const Request<Source>&) { return true; });
    }

    template <typename MustWaitFn, typename AnswerFn, typename CanAnswerFn>
    void drain(MustWaitFn mustWait, AnswerFn answer, CanAnswerFn canAnswer) {
        while (count_ > 0) {
            const Request<Source> request = items_[head_];
            if (!canAnswer(request)) return;
            if (request.kind == Kind::Set && mustWait()) return;
            head_ = (uint8_t)((head_ + 1) % Depth);
            --count_;
            answer(request);
        }
    }

    uint8_t size() const { return count_; }

    // Reclaim canceled requests even behind a live Set that is still waiting.
    // Compact in place without changing the FIFO order of surviving requests.
    template <typename DiscardFn>
    void discardIf(DiscardFn discard) {
        uint8_t kept = 0;
        for (uint8_t i = 0; i < count_; ++i) {
            const Request<Source> request = items_[(head_ + i) % Depth];
            if (!discard(request)) {
                items_[(head_ + kept) % Depth] = request;
                ++kept;
            }
        }
        count_ = kept;
    }

private:
    Request<Source> items_[Depth] = {};
    uint8_t head_ = 0;
    uint8_t count_ = 0;
};

}  // namespace RfRequests
