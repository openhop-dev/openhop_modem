// Recording W5100S hardware boundary for the production TCP namespace.
// Socket parsing/authentication, connection lifecycle and TX state machine
// remain production code; these shims do not model physical SPI timing.
#pragma once
#include "WiFi.h"
#include <vector>

inline bool usableIP = true;
inline std::shared_ptr<SocketStub> activeSocket;
namespace EthernetManager { inline bool hasIP() { return usableIP; } }
using EthernetClient = WiFiClient;
constexpr uint8_t MAX_SOCK_NUM = 4;
constexpr int SPI_ETHERNET_SETTINGS = 0;
constexpr uint8_t Sock_CLOSE = 1, Sock_SEND = 2;
namespace SnSR { constexpr uint8_t ESTABLISHED = 1, CLOSE_WAIT = 2; }
namespace SnIR { constexpr uint8_t SEND_OK = 1, TIMEOUT = 2; }
struct FakeSPI {
    void beginTransaction(int) {}
    void endTransaction() {}
};
struct W5100Stub {
    uint16_t SMASK = 2047, SSIZE = 2048;
    FakeSPI spi;
    FakeSPI* getSPI() { return &spi; }
    void writeSnCR(uint8_t, uint8_t cmd) {
        if (cmd == Sock_CLOSE && activeSocket) activeSocket->open = false;
    }
    uint8_t readSnSR(uint8_t) { return activeSocket && activeSocket->open ? SnSR::ESTABLISHED : 0; }
    uint8_t readSnCR(uint8_t) { return 0; }
    uint8_t readSnIR(uint8_t) { return SnIR::SEND_OK; }
    void writeSnIR(uint8_t, uint8_t) {}
    uint16_t readSnTX_FSR(uint8_t) { return 2048; }
    uint16_t readSnTX_WR(uint8_t) { return 0; }
    void writeSnTX_WR(uint8_t, uint16_t) {}
    uint16_t SBASE(uint8_t) { return 0; }
    bool hasOffsetAddressMapping() { return true; }
    void write(uint16_t, const uint8_t* bytes, size_t size) {
        activeSocket->output.insert(activeSocket->output.end(), bytes, bytes + size);
    }
};
inline W5100Stub W5100;
struct EthernetServer {
    inline static uint16_t server_port[MAX_SOCK_NUM] = {};
    uint16_t port;
    explicit EthernetServer(uint16_t value) : port(value) {}
    void begin() { server_port[0] = port; }
    EthernetClient accept() {
        auto result = incomingClient;
        incomingClient = {};
        if (result) activeSocket = result.s;
        return result;
    }
};
namespace W5100sBoundedRx {
enum class PollResult { BYTE, EMPTY, CLOSED, STALLED };
struct Reader {
    std::shared_ptr<SocketStub> socket;
    void clear() { socket.reset(); }
    void reset(uint8_t) { socket = activeSocket; }
    bool connected() { return socket && socket->open; }
    bool settled(uint32_t, bool& stalled) { stalled = false; return true; }
    PollResult poll(uint32_t, uint8_t& byte) {
        if (!connected()) return PollResult::CLOSED;
        if (socket->input.empty()) return PollResult::EMPTY;
        byte = socket->input.front();
        socket->input.pop_front();
        return PollResult::BYTE;
    }
};
}
