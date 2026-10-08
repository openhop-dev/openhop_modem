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
        while (count_ > 0) {
            const Request<Source> request = items_[head_];
            if (request.kind == Kind::Set && mustWait()) return;
            head_ = (uint8_t)((head_ + 1) % Depth);
            --count_;
            answer(request);
        }
    }

    uint8_t size() const { return count_; }

private:
    Request<Source> items_[Depth] = {};
    uint8_t head_ = 0;
    uint8_t count_ = 0;
};

}  // namespace RfRequests
