#include "Arduino.h"
#include "WiFi.h"
#include "protocol.h"
#include "radio_runtime.h"
#include <cassert>
#include <iostream>
#include <algorithm>
#include <vector>
#include "TCP_BACKEND"
#include "FRAME_PARSER"
void noteTransportFrameError(uint8_t) {}
static bool receiving = false;
static int mutations = 0;
static std::vector<int> replies;
static std::vector<TransportSource> destinations;
static RadioRuntime* owner = nullptr;
static SX1262* rfChip = nullptr;
static size_t initialPaWrites=0,initialBoostWrites=0;
static size_t paWrites(){return std::count_if(gpioWrites.begin(),gpioWrites.end(),[](const auto& v){return v.first==BOARD.rf_frontend.pa_mode_pin;});}
static size_t boostWrites(){return std::count(rfChip->calls.begin(),rfChip->calls.end(),"boost");}
static void syncMutations(){mutations=paWrites()-initialPaWrites+boostWrites()-initialBoostWrites;}
static void applyReception(){rfChip->irq=receiving?RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED:0;}
static bool uartEnabled=true;
static size_t radioOwnerCount=1;
#define PROTO_UART Serial2
FRAME_OUTPUT
static void routedReply(const ResponseRoute& route,uint8_t cmd,const uint8_t* p,uint16_t n){
 replies.push_back(cmd);destinations.push_back(route.transport);sendFrame(cmd,p,n,route);
}
static void constructOwner(){
 const RadioRuntime::Hooks hooks{routeCurrent,routeCanReply,routedReply,nullptr,nullptr};
 owner=new RadioRuntime(0,RadioLayout::at(0),hooks);assert(owner->begin());rfChip=chips.at(BOARD.pin_lora_nss);
 initialPaWrites=paWrites();initialBoostWrites=boostWrites();
}
void pump() { for (int i=0;i<12;++i) TCPServer::loop(); }
void processRfRequests(){applyReception();owner->service();syncMutations();}
void processHostCommand(uint8_t cmd,const uint8_t* p,uint16_t n,TransportSource source){
 applyReception();owner->command(cmd,p,n,primaryRoute(source));syncMutations();
}
std::shared_ptr<SocketStub> acceptClient(){
 incomingClient.s=std::make_shared<SocketStub>();auto socket=incomingClient.s;TCPServer::loop();return socket;
}
void request(const std::shared_ptr<SocketStub>& socket,uint8_t cmd,std::initializer_list<uint8_t> payload){
 uint8_t frame[MAX_FRAME_SIZE];uint16_t size=0;std::vector<uint8_t> bytes(payload);
 TCPServer::buildFrame(frame,size,cmd,bytes.data(),bytes.size());socket->input.insert(socket->input.end(),frame,frame+size);
 TCPServer::loop();pump();
}
void authenticate(const std::shared_ptr<SocketStub>& socket){request(socket,CMD_AUTH,{'t'});assert(TCPServer::isClientReady());socket->output.clear();}
std::vector<int> wireCommands(const std::shared_ptr<SocketStub>& socket){
 std::vector<int> commands;const auto& bytes=socket->output;
 for(size_t off=0;off<bytes.size();){assert(off+6<=bytes.size()&&bytes[off]==PROTO_SYNC);
 size_t len=bytes[off+2]|(size_t(bytes[off+3])<<8);assert(off+len+6<=bytes.size());
 uint16_t crc=bytes[off+len+4]|(uint16_t(bytes[off+len+5])<<8);assert(crc==crc16_ccitt(bytes.data()+off+1,len+3));
 commands.push_back(bytes[off+1]);off+=len+6;}return commands;
}
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": " #x "\n";return 1;}}while(0)
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string scenario = argv[1];
    constructOwner();
    NETWORK_SETUP
    TCPServer::begin(5055, scenario == "tokenless_reconnect" ? "" : "t");
    auto original = acceptClient();
    authenticate(original);
    receiving = true;
    if (scenario == "purge_behind_usb")
        processHostCommand(CMD_SET_FEM_STATE, (const uint8_t*)"\1\1", 2, TransportSource::USB);
    const bool boost = scenario == "reconnect_boost";
    if (boost) request(original, CMD_SET_RX_BOOST, {1});
    else request(original, CMD_SET_FEM_STATE, {1, 1});
    CHECK(mutations == 0 && replies.empty());
    if (scenario == "async_capacity" || scenario == "async_stale_capacity") {
        receiving=false;processRfRequests();pump();replies.clear();destinations.clear();original->output.clear();
        RadioConfig cfg=owner->config();
        processHostCommand(CMD_SET_CONFIG,(const uint8_t*)&cfg,sizeof(cfg),TransportSource::TCP);
        pump();replies.clear();original->output.clear();
        request(original,CMD_TX_REQUEST,{0x91,0x92});CHECK(owner->busy());
        sendFrame(CMD_PONG,nullptr,0,TransportSource::TCP); // occupy the actual W5100S writer
#ifdef TEST_W5100S
        CHECK(!TCPServer::canQueueResponse());
#endif
        rfChip->edge(RADIOLIB_SX126X_IRQ_TX_DONE);owner->serviceIrq();
#ifdef TEST_W5100S
        CHECK(replies.empty()&&TCPServer::isClientReady());
#endif
        if(scenario=="async_stale_capacity"){
            original->open=false;auto replacement=acceptClient();authenticate(replacement);
            owner->service();pump();CHECK(replacement->output.empty());
        }else{
            pump();owner->service();pump();CHECK(TCPServer::isClientReady());
            CHECK((wireCommands(original)==std::vector<int>{CMD_PONG,CMD_TX_DONE}));
        }
    } else if (scenario == "pending_wifi_usb" || scenario == "pending_wifi_uart") {
        NETWORK_INVALIDATION_SCENARIO
    } else if (scenario == "normal" || scenario == "other_interface") {
        if (scenario == "other_interface") TCPServer::invalidateInterface(IPAddress(192,168,8,1));
        receiving = false;
        processRfRequests();
        pump();
        CHECK(mutations == 1 && replies.size() == 1 && !original->output.empty());
    } else if (scenario == "multiple_tcp_replies") {
        request(original, CMD_SET_RX_BOOST, {1});
        request(original, CMD_GET_RX_BOOST, {});
        request(original, CMD_SET_RX_BOOST, {2});
        receiving = false;
        processRfRequests();
#ifdef TEST_W5100S
        // The second setter must not touch RF until the first reply drains.
        CHECK(mutations == 1 && owner->queuedRequests() == 3);
#endif
        for (int i = 0; i < 12; ++i) { pump(); processRfRequests(); }
        pump();
        CHECK(TCPServer::isClientReady());
        CHECK(mutations == 2 && owner->queuedRequests() == 0);
        CHECK((replies == std::vector<int>{CMD_FEM_STATE_RESP, CMD_RX_BOOST_RESP,
                                         CMD_RX_BOOST_RESP, CMD_ERROR}));
        CHECK(wireCommands(original) == replies);
    } else if (scenario == "mixed_fifo") {
        processHostCommand(CMD_GET_RX_BOOST, nullptr, 0, TransportSource::UART);
        processHostCommand(CMD_GET_FEM_STATE, nullptr, 0, TransportSource::UART);
        const uint8_t invalidBoost = 2;
        processHostCommand(CMD_SET_RX_BOOST, &invalidBoost, 1, TransportSource::UART);
        CHECK(owner->queuedRequests() == 4 && replies.empty());
        receiving = false;
        processRfRequests();
        CHECK(mutations == 1);
        CHECK((replies == std::vector<int>{CMD_FEM_STATE_RESP, CMD_RX_BOOST_RESP,
                                         CMD_FEM_STATE_RESP, CMD_ERROR}));
        CHECK(destinations[2] == TransportSource::UART);
    } else {
        if (scenario == "full_stale") {
            request(original, CMD_GET_FEM_STATE, {});
            request(original, CMD_GET_RX_BOOST, {});
            request(original, CMD_SET_RX_BOOST, {2});
            CHECK(owner->queuedRequests() == 4);
        }
        if (scenario == "interface_loss") {
#ifdef TEST_W5100S
            usableIP = false; TCPServer::loop(); usableIP = true;
#else
            TCPServer::invalidateInterface(IPAddress(192,168,1,20));
#endif
        }
        else if (scenario == "restart") TCPServer::begin(5055, "t");
        else original->open = false;
        if (scenario == "disconnect_only") {
            processRfRequests(); // no transport-loop cleanup yet
            CHECK(owner->queuedRequests() == 0 && mutations == 0 && replies.empty());
        } else {
            auto replacement = acceptClient();
            if (scenario != "reconnect_unauthenticated") authenticate(replacement);
            if (scenario == "purge_behind_usb") {
                processRfRequests();
                CHECK(owner->queuedRequests() == 1); // live USB request still waits
            } else if (scenario == "full_stale") {
                request(replacement, CMD_GET_FEM_STATE, {});
                CHECK(owner->queuedRequests() == 0);
                CHECK((replies == std::vector<int>{CMD_FEM_STATE_RESP}));
            }
            if (scenario != "full_stale") {
                receiving = false;
                processRfRequests();
                CHECK(mutations == (scenario == "purge_behind_usb" ? 1 : 0));
                CHECK(replacement->output.empty());
                if (scenario == "purge_behind_usb") {
                    CHECK(replies.size() == 1 && destinations[0] == TransportSource::USB);
                } else CHECK(replies.empty());
            }
        }
    }
    std::cout << scenario << ": PASS\n";
}
