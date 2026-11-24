// Full production main/runtime/TCP/parser; only unavailable SDK/device ports
// are injected. AF_UNIX socketpair sends are real; no IP listener or hardware.
#include <Arduino.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <WiFi.h>
#include "tcp_server.h"
#include "protocol.h"
#include "radio_layout.h"
#include "runtime_stats.h"
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#define REQUIRE(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
void setup(); void loop();
namespace OTAManager { extern int ticks; }
static std::vector<uint8_t> frame(uint8_t cmd, const uint8_t* data = nullptr, uint16_t len = 0) {
    std::vector<uint8_t> bytes{PROTO_SYNC, cmd, uint8_t(len), uint8_t(len >> 8)};
    if (len) bytes.insert(bytes.end(), data, data + len);
    const auto crc = crc16_ccitt(bytes.data() + 1, bytes.size() - 1);
    bytes.push_back(crc); bytes.push_back(crc >> 8); return bytes;
}
static void put(const std::shared_ptr<RadioSocketStub>& socket, const std::vector<uint8_t>& bytes) {
    socket->input.insert(socket->input.end(), bytes.begin(), bytes.end());
}
static bool has(const std::vector<uint8_t>& bytes, uint8_t cmd) {
    bool found = false;
    size_t pos = 0;
    while (pos < bytes.size()) {
        REQUIRE(bytes.size() - pos >= 6 && bytes[pos] == PROTO_SYNC);
        const size_t len = bytes[pos + 2] | (bytes[pos + 3] << 8);
        REQUIRE(pos + len + 6 <= bytes.size());
        const auto crc = crc16_ccitt(bytes.data() + pos + 1, len + 3);
        REQUIRE(bytes[pos + len + 4] == uint8_t(crc) && bytes[pos + len + 5] == uint8_t(crc >> 8));
        found |= bytes[pos + 1] == cmd; pos += len + 6;
    }
    return found;
}
static void pump(int count = 4) { while (count--) loop(); }
struct SaturatedPair {
    int peer = -1;
    std::shared_ptr<RadioSocketStub> socket;
    size_t filler = 0;
    explicit SaturatedPair(std::shared_ptr<RadioSocketStub> target) : socket(std::move(target)) {
        int pair[2]; REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
        int small = 4096;
        REQUIRE(::setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0);
        char fill[4096]{};
        ssize_t written;
        while ((written = ::send(pair[0], fill, sizeof(fill), MSG_DONTWAIT)) > 0) filler += size_t(written);
        REQUIRE(errno == EAGAIN || errno == EWOULDBLOCK);
        attachRadioSocket(socket, pair[0]); peer = pair[1];
    }
    std::vector<uint8_t> readPeer() {
        std::vector<uint8_t> bytes;
        uint8_t buffer[4096]; ssize_t count;
        while ((count = ::recv(peer, buffer, sizeof(buffer), MSG_DONTWAIT)) > 0)
            bytes.insert(bytes.end(), buffer, buffer + count);
        REQUIRE(count == 0 || errno == EAGAIN || errno == EWOULDBLOCK);
        return bytes;
    }
    void discardFiller() { const auto bytes = readPeer(); REQUIRE(bytes.size() == filler); }
    ~SaturatedPair() { closeRadioSocket(socket); if (peer >= 0) ::close(peer); }
};
static void saturatedEvent() {
    setup();
    REQUIRE(radioListeners[5055].listening && radioListeners[5056].listening);
    auto first = connectRadio(5055), second = connectRadio(5056); pump();
    const uint8_t token[] = {'s','e','c','r','e','t'};
    put(first, frame(CMD_AUTH, token, sizeof(token)));
    put(second, frame(CMD_AUTH, token, sizeof(token))); pump();
    REQUIRE(has(first->output, CMD_AUTH_OK) && has(second->output, CMD_AUTH_OK));
    first->output.clear(); second->output.clear(); Serial.tx.clear();
    // Ignore setup traffic in the writer-spy counters.
    first->legacyWriteCalls = second->legacyWriteCalls = 0;
    SaturatedPair blocked(first);
    put(second, frame(CMD_PING));
    chips.at(RadioLayout::at(0).hardware->pin_lora_nss)->receive({0x42});
    chips.at(RadioLayout::at(1).hardware->pin_lora_nss)->receive({0x51});
    const int before = OTAManager::ticks;
    const auto start = std::chrono::steady_clock::now();
    loop();
    const auto microsElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "full_main saturated_event: RF2 PONG/RX + OTA latency_us=" << microsElapsed
              << " RF1_legacy_write_calls=" << first->legacyWriteCalls
              << " RF1_send_calls=" << first->sendCalls << "\n";
    REQUIRE(has(second->output, CMD_PONG) && has(second->output, CMD_RX_PACKET));
    REQUIRE(OTAManager::ticks == before + 1);
    REQUIRE(microsElapsed < 100000); // Host acceptance ceiling, not a device RT claim.
    REQUIRE(first->legacyWriteCalls == 0 && second->legacyWriteCalls == 0);
    REQUIRE(first->open && TCPServer::isClientReady(0));
    blocked.discardFiller(); pump();
    const auto drained = blocked.readPeer();
    REQUIRE(has(drained, CMD_RX_PACKET) && drained.size() == 13);
    REQUIRE(drained[10] == 0x42); // Original RF1 payload, neither truncated nor replayed.
    pump(); REQUIRE(blocked.readPeer().empty());
    TCPServer::end();
}
static void partialWrites() {
    setup();
    auto first = connectRadio(5055), second = connectRadio(5056); pump();
    const uint8_t token[] = {'s','e','c','r','e','t'};
    put(first, frame(CMD_AUTH, token, sizeof(token)));
    put(second, frame(CMD_AUTH, token, sizeof(token))); pump();
    first->output.clear(); second->output.clear();
    const uint8_t payload[] = {0xAA,0xBB,0xCC,0xDD};
    const auto event = frame(CMD_RX_PACKET, payload, sizeof(payload));
    const auto reply = frame(CMD_PONG);
    const ResponseRoute route{TransportSource::TCP, 0, TCPServer::connectionGeneration()};
    first->sendResults = {2,0,-EAGAIN,1,-EINTR,-ENOMEM,-ENOBUFS,3};
    TCPServer::writeRadioEvent(0, event.data(), event.size());
    TCPServer::writeTo(route, reply.data(), reply.size());
    TCPServer::write(event.data(), event.size());
    REQUIRE(first->output == (std::vector<uint8_t>{event.begin(), event.begin() + 2}));
    const auto reads = first->reads;
    for (int i = 0; i < 32; ++i) {
        put(second, frame(CMD_PING));
        const auto calls = first->sendCalls;
        loop();
        REQUIRE(first->sendCalls - calls <= 1); // One send per session service, no drain loop.
        REQUIRE(first->reads == reads && has(second->output, CMD_PONG));
        second->output.clear();
    }
    auto expected = event; expected.insert(expected.end(), reply.begin(), reply.end());
    expected.insert(expected.end(), event.begin(), event.end());
    REQUIRE(first->open && first->output == expected);
    REQUIRE(first->legacyWriteCalls == 0 && second->legacyWriteCalls == 0);
    REQUIRE(has(first->output, CMD_RX_PACKET) && has(first->output, CMD_PONG));
    for (size_t i = 0; i < first->attempts.size(); ++i) {
        REQUIRE(first->attempts[i] <= MAX_FRAME_SIZE);
        REQUIRE(first->flags[i] & MSG_DONTWAIT);
    }
    TCPServer::end();
}
using Sockets = std::pair<std::shared_ptr<RadioSocketStub>, std::shared_ptr<RadioSocketStub>>;
static Sockets prepare(bool authenticateFirst = true) {
    setup();
    auto first = connectRadio(5055), second = connectRadio(5056); pump();
    const uint8_t token[] = {'s','e','c','r','e','t'};
    if (authenticateFirst) put(first, frame(CMD_AUTH, token, sizeof(token)));
    put(second, frame(CMD_AUTH, token, sizeof(token))); pump();
    REQUIRE(has(second->output, CMD_AUTH_OK));
    if (authenticateFirst) REQUIRE(has(first->output, CMD_AUTH_OK));
    first->output.clear(); second->output.clear(); Serial.tx.clear();
    return {first, second};
}
static void peerProgress(const std::shared_ptr<RadioSocketStub>& peer) {
    peer->output.clear(); put(peer, frame(CMD_PING));
    chips.at(RadioLayout::at(1).hardware->pin_lora_nss)->receive({0x69});
    const auto beforeTicks = OTAManager::ticks;
    const auto beforeDelays = delayCalls;
    const auto start = std::chrono::steady_clock::now();
    loop();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    REQUIRE(elapsed < 100000 && delayCalls == beforeDelays);
    REQUIRE(OTAManager::ticks == beforeTicks + 1);
    REQUIRE(has(peer->output, CMD_PONG) && has(peer->output, CMD_RX_PACKET));
    REQUIRE(peer->legacyWriteCalls == 0);
    std::cout << "peer PONG/RX/OTA progress_us=" << elapsed << "\n";
}
static void saturatedReplyOrAuth(const std::string& scenario) {
    const bool isReply = scenario == "saturated_reply";
    const bool isAuth = scenario == "saturated_auth_ok";
    auto sockets = prepare(isReply);
    auto first = sockets.first, second = sockets.second;
    SaturatedPair blocked(first);
    const uint8_t token[] = {'s','e','c','r','e','t'};
    auto command = isReply ? frame(CMD_PING) : frame(CMD_AUTH, token, sizeof(token));
    if (!isReply && !isAuth) command.back() ^= 1;
    put(first, command); peerProgress(second);
    REQUIRE(first->open && TCPServer::isClientReady(0) == (isReply || isAuth));
    blocked.discardFiller(); pump();
    const uint8_t error = ERR_CRC_MISMATCH;
    const auto expected = isReply ? frame(CMD_PONG) : isAuth ? frame(CMD_AUTH_OK) : frame(CMD_ERROR, &error, 1);
    REQUIRE(blocked.readPeer() == expected);
    REQUIRE(first->legacyWriteCalls == 0);
    pump(); REQUIRE(blocked.readPeer().empty());
    TCPServer::end();
}
static void closingAuthError(bool wrongToken, bool partial) {
    auto sockets = prepare(false);
    auto first = sockets.first, second = sockets.second;
    std::unique_ptr<SaturatedPair> blocked;
    if (partial) first->sendResults = {2,0,-EAGAIN};
    else blocked.reset(new SaturatedPair(first));
    const uint8_t wrong[] = {'x'}, valid[] = {'s','e','c','r','e','t'};
    put(first, wrongToken ? frame(CMD_AUTH, wrong, 1) : frame(CMD_PING));
    put(first, frame(CMD_AUTH, valid, sizeof(valid))); put(first, frame(CMD_PING));
    peerProgress(second);
    REQUIRE(!TCPServer::isClientReady(0));
    REQUIRE(first->open); // Fail closed to input, but finish the bounded error frame.
    const auto consumed = first->reads; REQUIRE(consumed == (wrongToken ? 7U : 6U));
    const auto rejected = frame(CMD_PONG);
    TCPServer::write(rejected.data(), rejected.size()); // Cannot bypass failed auth via legacy raw output.
    TCPServer::writeRadioEvent(0, rejected.data(), rejected.size());
    const uint8_t error = ERR_UNAUTHORIZED;
    const auto expected = frame(CMD_ERROR, &error, 1);
    if (partial) REQUIRE(first->output == (std::vector<uint8_t>{expected.begin(), expected.begin() + 2}));
    for (int i = 0; i < 2; ++i) { peerProgress(second); REQUIRE(first->reads == consumed); }
    if (blocked) blocked->discardFiller();
    pump(8);
    REQUIRE(!first->open && !TCPServer::isClientReady(0) && first->reads == consumed);
    REQUIRE((blocked ? blocked->readPeer() : first->output) == expected);
    REQUIRE(first->legacyWriteCalls == 0);
    TCPServer::end();
}
static void congestionDeadline(bool wrap, bool agedTail) {
    auto sockets = prepare();
    auto first = sockets.first, second = sockets.second;
    clockMs = wrap ? UINT32_MAX - 999 : 0;
    const uint32_t start = clockMs;
    first->maxChunk = 1;
    const auto reply = frame(CMD_PONG);
    TCPServer::writeRadioEvent(0, reply.data(), reply.size());
    if (agedTail) TCPServer::writeRadioEvent(0, reply.data(), reply.size());
    const auto generation = TCPServer::connectionGeneration();
    REQUIRE(first->output.size() == 1);
    clockMs = start + 1500;
    if (agedTail) first->maxChunk = 1000; // Complete head; tail is still 1500 ms old.
    peerProgress(second);
    REQUIRE(first->open);
    first->maxChunk = 1;
    clockMs = start + 1999; peerProgress(second); REQUIRE(first->open);
    clockMs = start + 2000; peerProgress(second);
    REQUIRE(!first->open && !TCPServer::isConnectionCurrent(generation));
    REQUIRE(!TCPServer::canQueueResponse());
    REQUIRE(TCPServer::isClientReady(1));
    TCPServer::end();
}
static size_t countFrames(const std::vector<uint8_t>& bytes, uint8_t cmd) {
    (void)has(bytes, cmd); // Complete-frame and CRC validation first.
    size_t result = 0;
    for (size_t pos = 0; pos < bytes.size();) {
        result += bytes[pos + 1] == cmd;
        pos += 6 + (bytes[pos + 2] | (bytes[pos + 3] << 8));
    }
    return result;
}
static void queuePressure(const std::string& mode) {
    auto sockets = prepare(); auto first = sockets.first, second = sockets.second;
    const ResponseRoute old{TransportSource::TCP, 0, TCPServer::connectionGeneration()};
    first->capacity = 0;
    const std::vector<uint8_t> payload(261, 0x78);
    const auto packet = frame(CMD_RX_PACKET, payload.data(), payload.size());
    REQUIRE(packet.size() == MAX_FRAME_SIZE && TCPServer::OUTPUT_QUEUE_FRAMES == 4);
    for (size_t i = 0; i < TCPServer::OUTPUT_QUEUE_FRAMES; ++i) {
        REQUIRE(TCPServer::canQueueResponse(old));
        TCPServer::writeRadioEvent(0, packet.data(), packet.size());
    }
    REQUIRE(TCPServer::isRouteCurrent(old) && !TCPServer::canQueueResponse(old));
    const auto beforeReads = first->reads;
    put(first, frame(CMD_PING)); peerProgress(second);
    REQUIRE(first->reads == beforeReads && first->output.empty());
    if (mode == "queue_drain") {
        first->capacity = std::numeric_limits<size_t>::max(); pump(12);
        auto expected = packet;
        for (size_t i = 1; i < TCPServer::OUTPUT_QUEUE_FRAMES; ++i)
            expected.insert(expected.end(), packet.begin(), packet.end());
        const auto pong = frame(CMD_PONG); expected.insert(expected.end(), pong.begin(), pong.end());
        REQUIRE(first->output == expected && first->open && TCPServer::canQueueResponse(old));
        REQUIRE(countFrames(first->output, CMD_RX_PACKET) == 4 && countFrames(first->output, CMD_PONG) == 1);
    } else {
        if (mode == "queue_overflow_reply") TCPServer::writeTo(old, packet.data(), packet.size());
        else if (mode == "queue_overflow_event") TCPServer::writeRadioEvent(0, packet.data(), packet.size());
        else if (mode == "queue_overflow_raw") TCPServer::write(packet.data(), packet.size());
        else if (mode == "queue_disconnect") first->open = false;
        else return;
        REQUIRE(!first->open);
        auto replacement = connectRadio(5055); pump();
        const uint8_t token[] = {'s','e','c','r','e','t'};
        put(replacement, frame(CMD_AUTH, token, sizeof(token))); pump();
        REQUIRE(replacement->output == frame(CMD_AUTH_OK)); replacement->output.clear();
        REQUIRE(!TCPServer::isRouteCurrent(old) && !TCPServer::canQueueResponse(old));
        REQUIRE(TCPServer::connectionGeneration() > old.generation);
        TCPServer::writeTo(old, packet.data(), packet.size()); pump();
        REQUIRE(replacement->output.empty() && first->output.empty());
        put(replacement, frame(CMD_PING)); pump();
        REQUIRE(replacement->output == frame(CMD_PONG));
    }
    TCPServer::end();
}
static void pendingCompletion(bool reconnect) {
    auto sockets = prepare(); auto first = sockets.first, second = sockets.second;
    const RadioConfig config{915000000, 125000, 7, 5, 10, 0x12, 16};
    put(first, frame(CMD_SET_CONFIG, reinterpret_cast<const uint8_t*>(&config), sizeof(config))); pump();
    REQUIRE(has(first->output, CMD_CONFIG_RESP)); first->output.clear();
    const uint8_t payload = 0x42;
    put(first, frame(CMD_TX_REQUEST, &payload, 1)); pump();
    auto* chip = chips.at(RadioLayout::at(0).hardware->pin_lora_nss);
    REQUIRE(chip->txStarts == 1);
    const ResponseRoute old{TransportSource::TCP, 0, TCPServer::connectionGeneration()};
    first->capacity = 0;
    const auto packet = frame(CMD_RX_PACKET, &payload, 1);
    for (size_t i = 0; i < TCPServer::OUTPUT_QUEUE_FRAMES; ++i)
        TCPServer::writeRadioEvent(0, packet.data(), packet.size());
    REQUIRE(!TCPServer::canQueueResponse(old));
    const auto reads = first->reads;
    put(first, frame(CMD_GET_CONFIG)); chip->edge(RADIOLIB_SX126X_IRQ_TX_DONE);
    peerProgress(second);
    REQUIRE(first->reads == reads && first->output.empty());
    RuntimeStats::RadioSnapshot snapshot{}; REQUIRE(RuntimeStats::captureRadio(0, snapshot));
    REQUIRE(snapshot.status.tx_count == 1);
    if (reconnect) {
        first->open = false;
        auto replacement = connectRadio(5055); pump();
        const uint8_t token[] = {'s','e','c','r','e','t'};
        put(replacement, frame(CMD_AUTH, token, sizeof(token))); pump();
        REQUIRE(replacement->output == frame(CMD_AUTH_OK));
        REQUIRE(!TCPServer::isRouteCurrent(old) && first->output.empty());
        replacement->output.clear(); pump(); REQUIRE(replacement->output.empty());
    } else {
        first->capacity = std::numeric_limits<size_t>::max(); pump(20);
        REQUIRE(first->open && first->input.empty());
        REQUIRE(countFrames(first->output, CMD_RX_PACKET) == TCPServer::OUTPUT_QUEUE_FRAMES);
        REQUIRE(countFrames(first->output, CMD_TX_DONE) == 1);
        REQUIRE(countFrames(first->output, CMD_CONFIG_RESP) == 1);
        REQUIRE(countFrames(first->output, CMD_ERROR) == 0);
        std::vector<uint8_t> commands;
        for (size_t pos = 0; pos < first->output.size();) {
            commands.push_back(first->output[pos + 1]);
            pos += 6 + (first->output[pos + 2] | (first->output[pos + 3] << 8));
        }
        REQUIRE(commands == (std::vector<uint8_t>{CMD_RX_PACKET,CMD_RX_PACKET,CMD_RX_PACKET,CMD_RX_PACKET,CMD_TX_DONE,CMD_CONFIG_RESP}));
    }
    TCPServer::end();
}
static void preauthErrorPressure() {
    auto sockets = prepare(false); auto first = sockets.first, second = sockets.second;
    first->capacity = 0;
    auto bad = frame(CMD_AUTH); bad.back() ^= 1;
    for (size_t i = 0; i < 12; ++i) put(first, bad);
    for (size_t i = 0; i < 8; ++i) peerProgress(second);
    REQUIRE(first->open && !TCPServer::isClientReady(0));
    REQUIRE(first->reads == TCPServer::OUTPUT_QUEUE_FRAMES * bad.size());
    first->capacity = std::numeric_limits<size_t>::max(); pump(32);
    REQUIRE(first->input.empty() && countFrames(first->output, CMD_ERROR) == 12);
    REQUIRE(!TCPServer::isClientReady(0) && first->open);
    TCPServer::end();
}
static void wrongTokenDeadline() {
    auto sockets = prepare(false); auto first = sockets.first, second = sockets.second;
    SaturatedPair blocked(first);
    const uint32_t start = clockMs;
    const uint8_t token = 0x78; put(first, frame(CMD_AUTH, &token, 1));
    peerProgress(second); REQUIRE(first->open && !TCPServer::isClientReady(0));
    clockMs = start + TCPServer::OUTPUT_CONGESTION_TIMEOUT_MS - 1; peerProgress(second);
    REQUIRE(first->open);
    clockMs += 1; peerProgress(second); REQUIRE(!first->open);
    TCPServer::end();
}
static void invalidOrFatalWrite(const std::string& mode) {
    auto sockets = prepare(); auto first = sockets.first, second = sockets.second;
    const ResponseRoute old{TransportSource::TCP, 0, TCPServer::connectionGeneration()};
    const auto reply = frame(CMD_PONG);
    if (mode == "oversized_write") {
        std::vector<uint8_t> large(MAX_FRAME_SIZE + 1, 0xAA);
        TCPServer::writeTo(old, large.data(), large.size());
    } else if (mode == "null_write") TCPServer::writeTo(old, nullptr, 1);
    else {
        first->sendResults = {2,-EPIPE};
        TCPServer::writeTo(old, reply.data(), reply.size());
        REQUIRE(first->output.size() == 2); peerProgress(second);
    }
    REQUIRE(!first->open && !TCPServer::isRouteCurrent(old));
    peerProgress(second); TCPServer::end();
}
static void oneSendPerService() {
    auto sockets = prepare(); auto first = sockets.first, second = sockets.second;
    const std::vector<uint8_t> payload(261, 0x19);
    const auto packet = frame(CMD_RX_PACKET, payload.data(), payload.size());
    first->capacity = 0;
    TCPServer::writeRadioEvent(0, packet.data(), packet.size());
    put(first, frame(CMD_PING));
    first->capacity = std::numeric_limits<size_t>::max();
    const auto calls = first->sendCalls, bytes = first->attemptedBytes;
    peerProgress(second);
    REQUIRE(first->sendCalls - calls == 1 && first->attemptedBytes - bytes == MAX_FRAME_SIZE);
    REQUIRE(first->output == packet && first->input.empty());
    loop();
    auto expected = packet; const auto pong = frame(CMD_PONG);
    expected.insert(expected.end(), pong.begin(), pong.end());
    REQUIRE(first->output == expected);
    TCPServer::end();
}
static void queuedCleanup(bool restart) {
    auto sockets = prepare(); auto first = sockets.first, second = sockets.second;
    const ResponseRoute old{TransportSource::TCP, 0, TCPServer::connectionGeneration()};
    const auto packet = frame(CMD_PONG);
    first->maxChunk = 2;
    TCPServer::writeTo(old, packet.data(), packet.size());
    TCPServer::writeTo(old, packet.data(), packet.size());
    REQUIRE(first->output.size() == 2);
    if (restart) {
        extern void processHostCommand(uint8_t, const uint8_t*, uint16_t, const ResponseRoute&);
        const uint16_t ports[] = {5055,5056};
        REQUIRE(TCPServer::beginRadios(ports, 2, "secret", processHostCommand));
    } else TCPServer::invalidateInterface(first->local);
    REQUIRE(!first->open && !TCPServer::isRouteCurrent(old));
    auto replacement = connectRadio(5055); pump();
    const uint8_t token[] = {'s','e','c','r','e','t'};
    put(replacement, frame(CMD_AUTH, token, sizeof(token))); pump();
    REQUIRE(replacement->output == frame(CMD_AUTH_OK)); replacement->output.clear();
    TCPServer::writeTo(old, packet.data(), packet.size()); pump();
    REQUIRE(replacement->output.empty());
    put(replacement, frame(CMD_PING)); pump(); REQUIRE(replacement->output == packet);
    TCPServer::end();
}
static void sdkReferenceControl() {
    REQUIRE(radioLegacySocketWrite != nullptr);
    auto socket = connectRadio(5055); SaturatedPair blocked(socket);
    const auto bytes = frame(CMD_PONG);
    const auto start = std::chrono::steady_clock::now();
    const size_t count = radioLegacySocketWrite(socket->nativeFd, bytes.data(), bytes.size());
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "REFERENCE CONTROL (expected BAD SDK behavior, NOT acceptance): SDK write blocked_ms="
              << elapsed << " bytes=" << count << "\n";
    REQUIRE(count == 0 && elapsed >= 9000);
}
int main(int argc, char** argv) {
    preferences["enabled"]={'c',3}; // Congestion fixtures require both owners active.
    REQUIRE(argc == 2); std::cout.setf(std::ios::unitbuf);
    const std::string scenario = argv[1];
    if (scenario == "saturated_event") saturatedEvent();
    else if (scenario == "partial_writes") partialWrites();
    else if (scenario == "saturated_reply" || scenario == "saturated_auth_ok" || scenario == "saturated_crc_error") saturatedReplyOrAuth(scenario);
    else if (scenario == "wrong_token_congestion") closingAuthError(true, false);
    else if (scenario == "preauth_command_congestion") closingAuthError(false, false);
    else if (scenario == "wrong_token_partial") closingAuthError(true, true);
    else if (scenario == "deadline") congestionDeadline(false, false);
    else if (scenario == "deadline_wrap") congestionDeadline(true, false);
    else if (scenario == "deadline_aged_tail") congestionDeadline(false, true);
    else if (scenario == "one_send_per_service") oneSendPerService();
    else if (scenario == "queued_restart") queuedCleanup(true);
    else if (scenario == "queued_invalidation") queuedCleanup(false);
    else if (scenario == "sdk_reference_control") sdkReferenceControl();
    else if (scenario == "queue_drain" || scenario == "queue_overflow_reply" || scenario == "queue_overflow_event" || scenario == "queue_overflow_raw" || scenario == "queue_disconnect") queuePressure(scenario);
    else if (scenario == "pending_completion") pendingCompletion(false);
    else if (scenario == "pending_completion_reconnect") pendingCompletion(true);
    else if (scenario == "preauth_error_pressure") preauthErrorPressure();
    else if (scenario == "wrong_token_deadline") wrongTokenDeadline();
    else if (scenario == "oversized_write" || scenario == "null_write" || scenario == "fatal_write") invalidOrFatalWrite(scenario);
    else return 2;
    std::cout << scenario << ": PASS\n";
}
