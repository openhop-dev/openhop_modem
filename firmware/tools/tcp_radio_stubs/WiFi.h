#pragma once
#include "Arduino.h"
#include "IPAddress.h"
#include <map>
#include <vector>
#include "socket_port.h"

// The only fake boundary is the unavailable Arduino network driver. Listener
// queues, reads and writes are observable; the real server/parser own sessions.
struct RadioSocketStub : RadioSocketPort {
    IPAddress local{192,168,1,20}, remote{192,168,1,2};
    std::deque<uint8_t> input;
    size_t reads = 0;
};
struct WiFiClient {
    std::shared_ptr<RadioSocketStub> socket;
    explicit operator bool() const { return socket && socket->open; }
    bool connected() { return bool(*this); }
    void stop() { closeRadioSocket(socket); }
    int fd() const { return !socket || !socket->open ? -1 :
        (socket->nativeFd >= 0 ? socket->nativeFd : socket->modelFd); }
    IPAddress remoteIP() { return socket ? socket->remote : IPAddress(); }
    IPAddress localIP() { return socket ? socket->local : IPAddress(); }
    void setNoDelay(bool) {}
    int available() { return socket ? int(socket->input.size()) : 0; }
    int read() {
        if (!socket || socket->input.empty()) return -1;
        const int byte = socket->input.front();
        socket->input.pop_front(); ++socket->reads; return byte;
    }
    size_t write(const uint8_t* data, size_t length) {
        if (!connected()) return 0;
        ++socket->legacyWriteCalls;
        if (socket->nativeFd >= 0 && radioLegacySocketWrite)
            return radioLegacySocketWrite(fd(), data, length);
        const auto result = radioSocketSend(fd(), data, length, MSG_DONTWAIT);
        return result > 0 ? size_t(result) : 0;
    }
};
struct RadioListenerStub {
    bool listening = false;
    size_t begins = 0, ends = 0, accepts = 0;
    std::deque<WiFiClient> pending;
};
inline std::map<uint16_t, RadioListenerStub> radioListeners;
struct WiFiServer {
    uint16_t port;
    explicit WiFiServer(uint16_t value) : port(value) {}
    void begin() { auto& l = radioListeners[port]; l.listening = true; ++l.begins; }
    void end() { auto& l = radioListeners[port]; l.listening = false; ++l.ends; }
    void setNoDelay(bool) {}
    WiFiClient available() {
        auto& l = radioListeners[port];
        if (!l.listening || l.pending.empty()) return {};
        auto result = l.pending.front(); l.pending.pop_front(); ++l.accepts;
        return result;
    }
};
inline std::shared_ptr<RadioSocketStub> connectRadio(uint16_t port,
        IPAddress remote = IPAddress(192,168,1,2),
        IPAddress local = IPAddress(192,168,1,20)) {
    auto socket = std::make_shared<RadioSocketStub>();
    socket->remote = remote; socket->local = local;
    registerRadioSocket(socket);
    radioListeners[port].pending.push_back(WiFiClient{socket});
    return socket;
}
