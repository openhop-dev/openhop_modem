#pragma once
// Host-only lwIP socket port. Fake descriptors model finite send capacity;
// native descriptors execute real POSIX send on AF_UNIX socketpairs. The real
// TCP server/parser still own all queues, authentication and generations.
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <vector>
#include <algorithm>

struct RadioSocketPort {
    bool open = true;
    int nativeFd = -1, modelFd = -1;
    std::vector<uint8_t> output;
    size_t capacity = std::numeric_limits<size_t>::max();
    size_t maxChunk = std::numeric_limits<size_t>::max();
    // >=0: maximum bytes for this attempt (0 means a zero write); <0: -errno.
    std::deque<int> sendResults;
    size_t sendCalls = 0, legacyWriteCalls = 0, attemptedBytes = 0;
    std::vector<size_t> attempts;
    std::vector<int> flags;
};
inline std::map<int, std::weak_ptr<RadioSocketPort>> radioSocketDescriptors;
inline int nextRadioDescriptor = 1000000;
using RadioLegacyWrite = size_t (*)(int, const uint8_t*, size_t);
inline RadioLegacyWrite radioLegacySocketWrite = nullptr;
inline size_t radioUntrackedSendCalls = 0;
inline int registerRadioSocket(const std::shared_ptr<RadioSocketPort>& socket) {
    const int descriptor = nextRadioDescriptor++;
    socket->modelFd = descriptor;
    radioSocketDescriptors[descriptor] = socket;
    return descriptor;
}
inline void attachRadioSocket(const std::shared_ptr<RadioSocketPort>& socket, int descriptor) {
    socket->nativeFd = descriptor;
    radioSocketDescriptors[descriptor] = socket;
}
inline ssize_t radioSocketSend(int descriptor, const void* bytes, size_t length, int flags) {
    auto found = radioSocketDescriptors.find(descriptor);
    auto socket = found == radioSocketDescriptors.end() ? nullptr : found->second.lock();
    if (!socket) {
        ++radioUntrackedSendCalls;
        return ::send(descriptor, bytes, length, flags);
    }
    ++socket->sendCalls; socket->attemptedBytes += length;
    socket->attempts.push_back(length); socket->flags.push_back(flags);
    if (!socket->open) { errno = EBADF; return -1; }
    if (socket->nativeFd == descriptor) return ::send(descriptor, bytes, length, flags);
    size_t accepted = std::min(length, std::min(socket->capacity, socket->maxChunk));
    if (!socket->sendResults.empty()) {
        const int result = socket->sendResults.front(); socket->sendResults.pop_front();
        if (result < 0) { errno = -result; return -1; }
        accepted = std::min(accepted, size_t(result));
        if (result == 0) return 0;
    }
    if (!accepted && length) { errno = EAGAIN; return -1; }
    const auto* data = static_cast<const uint8_t*>(bytes);
    socket->output.insert(socket->output.end(), data, data + accepted);
    if (socket->capacity != std::numeric_limits<size_t>::max()) socket->capacity -= accepted;
    return static_cast<ssize_t>(accepted);
}
inline void closeRadioSocket(const std::shared_ptr<RadioSocketPort>& socket) {
    if (!socket) return;
    socket->open = false;
    if (socket->nativeFd >= 0) {
        radioSocketDescriptors.erase(socket->nativeFd);
        ::close(socket->nativeFd); socket->nativeFd = -1;
    }
    radioSocketDescriptors.erase(socket->modelFd);
}
