// Compile production transport and parser, not a parallel endpoint registry.
#include "WiFi.h"
#include "tcp_server.h"
#include "response_route.h"
#include "../src/tcp_server.cpp"
#include "../src/frame_parser.cpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define REQUIRE(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
struct Command { uint8_t cmd; std::vector<uint8_t> payload; ResponseRoute route; };
static std::vector<Command> commands;
static std::vector<uint8_t> legacyErrors;
static size_t legacyCommands = 0;
void noteTransportFrameError(uint8_t error) { legacyErrors.push_back(error); }
void processHostCommand(uint8_t, const uint8_t*, uint16_t, TransportSource src) {
    REQUIRE(src == TransportSource::TCP); ++legacyCommands;
}
static std::vector<uint8_t> frame(uint8_t cmd, const std::vector<uint8_t>& payload = {}) {
    uint8_t bytes[MAX_FRAME_SIZE]; uint16_t size = 0;
    TCPServer::buildFrame(bytes, size, cmd, payload.data(), payload.size());
    return {bytes, bytes + size};
}
static void append(const std::shared_ptr<RadioSocketStub>& socket, const std::vector<uint8_t>& bytes) {
    socket->input.insert(socket->input.end(), bytes.begin(), bytes.end());
}
static void onCommand(uint8_t cmd, const uint8_t* payload, uint16_t length, const ResponseRoute& route) {
    REQUIRE(TCPServer::isRouteCurrent(route));
    REQUIRE(TCPServer::canQueueResponse(route));
    commands.push_back({cmd, {payload, payload + length}, route});
    const auto reply = frame(CMD_PONG, {route.radio});
    TCPServer::writeTo(route, reply.data(), reply.size());
}
static void auth(const std::shared_ptr<RadioSocketStub>& socket, uint8_t radio) {
    append(socket, frame(CMD_AUTH, {'t'})); TCPServer::loop();
    REQUIRE(TCPServer::isClientReady(radio));
    REQUIRE(socket->output == frame(CMD_AUTH_OK)); socket->output.clear();
}
static void twoRadioCommands() {
    const uint16_t ports[] = {5055,5056};
    REQUIRE(TCPServer::beginRadios(ports, 2, "t", onCommand));
    auto first = connectRadio(5055), second = connectRadio(5056);
    TCPServer::loop();
    REQUIRE(!TCPServer::isClientReady(0) && !TCPServer::isClientReady(1));
    auth(first, 0); auth(second, 1);
    append(second, frame(CMD_PING, {2}));
    append(first, frame(CMD_PING, {1}));
    TCPServer::loop();
    REQUIRE(commands.size() == 2);
    for (const auto& cmd : commands) {
        REQUIRE(cmd.cmd == CMD_PING && cmd.route.transport == TransportSource::TCP);
        REQUIRE(cmd.payload == std::vector<uint8_t>{uint8_t(cmd.route.radio + 1)});
        REQUIRE(cmd.route.generation != 0);
    }
    REQUIRE(first->output == frame(CMD_PONG, {0}));
    REQUIRE(second->output == frame(CMD_PONG, {1}));
    REQUIRE(legacyCommands == 0);
    REQUIRE(TCPServer::getClientIP(0) == "192.168.1.2");
    REQUIRE(TCPServer::getClientIP(1) == "192.168.1.2");
    TCPServer::end();
}
static void fourRadioEvents() {
    REQUIRE(TCPServer::MAX_RADIOS == 4);
    const uint16_t ports[] = {5055,5056,5057,5058};
    REQUIRE(TCPServer::beginRadios(ports, 4, "t", onCommand));
    std::vector<std::shared_ptr<RadioSocketStub>> sockets;
    for (auto port : ports) sockets.push_back(connectRadio(port));
    TCPServer::loop();
    const auto event = frame(CMD_RX_PACKET, {0x12,0x34});
    for (uint8_t radio = 0; radio < 4; ++radio) {
        TCPServer::writeRadioEvent(radio, event.data(), event.size());
        REQUIRE(sockets[radio]->output.empty());
    }
    REQUIRE(!TCPServer::isClientReady(4) && !TCPServer::isClientReady(255));
    REQUIRE(TCPServer::getClientIP(4).empty());
    for (uint8_t radio = 0; radio < 4; ++radio) {
        auth(sockets[radio], radio);
        TCPServer::writeRadioEvent(radio, event.data(), event.size());
        for (uint8_t other = 0; other < 4; ++other) {
            REQUIRE(sockets[other]->output == (other == radio ? event : std::vector<uint8_t>{}));
            sockets[other]->output.clear();
            if (other > radio) REQUIRE(!TCPServer::isClientReady(other));
        }
    }
    TCPServer::writeRadioEvent(4, event.data(), event.size());
    TCPServer::writeRadioEvent(255, event.data(), event.size());
    sockets[2]->open = false;
    TCPServer::writeRadioEvent(2, event.data(), event.size());
    for (auto& socket : sockets) REQUIRE(socket->output.empty());
    TCPServer::write(event.data(), event.size());
    REQUIRE(sockets[0]->output == event); // Raw legacy API remains primary-only.
    REQUIRE(sockets[1]->output.empty() && sockets[3]->output.empty());
    TCPServer::end();
    for (auto port : ports) REQUIRE(!radioListeners.at(port).listening);
    for (auto& socket : sockets) REQUIRE(!socket->open);
    TCPServer::writeRadioEvent(0, event.data(), event.size());
    REQUIRE(sockets[0]->output == event);
}
static void fairService(size_t count, bool reverse) {
    const uint16_t ports[] = {5055,5056,5057,5058};
    REQUIRE(TCPServer::beginRadios(ports, count, "", onCommand));
    std::vector<std::shared_ptr<RadioSocketStub>> sockets;
    for (size_t radio = 0; radio < count; ++radio) sockets.push_back(connectRadio(ports[radio]));
    TCPServer::loop();
    for (size_t pos = 0; pos < count; ++pos) {
        const size_t radio = reverse ? count - pos - 1 : pos;
        for (uint8_t seq = 0; seq < 3; ++seq)
            append(sockets[radio], frame(CMD_PING, {uint8_t(radio),seq}));
    }
    for (size_t pass = 0; pass < 3; ++pass) {
        TCPServer::loop();
        REQUIRE(commands.size() == count * (pass + 1));
        for (size_t pos = 0; pos < count; ++pos) {
            const size_t radio = (pass + 1 + pos) % count;
            const auto& cmd = commands[pass * count + pos];
            REQUIRE(cmd.route.radio == radio);
            REQUIRE(cmd.payload == (std::vector<uint8_t>{uint8_t(radio),uint8_t(pass)}));
            REQUIRE(sockets[radio]->input.size() == (2 - pass) * 8);
        }
    }
    TCPServer::end();
}
static void byteBudgetInterleaving() {
    const uint16_t ports[] = {5055,5056};
    REQUIRE(TCPServer::beginRadios(ports, 2, "", onCommand));
    auto first = connectRadio(5055), second = connectRadio(5056);
    const auto large = frame(CMD_SET_CONFIG, std::vector<uint8_t>(261, 0x19));
    REQUIRE(large.size() == 267);
    append(first, large); append(second, frame(CMD_PING, {0x29}));
    TCPServer::loop();
    REQUIRE(first->reads == 256 && first->input.size() == 11);
    REQUIRE(commands.size() == 1 && commands[0].route.radio == 1);
    TCPServer::loop();
    REQUIRE(first->reads == 267 && first->input.empty());
    REQUIRE(commands.size() == 2 && commands[1].route.radio == 0);
    REQUIRE(commands[1].payload == std::vector<uint8_t>(261, 0x19));
    first->input.insert(first->input.end(), 300, 0); // no sync: byte budget still bounds noise
    second->input.insert(second->input.end(), 300, 0);
    const auto firstReads = first->reads, secondReads = second->reads;
    TCPServer::loop();
    REQUIRE(first->reads - firstReads == 256 && second->reads - secondReads == 256);
    REQUIRE(first->input.size() == 44 && second->input.size() == 44);
    TCPServer::end();
}
static std::vector<std::pair<uint8_t,uint8_t>> radioErrors;
static void onRadioError(uint8_t radio, uint8_t error) { radioErrors.push_back({radio,error}); }
static void crcOwner() {
    const uint16_t ports[] = {5055,5056,5057,5058};
    TCPServer::setRadioFrameErrorHandler(onRadioError);
    REQUIRE(TCPServer::beginRadios(ports, 4, "t", onCommand));
    std::vector<std::shared_ptr<RadioSocketStub>> sockets;
    for (auto port : ports) sockets.push_back(connectRadio(port));
    TCPServer::loop();
    for (uint8_t radio = 0; radio < 4; ++radio) auth(sockets[radio], radio);
    for (uint8_t radio = 0; radio < 4; ++radio) {
        auto corrupt = frame(CMD_PING, {uint8_t(10 + radio)}); corrupt.back() ^= 1;
        append(sockets[radio], corrupt); TCPServer::loop();
        REQUIRE(radioErrors.size() == size_t(radio + 1));
        REQUIRE(radioErrors.back() == (std::pair<uint8_t,uint8_t>{radio,ERR_CRC_MISMATCH}));
        REQUIRE(commands.empty() && legacyErrors.empty());
        for (uint8_t other = 0; other < 4; ++other) {
            REQUIRE(sockets[other]->output == (other == radio ? frame(CMD_ERROR, {ERR_CRC_MISMATCH}) : std::vector<uint8_t>{}));
            sockets[other]->output.clear();
        }
        append(sockets[radio], frame(CMD_PING, {radio})); TCPServer::loop();
        REQUIRE(commands.size() == 1 && commands[0].route.radio == radio);
        commands.clear(); sockets[radio]->output.clear();
    }
    // Oversized length rejection has the same owner and must recover its parser.
    append(sockets[3], {PROTO_SYNC,CMD_PING,0xFF,0x7F}); TCPServer::loop();
    REQUIRE(radioErrors.back() == (std::pair<uint8_t,uint8_t>{3,ERR_PAYLOAD_TOO_BIG}));
    REQUIRE(sockets[3]->output == frame(CMD_ERROR, {ERR_PAYLOAD_TOO_BIG}));
    REQUIRE(sockets[0]->output.empty() && sockets[1]->output.empty() && sockets[2]->output.empty());
    TCPServer::end();
    TCPServer::setRadioFrameErrorHandler(nullptr);
    TCPServer::begin(5055, "t");
    auto preauth = connectRadio(5055);
    auto corrupt = frame(CMD_AUTH, {'t'}); corrupt.back() ^= 1;
    append(preauth, corrupt); TCPServer::loop();
    REQUIRE(legacyErrors == std::vector<uint8_t>{ERR_CRC_MISMATCH});
    REQUIRE(!TCPServer::isClientReady());
    REQUIRE(preauth->output == frame(CMD_ERROR, {ERR_CRC_MISMATCH}));
    REQUIRE(legacyCommands == 0);
    TCPServer::end();
}
static ResponseRoute requestRoute(const std::shared_ptr<RadioSocketStub>& socket, uint8_t radio) {
    append(socket, frame(CMD_PING, {radio})); TCPServer::loop();
    REQUIRE(!commands.empty() && commands.back().route.radio == radio);
    const auto route = commands.back().route;
    socket->output.clear();
    return route;
}
static void staleRoutes() {
    const uint16_t ports[] = {5055,5056,5057,5058};
    REQUIRE(TCPServer::beginRadios(ports, 4, "t", onCommand));
    std::vector<std::shared_ptr<RadioSocketStub>> sockets;
    std::vector<ResponseRoute> routes;
    for (auto port : ports) sockets.push_back(connectRadio(port));
    TCPServer::loop();
    for (uint8_t radio = 0; radio < 4; ++radio) {
        auth(sockets[radio], radio); routes.push_back(requestRoute(sockets[radio], radio));
    }
    const auto reply = frame(CMD_PONG, {0x79});
    auto reject = [&](const ResponseRoute& route) {
        REQUIRE(!TCPServer::isRouteCurrent(route) && !TCPServer::canQueueResponse(route));
        TCPServer::writeTo(route, reply.data(), reply.size());
        for (auto& socket : sockets) REQUIRE(socket->output.empty());
    };
    reject({TransportSource::TCP,4,routes[0].generation});
    reject({TransportSource::TCP,255,routes[0].generation});
    reject({TransportSource::TCP,0,0});
    reject({TransportSource::USB,0,routes[0].generation});
    reject({TransportSource::UART,0,routes[0].generation});
    reject({TransportSource::TCP,0,routes[0].generation + 1});
    sockets[1]->open = false;
    reject(routes[1]); // Must fail even before the transport loop notices close.
    REQUIRE(TCPServer::isRouteCurrent(routes[0]) && TCPServer::isRouteCurrent(routes[2]));
    auto oldSecond = sockets[1]; sockets[1] = connectRadio(5056); TCPServer::loop();
    reject(routes[1]); // A connected but unauthenticated replacement owns no route.
    reject({TransportSource::TCP,1,routes[1].generation + 1}); // Even its exact generation needs auth.
    auth(sockets[1], 1); reject(routes[1]);
    const auto replacement = requestRoute(sockets[1], 1);
    REQUIRE(replacement.generation != routes[1].generation);
    REQUIRE(TCPServer::isRouteCurrent(replacement));
    TCPServer::writeTo(replacement, reply.data(), reply.size());
    REQUIRE(sockets[1]->output == reply && oldSecond->output.empty()); sockets[1]->output.clear();
    REQUIRE(TCPServer::connectionGeneration() == routes[0].generation);
    REQUIRE(TCPServer::isConnectionCurrent(routes[0].generation));
    REQUIRE(TCPServer::getClientIP() == TCPServer::getClientIP(0));
    REQUIRE(TCPServer::canQueueResponse());
    // Repeated bank shrink/re-expand must retain generations of dormant slots.
    for (size_t iteration = 0; iteration < 3; ++iteration) {
        REQUIRE(TCPServer::beginRadios(ports, 1, "t", onCommand));
        for (const auto& route : routes) reject(route);
        REQUIRE(!TCPServer::isRouteCurrent(replacement));
        REQUIRE(TCPServer::beginRadios(ports, 4, "t", onCommand));
        for (uint8_t radio = 0; radio < 4; ++radio) sockets[radio] = connectRadio(ports[radio]);
        TCPServer::loop();
        for (uint8_t radio = 0; radio < 4; ++radio) {
            auth(sockets[radio], radio); reject(routes[radio]);
            const auto current = requestRoute(sockets[radio], radio);
            REQUIRE(current.generation > routes[radio].generation); routes[radio] = current;
        }
    }
    TCPServer::end();
    for (const auto& route : routes) reject(route);
    REQUIRE(TCPServer::connectionGeneration() == 0 && !TCPServer::isConnectionCurrent(0));
    REQUIRE(!TCPServer::canQueueResponse() && TCPServer::getClientIP().empty());
}
static void exhaustedGeneration() {
    const uint16_t ports[] = {5055,5056};
    REQUIRE(TCPServer::beginRadios(ports, 2, "", onCommand));
    // Failure injection at the actual production counter boundary. Never reuse
    // an old nonzero route after the 32-bit identity space is exhausted.
    TCPServer::sessions[1].generation = UINT32_MAX - 1;
    auto first = connectRadio(5055), second = connectRadio(5056); TCPServer::loop();
    const auto last = requestRoute(second, 1);
    REQUIRE(last.generation == UINT32_MAX);
    second->open = false;
    auto replacement = connectRadio(5056); TCPServer::loop();
    REQUIRE(!replacement->open && !TCPServer::isClientReady(1));
    REQUIRE(!TCPServer::isRouteCurrent(last) && TCPServer::isClientReady(0));
    REQUIRE(first->open);
    REQUIRE(TCPServer::beginRadios(ports, 2, "", onCommand));
    replacement = connectRadio(5056); TCPServer::loop();
    REQUIRE(!replacement->open && !TCPServer::isClientReady(1));
    TCPServer::end();
}
static void isolatedAuth(bool wrongToken) {
    const uint16_t ports[] = {5055,5056,5057,5058};
    for (uint8_t target = 0; target < 4; ++target) {
        REQUIRE(TCPServer::beginRadios(ports, 4, "t", onCommand));
        std::vector<std::shared_ptr<RadioSocketStub>> sockets;
        for (auto port : ports) sockets.push_back(connectRadio(port));
        TCPServer::loop(); commands.clear();
        for (uint8_t radio = 0; radio < 4; ++radio) if (radio != target) auth(sockets[radio], radio);
        REQUIRE(!TCPServer::isClientReady(target));
        const auto readyPeer = uint8_t((target + 1) % 4);
        if (wrongToken) append(sockets[target], frame(CMD_AUTH, {'x'}));
        append(sockets[target], frame(CMD_PING, {target}));
        append(sockets[readyPeer], frame(CMD_PING, {readyPeer}));
        TCPServer::loop();
        REQUIRE(!sockets[target]->open && !TCPServer::isClientReady(target));
        REQUIRE(sockets[target]->output == frame(CMD_ERROR, {ERR_UNAUTHORIZED}));
        REQUIRE(commands.size() == 1 && commands[0].route.radio == readyPeer);
        for (uint8_t radio = 0; radio < 4; ++radio)
            if (radio != target) REQUIRE(sockets[radio]->open && TCPServer::isClientReady(radio));
        TCPServer::loop(); REQUIRE(commands.size() == 1); // queued unauthorized command cannot escape
        TCPServer::end();
    }
}
static void partialInterleaving() {
    const uint16_t ports[] = {5055,5056,5057,5058};
    REQUIRE(TCPServer::beginRadios(ports, 4, "t", onCommand));
    std::vector<std::shared_ptr<RadioSocketStub>> sockets;
    for (auto port : ports) sockets.push_back(connectRadio(port));
    const auto authentication = frame(CMD_AUTH, {'t'});
    for (auto& socket : sockets) append(socket, {authentication.begin(), authentication.begin() + 3});
    TCPServer::loop();
    for (uint8_t radio = 0; radio < 4; ++radio) {
        REQUIRE(!TCPServer::isClientReady(radio) && sockets[radio]->output.empty());
        append(sockets[radio], {authentication.begin() + 3, authentication.end()});
        append(sockets[radio], frame(CMD_PING, {radio}));
    }
    TCPServer::loop();
    REQUIRE(commands.empty()); // AUTH itself consumes the one-frame quota on every listener.
    for (auto& socket : sockets) { REQUIRE(socket->output == frame(CMD_AUTH_OK)); socket->output.clear(); }
    TCPServer::loop(); REQUIRE(commands.size() == 4);
    for (auto& socket : sockets) socket->output.clear();
    commands.clear();
    std::vector<std::vector<uint8_t>> frames;
    for (uint8_t radio = 0; radio < 4; ++radio) {
        frames.push_back(frame(CMD_SET_CONFIG, {radio,PROTO_SYNC,uint8_t(radio + 5),0x40}));
        append(sockets[radio], {frames[radio].begin(), frames[radio].begin() + 5});
    }
    TCPServer::loop(); REQUIRE(commands.empty());
    for (int radio = 3; radio >= 0; --radio) {
        append(sockets[radio], {frames[radio].begin() + 5, frames[radio].end()});
        TCPServer::loop();
        REQUIRE(commands.size() == size_t(4 - radio));
        REQUIRE(commands.back().route.radio == radio);
        REQUIRE(commands.back().payload == (std::vector<uint8_t>{uint8_t(radio),PROTO_SYNC,uint8_t(radio + 5),0x40}));
    }
    TCPServer::end();
}
static void lanAdmission() {
    const uint16_t ports[] = {5055,5056,5057,5058};
    const IPAddress privateIPs[] = {IPAddress(10,1,2,3),IPAddress(172,16,2,3),IPAddress(169,254,2,3),IPAddress(127,0,0,2)};
    for (uint8_t target = 0; target < 4; ++target) {
        REQUIRE(TCPServer::beginRadios(ports, 4, "t", onCommand));
        std::vector<std::shared_ptr<RadioSocketStub>> sockets;
        for (uint8_t radio = 0; radio < 4; ++radio) {
            sockets.push_back(connectRadio(ports[radio], radio == target ? IPAddress(8,8,8,8) : privateIPs[radio]));
            append(sockets.back(), frame(CMD_AUTH, {'t'}));
        }
        TCPServer::loop();
        REQUIRE(!sockets[target]->open && sockets[target]->reads == 0 && sockets[target]->output.empty());
        for (uint8_t radio = 0; radio < 4; ++radio) if (radio != target) {
            REQUIRE(TCPServer::isClientReady(radio));
            REQUIRE(sockets[radio]->output == frame(CMD_AUTH_OK));
            sockets[radio]->output.clear();
            requestRoute(sockets[radio], radio);
        }
        TCPServer::end();
    }
}
static void oneClientPerPort() {
    const uint16_t ports[] = {5055,5056,5057,5058};
    REQUIRE(TCPServer::beginRadios(ports, 4, "", onCommand));
    std::vector<std::shared_ptr<RadioSocketStub>> originals, waiting;
    std::vector<ResponseRoute> routes;
    for (auto port : ports) originals.push_back(connectRadio(port));
    TCPServer::loop();
    for (uint8_t radio = 0; radio < 4; ++radio) routes.push_back(requestRoute(originals[radio], radio));
    for (uint8_t radio = 0; radio < 4; ++radio) {
        waiting.push_back(connectRadio(ports[radio], IPAddress(192,168,1,3)));
        append(waiting.back(), frame(CMD_PING, {uint8_t(radio + 4)}));
        append(originals[radio], frame(CMD_PING, {radio}));
    }
    TCPServer::loop();
    for (uint8_t radio = 0; radio < 4; ++radio) {
        REQUIRE(waiting[radio]->reads == 0 && waiting[radio]->output.empty());
        REQUIRE(radioListeners.at(ports[radio]).pending.size() == 1);
        REQUIRE(TCPServer::isRouteCurrent(routes[radio]));
    }
    originals[2]->open = false; TCPServer::loop();
    REQUIRE(!TCPServer::isRouteCurrent(routes[2]));
    REQUIRE(waiting[2]->output == frame(CMD_PONG, {2}));
    REQUIRE(commands.back().route.radio == 2 && commands.back().route.generation > routes[2].generation);
    for (uint8_t radio = 0; radio < 4; ++radio) if (radio != 2) {
        REQUIRE(waiting[radio]->reads == 0 && TCPServer::isRouteCurrent(routes[radio]));
        REQUIRE(TCPServer::getClientIP(radio) == "192.168.1.2");
    }
    REQUIRE(TCPServer::getClientIP(2) == "192.168.1.3");
    TCPServer::end();
}
static void interfaceInvalidation() {
    const uint16_t ports[] = {5055,5056,5057,5058};
    const IPAddress wifi(192,168,1,20), ethernet(10,0,0,20);
    REQUIRE(TCPServer::beginRadios(ports, 4, "t", onCommand));
    std::vector<std::shared_ptr<RadioSocketStub>> sockets;
    std::vector<ResponseRoute> routes;
    std::vector<std::vector<uint8_t>> requests;
    for (uint8_t radio = 0; radio < 4; ++radio)
        sockets.push_back(connectRadio(ports[radio], IPAddress(192,168,1,2), radio % 2 ? ethernet : wifi));
    TCPServer::loop();
    for (uint8_t radio = 0; radio < 4; ++radio) {
        auth(sockets[radio], radio); routes.push_back(requestRoute(sockets[radio], radio));
        requests.push_back(frame(CMD_PING, {radio,0xAA}));
        append(sockets[radio], {requests[radio].begin(), requests[radio].begin() + 5});
    }
    TCPServer::loop(); commands.clear();
    sockets[0]->local = IPAddress(); sockets[2]->local = IPAddress();
    TCPServer::invalidateInterface(wifi);
    const auto event = frame(CMD_RX_PACKET, {0x4A});
    for (uint8_t radio = 0; radio < 4; ++radio) {
        REQUIRE(sockets[radio]->open == bool(radio % 2));
        REQUIRE(TCPServer::isRouteCurrent(routes[radio]) == bool(radio % 2));
        REQUIRE(radioListeners.at(ports[radio]).listening);
        if (radio % 2) {
            append(sockets[radio], {requests[radio].begin() + 5, requests[radio].end()});
        } else {
            TCPServer::writeTo(routes[radio], event.data(), event.size());
            TCPServer::writeRadioEvent(radio, event.data(), event.size());
        }
    }
    TCPServer::loop(); REQUIRE(commands.size() == 2);
    for (const auto& cmd : commands) {
        REQUIRE(cmd.route.radio % 2);
        REQUIRE(cmd.payload == (std::vector<uint8_t>{cmd.route.radio,0xAA}));
    }
    REQUIRE(sockets[0]->output.empty() && sockets[2]->output.empty());
    for (uint8_t radio : {0,2}) {
        sockets[radio] = connectRadio(ports[radio]); TCPServer::loop(); auth(sockets[radio], radio);
        const auto route = requestRoute(sockets[radio], radio);
        REQUIRE(route.generation > routes[radio].generation && !TCPServer::isRouteCurrent(routes[radio]));
    }
    TCPServer::invalidateInterface(ethernet);
    REQUIRE(!sockets[1]->open && !sockets[3]->open && sockets[0]->open && sockets[2]->open);
    TCPServer::end();
}
static void legacyPrimary() {
    TCPServer::begin(5055, "t");
    auto socket = connectRadio(5055); TCPServer::loop();
    REQUIRE(!TCPServer::isClientReady() && TCPServer::connectionGeneration() == 0);
    auth(socket, 0);
    const auto generation = TCPServer::connectionGeneration();
    REQUIRE(generation != 0 && TCPServer::isConnectionCurrent(generation));
    append(socket, frame(CMD_AUTH, {'x'})); TCPServer::loop(); // Existing idempotent AUTH semantics.
    REQUIRE(socket->output == frame(CMD_AUTH_OK) && TCPServer::isClientReady()); socket->output.clear();
    append(socket, frame(CMD_PING)); TCPServer::loop();
    REQUIRE(legacyCommands == 1 && commands.empty());
    socket->open = false; TCPServer::loop();
    REQUIRE(!TCPServer::isConnectionCurrent(generation));
    TCPServer::end(); TCPServer::end(); TCPServer::loop();
}
static void corruptFrameBudget() {
    const uint16_t ports[] = {5055,5056};
    REQUIRE(TCPServer::beginRadios(ports, 2, "", onCommand));
    auto first = connectRadio(5055), second = connectRadio(5056);
    auto corrupt = frame(CMD_PING); corrupt.back() ^= 1;
    append(first, corrupt); append(first, corrupt); append(first, frame(CMD_PING));
    append(second, frame(CMD_PING)); TCPServer::loop();
    REQUIRE(first->reads == corrupt.size() && first->input.size() == corrupt.size() * 2);
    REQUIRE(first->output == frame(CMD_ERROR, {ERR_CRC_MISMATCH}));
    REQUIRE(commands.size() == 1 && commands[0].route.radio == 1);
    TCPServer::loop();
    REQUIRE(first->reads == corrupt.size() * 2 && commands.size() == 1);
    TCPServer::loop();
    REQUIRE(first->input.empty() && commands.size() == 2 && commands[1].route.radio == 0);
    TCPServer::end();
}
static void customPorts() {
    const uint16_t ports[] = {6150,65535,1024};
    REQUIRE(TCPServer::beginRadios(ports, 3, "", onCommand));
    REQUIRE(radioListeners.size() == 3);
    for (uint8_t radio = 0; radio < 3; ++radio) {
        REQUIRE(radioListeners.at(ports[radio]).listening);
        auto socket = connectRadio(ports[radio]);
        const auto route = requestRoute(socket, radio);
        REQUIRE(TCPServer::isRouteCurrent(route));
    }
    REQUIRE(!TCPServer::isClientReady(3));
    TCPServer::end();
    for (auto port : ports) REQUIRE(!radioListeners.at(port).listening);
}
static void invalidDefinitions() {
    const uint16_t valid[] = {5055,5056,5057,5058};
    const uint16_t duplicate[] = {5055,5056,5056};
    const uint16_t zero[] = {5055,0};
    const uint16_t http[] = {5055,80};
    const uint16_t excess[] = {5055,5056,5057,5058,5059};
    auto reject = [&] {
        const auto before = radioListeners;
        REQUIRE(!TCPServer::beginRadios(duplicate, 3, "changed", onCommand));
        REQUIRE(!TCPServer::beginRadios(zero, 2, "changed", onCommand));
        REQUIRE(!TCPServer::beginRadios(http, 2, "changed", onCommand));
        REQUIRE(!TCPServer::beginRadios(excess, 5, "changed", onCommand));
        REQUIRE(!TCPServer::beginRadios(valid, 0, "changed", onCommand));
        REQUIRE(!TCPServer::beginRadios(nullptr, 2, "changed", onCommand));
        REQUIRE(!TCPServer::beginRadios(valid, 2, "changed", nullptr));
        REQUIRE(before.size() == radioListeners.size());
        for (const auto& entry : before) {
            const auto& after = radioListeners.at(entry.first);
            REQUIRE(entry.second.begins == after.begins && entry.second.ends == after.ends);
            REQUIRE(entry.second.listening == after.listening);
        }
    };
    reject(); // No partial startup even when the first definition is valid.
    REQUIRE(TCPServer::beginRadios(valid, 2, "t", onCommand));
    auto first = connectRadio(5055), second = connectRadio(5056);
    TCPServer::loop(); auth(first, 0); auth(second, 1);
    reject(); // Invalid restart must not destroy live listeners or change auth.
    REQUIRE(first->open && second->open && TCPServer::isClientReady(1));
    append(second, frame(CMD_PING)); TCPServer::loop();
    REQUIRE(commands.size() == 1 && commands[0].route.radio == 1);
    TCPServer::end();
}
int main(int argc, char** argv) {
    REQUIRE(argc == 2);
    const std::string scenario = argv[1];
    if (scenario == "custom_ports") customPorts();
    else if (scenario == "corrupt_frame_budget") corruptFrameBudget();
    else if (scenario == "isolated_wrong_token") isolatedAuth(true);
    else if (scenario == "isolated_preauth_command") isolatedAuth(false);
    else if (scenario == "partial_interleaving") partialInterleaving();
    else if (scenario == "lan_admission") lanAdmission();
    else if (scenario == "one_client_per_port") oneClientPerPort();
    else if (scenario == "interface_invalidation") interfaceInvalidation();
    else if (scenario == "legacy_primary") legacyPrimary();
    else if (scenario == "stale_routes") staleRoutes();
    else if (scenario == "exhausted_generation") exhaustedGeneration();
    else if (scenario == "crc_owner") crcOwner();
    else if (scenario == "fair_two_forward") fairService(2, false);
    else if (scenario == "fair_two_reverse") fairService(2, true);
    else if (scenario == "fair_four_forward") fairService(4, false);
    else if (scenario == "fair_four_reverse") fairService(4, true);
    else if (scenario == "byte_budget_interleaving") byteBudgetInterleaving();
    else if (scenario == "four_radio_events") fourRadioEvents();
    else if (scenario == "invalid_definitions") invalidDefinitions();
    else if (scenario == "two_radio_commands") twoRadioCommands();
    else return 2;
    std::cout << scenario << ": PASS\n";
}
