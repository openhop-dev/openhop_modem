#include <Arduino.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <Preferences.h>
#include "radio_layout.h"
#include "runtime_stats.h"
#include "protocol.h"
#include <cassert>
#include <iostream>
void setup(); void loop();
namespace OTAManager { extern int ticks; }
static std::vector<uint8_t> frame(uint8_t cmd,const uint8_t* p=nullptr,uint16_t n=0) {
    std::vector<uint8_t> b{PROTO_SYNC,cmd,uint8_t(n),uint8_t(n>>8)};
    if(n)b.insert(b.end(),p,p+n);
    auto crc=crc16_ccitt(b.data()+1,b.size()-1);b.push_back(crc);b.push_back(crc>>8);return b;
}
static void put(const std::shared_ptr<RadioSocketStub>& s,const std::vector<uint8_t>& b){s->input.insert(s->input.end(),b.begin(),b.end());}
static void pump(){for(int i=0;i<6;++i)loop();}
static bool has(const std::vector<uint8_t>& b,uint8_t cmd){for(size_t i=0;i+6<=b.size();){size_t n=b[i+2]|(b[i+3]<<8);assert(i+n+6<=b.size());if(b[i+1]==cmd)return true;i+=n+6;}return false;}
int main(int argc,char** argv) {
    const std::string mode=argc>1?argv[1]:"default";
    unsigned mask=mode=="off"?0:mode=="rf2"?2:mode=="both"?3:1;
    if(mode!="default") preferences["enabled"]={'c',mask};
    const bool fail=mode=="fail";
    if(fail)beginErrors[RadioLayout::at(0).hardware->pin_lora_nss]=-1;
    setup();
    assert(radioListeners[5055].listening && radioListeners[5056].listening);
    const size_t expected=(mask&1?1:0)+(mask&2?1:0);
    assert(chips.size()==expected && "disabled owners must never construct hardware");
    RuntimeStats::RadioSnapshot a{},b{};
    assert(RuntimeStats::captureRadio(0,a)&&RuntimeStats::captureRadio(1,b));
    assert(a.present&&b.present&&a.enabled==bool(mask&1)&&b.enabled==bool(mask&2));
    assert(a.ready==(bool(mask&1)&&!fail)&&b.ready==bool(mask&2));
    for(size_t id=0;id<2;++id) {
        const auto& hw=*RadioLayout::at(id).hardware;
        if(!(mask&(1U<<id))) {
            assert(!chips.count(hw.pin_lora_nss));
            for(size_t j=0;j<hw.static_gpio_count;++j)assert(!gpio.count(hw.static_gpios[j].pin));
        }
    }
    auto one=connectRadio(5055),two=connectRadio(5056);pump();
    const uint8_t token[]={'s','e','c','r','e','t'};
    for(auto s:{one,two})put(s,frame(CMD_AUTH,token,sizeof(token)));
    pump();assert(has(one->output,CMD_AUTH_OK)&&has(two->output,CMD_AUTH_OK));
    RadioConfig cfg{910525000,62500,8,8,18,0x12,16};
    for(size_t id=0;id<2;++id) {
        auto s=id?two:one;
        for(auto cmd:{CMD_SET_CONFIG,CMD_RADIO_RESUME,CMD_RX_START,CMD_TX_REQUEST,CMD_CAD_REQUEST,CMD_SET_FEM_STATE,CMD_SET_RX_BOOST,CMD_SET_AGC_INTERVAL}) {
            if ((mask & (1U << id)) && !(fail && id==0)) continue;
            s->output.clear(); Serial.tx.clear();
            put(s,frame(cmd,reinterpret_cast<const uint8_t*>(&cfg),sizeof(cfg)));pump();
            if(!(mask&(1U<<id))||(fail&&id==0))assert(has(s->output,CMD_ERROR));
            if(id==1)assert(Serial.tx.empty());
        }
        s->output.clear();put(s,frame(CMD_PING));put(s,frame(CMD_GET_WIFI));put(s,frame(CMD_STATUS_REQ));pump();
        assert(has(s->output,CMD_PONG)&&has(s->output,CMD_WIFI_STATUS));
        assert(has(s->output,CMD_STATUS_RESP) && "status telemetry must remain usable with disabled or failed radios");
    }
    auto ping=frame(CMD_PING);Serial.tx.clear();Serial.rx.insert(Serial.rx.end(),ping.begin(),ping.end());pump();
    assert(has(Serial.tx,CMD_PONG));
    if((mask&1)&&!fail) {
        one->output.clear();put(one,frame(CMD_SET_CONFIG,reinterpret_cast<const uint8_t*>(&cfg),sizeof(cfg)));pump();
        assert(has(one->output,CMD_CONFIG_RESP));
        const uint8_t payload[]={1,2};put(one,frame(CMD_TX_REQUEST,payload,sizeof(payload)));pump();
        auto* radio=chips.at(RadioLayout::at(0).hardware->pin_lora_nss);assert(radio->txStarts==1);
        radio->edge(RADIOLIB_SX126X_IRQ_TX_DONE);pump();assert(has(one->output,CMD_TX_DONE));
    }
    for(int i=0;i<20;++i){clockMs+=1000;loop();}
    assert(chips.size()==expected&&OTAManager::ticks>0);
    assert(RuntimeStats::captureRadio(0,a)&&RuntimeStats::captureRadio(1,b));
    if(!(mask&1)||fail)assert(!a.ready&&!a.txReady);
    if(!(mask&2))assert(!b.ready&&!b.txReady);
    std::cout<<"PASS "<<mode<<": real main setup/loop, stable endpoints, command isolation, PONG/device queries, management, RF1-only TX\n";
}
