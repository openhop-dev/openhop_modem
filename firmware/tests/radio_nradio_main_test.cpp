// Full production setup/loop/parser/TCP/runtime. Only SDK/device ports are fake.
#include <Arduino.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <WiFi.h>
#include "radio_layout.h"
#include "runtime_stats.h"
#include "protocol.h"
#include <array>
#include <set>
#include <cassert>
#include <iostream>
void setup(); void loop();
namespace OTAManager { extern int ticks; }
using Socket = std::shared_ptr<RadioSocketStub>;
static std::array<Socket, NRADIO_COUNT> sockets;
static std::array<SX1262*, NRADIO_COUNT> radios{};
static std::vector<uint8_t> frame(uint8_t cmd, const uint8_t* p=nullptr, uint16_t n=0) {
    std::vector<uint8_t> out{PROTO_SYNC,cmd,uint8_t(n),uint8_t(n>>8)};
    if (n) out.insert(out.end(),p,p+n);
    const auto crc=crc16_ccitt(out.data()+1,out.size()-1);
    out.push_back(crc);out.push_back(crc>>8);return out;
}
struct Reply { uint8_t cmd; std::vector<uint8_t> data; };
static std::vector<Reply> decode(const std::vector<uint8_t>& bytes) {
    std::vector<Reply> out;
    for(size_t i=0;i<bytes.size();) {
        assert(i+6<=bytes.size() && bytes[i]==PROTO_SYNC);
        const size_t n=bytes[i+2]|(bytes[i+3]<<8);
        assert(i+n+6<=bytes.size());
        assert(crc16_ccitt(bytes.data()+i+1,n+3)==uint16_t(bytes[i+n+4]|(bytes[i+n+5]<<8)));
        out.push_back({bytes[i+1],{bytes.begin()+i+4,bytes.begin()+i+4+n}});i+=n+6;
    }
    return out;
}
static bool has(size_t id,uint8_t cmd) { for(const auto& r:decode(sockets[id]->output))if(r.cmd==cmd)return true;return false; }
static void pump(int n=6){while(n--)loop();}
static void clear(){for(auto& s:sockets)if(s)s->output.clear();Serial.tx.clear();}
static void send(size_t id,uint8_t cmd,const uint8_t* p=nullptr,uint16_t n=0){auto b=frame(cmd,p,n);sockets[id]->input.insert(sockets[id]->input.end(),b.begin(),b.end());pump();}
static void auth(size_t id){const uint8_t token[]={'s','e','c','r','e','t'};send(id,CMD_AUTH,token,sizeof(token));assert(has(id,CMD_AUTH_OK));}
static void exclusive(size_t id,uint8_t cmd){assert(has(id,cmd));for(size_t j=0;j<sockets.size();++j)if(j!=id)assert(sockets[j]->output.empty());}
static RuntimeStats::RadioSnapshot snapshot(size_t id){RuntimeStats::RadioSnapshot out;assert(RuntimeStats::captureRadio(id,out));return out;}
int main(int argc,char** argv){
    assert(argc==2);std::cout.setf(std::ios::unitbuf);const std::string mode=argv[1];
    const unsigned all=(1U<<NRADIO_COUNT)-1U;
    unsigned mask=mode=="off"?0:mode=="highest"?(1U<<(NRADIO_COUNT-1)):mode=="sparse"?(1U|(1U<<(NRADIO_COUNT-1))):all;
    preferences["enabled"]={'c',mask};
    const int lastNss=RadioLayout::at(NRADIO_COUNT-1).hardware?RadioLayout::at(NRADIO_COUNT-1).hardware->pin_lora_nss:-1;
    if(mode=="failed")beginErrors[lastNss]=-1;
    setup();
    if(mode=="invalid"){
        assert(!RadioLayout::valid() && chips.empty() && radioListeners.empty() && gpioWrites.empty());
        std::cout<<"PASS over-capacity layout rejected before hardware/listeners\n";return 0;
    }
    assert(RadioLayout::valid() && RuntimeStats::radioCount()==NRADIO_COUNT);
    size_t enabled=0,ready=0;SPIClass* shared=nullptr;std::set<void(*)()> callbacks;
    for(size_t i=0;i<NRADIO_COUNT;++i){
        assert(radioListeners[5055+i].listening);
        const auto s=snapshot(i);assert(s.id==i && s.tcpPort==5055+i && s.enabled==bool(mask&(1U<<i)));
        const auto& hw=*RadioLayout::at(i).hardware;
        if(!s.enabled){assert(!chips.count(hw.pin_lora_nss) && !s.ready && !s.txReady);}
        else {
            ++enabled;radios[i]=chips.at(hw.pin_lora_nss);
            assert(radios[i]->mod->dio==hw.pin_lora_dio1);
            if(shared)assert(shared==radios[i]->mod->spi);else shared=radios[i]->mod->spi;
            if(s.ready){++ready;assert(radios[i]->action);callbacks.insert(radios[i]->action);}
        }
        assert(s.ready==(s.enabled && !(mode=="failed" && i==NRADIO_COUNT-1)));
        sockets[i]=connectRadio(5055+i,IPAddress(192,168,1,10+i));
    }
    assert(chips.size()==enabled && callbacks.size()==ready);
    if(shared)assert(shared->begins==1 && shared->sck==5 && shared->miso==6 && shared->mosi==7);
    pump();for(size_t i=0;i<NRADIO_COUNT;++i)auth(i);clear();
    for(size_t i=0;i<NRADIO_COUNT;++i){
        RadioConfig cfg{uint32_t(910000000+i*100000),62500,7,5,int8_t(10+i),uint16_t(0x1212+i),32};
        send(i,CMD_SET_CONFIG,reinterpret_cast<const uint8_t*>(&cfg),sizeof(cfg));
        if(!snapshot(i).ready){exclusive(i,CMD_ERROR);clear();send(i,CMD_RADIO_RESUME);exclusive(i,CMD_ERROR);clear();send(i,CMD_PING);exclusive(i,CMD_PONG);clear();continue;}
        exclusive(i,CMD_CONFIG_RESP);const auto reply=decode(sockets[i]->output);assert(reply.size()==1 && reply[0].data.size()==sizeof(cfg));
        RadioConfig returned{};memcpy(&returned,reply[0].data.data(),sizeof(returned));assert(memcmp(&cfg,&returned,sizeof(cfg))==0);
        assert(snapshot(i).radio.syncword==cfg.syncword && snapshot(i).txReady);clear();
    }
    // All declared usable owners can be pending together on ONE shared SPI bus.
    const uint8_t payload[]={0x3e,0x00,0x41};
    for(size_t i=0;i<NRADIO_COUNT;++i)if(snapshot(i).ready){send(i,CMD_TX_REQUEST,payload,sizeof(payload));assert(radios[i]->txStarts==1 && snapshot(i).busy);}
    const int ticks=OTAManager::ticks;
    for(size_t i=0;i<NRADIO_COUNT;++i){send(i,CMD_PING);assert(has(i,CMD_PONG));}
    assert(OTAManager::ticks>ticks);clear();
    // Deliver actual registered high-index ISR thunks, rejecting garbage first.
    for(size_t step=0;step<NRADIO_COUNT;++step){
        const size_t i=NRADIO_COUNT-step-1;if(!snapshot(i).ready)continue;
        radios[i]->irqStatus=0x71;radios[i]->edge(RADIOLIB_SX126X_IRQ_TX_DONE);pump();
        assert(snapshot(i).busy && !has(i,CMD_TX_DONE));
        radios[i]->irqStatus=0xd4;pump();exclusive(i,CMD_TX_DONE);
        assert(snapshot(i).status.tx_count==1 && !snapshot(i).busy);clear();
    }
    for(size_t i=0;i<NRADIO_COUNT;++i)if(snapshot(i).ready){
        radios[i]->receive({0xa0,uint8_t(i)});pump();exclusive(i,CMD_RX_PACKET);
        if(i)assert(Serial.tx.empty());else assert(!Serial.tx.empty());clear();
        send(i,CMD_CAD_REQUEST);assert(radios[i]->scans==1 && snapshot(i).busy);
        radios[i]->edge(RADIOLIB_SX126X_IRQ_CAD_DONE|(i%2?RADIOLIB_SX126X_IRQ_CAD_DETECTED:0));pump();exclusive(i,CMD_CAD_RESP);clear();
    }
    // One high owner's timeout and a replacement generation cannot leak success.
    size_t target=NRADIO_COUNT;
    for(size_t i=0;i<NRADIO_COUNT;++i)if(snapshot(i).ready)target=i;
    if(target<NRADIO_COUNT){
        send(target,CMD_TX_REQUEST,payload,sizeof(payload));clockMs+=4501;pump();exclusive(target,CMD_ERROR);assert(!has(target,CMD_TX_DONE));clear();
        send(target,CMD_TX_REQUEST,payload,sizeof(payload));assert(snapshot(target).busy);
        closeRadioSocket(sockets[target]);pump();sockets[target]=connectRadio(5055+target,IPAddress(192,168,1,90));pump();auth(target);clear();
        radios[target]->edge(RADIOLIB_SX126X_IRQ_TX_DONE);pump();
        for(size_t i=0;i<NRADIO_COUNT;++i)assert(!has(i,CMD_TX_DONE));
        send(target,CMD_PING);assert(has(target,CMD_PONG));clear();
    }
    for(size_t i=0;i<NRADIO_COUNT;++i){const auto s=snapshot(i);assert(s.id==i && s.tcpPort==5055+i && s.enabled==bool(mask&(1U<<i)));}
    std::cout<<"PASS full production non-Duo N="<<NRADIO_COUNT<<" mode="<<mode
             <<": activation/shared SPI, independent configs/auth, high ISR/RX/TX/CAD, deadlines/generations/disabled IDs\n";
}
