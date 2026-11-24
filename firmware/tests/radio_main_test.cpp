#include <Arduino.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <WiFi.h>
#include "board_config.h"
#include "radio_layout.h"
#include "runtime_stats.h"
#include "protocol.h"
#include <cassert>
#include <iostream>
void setup();void loop();
namespace BatteryMonitor {extern int reads;}
static std::vector<uint8_t> frame(uint8_t c,const uint8_t* p=nullptr,uint16_t n=0){
 std::vector<uint8_t>b{PROTO_SYNC,c,uint8_t(n),uint8_t(n>>8)};if(n)b.insert(b.end(),p,p+n);auto crc=crc16_ccitt(b.data()+1,b.size()-1);b.push_back(crc);b.push_back(crc>>8);return b;}
static void put(const std::shared_ptr<RadioSocketStub>& s,const std::vector<uint8_t>& b){s->input.insert(s->input.end(),b.begin(),b.end());}
static void pump(int n=4){while(n--)loop();}
static bool has(const std::vector<uint8_t>& b,uint8_t c){size_t i=0;while(i+6<=b.size()){assert(b[i]==PROTO_SYNC);size_t n=b[i+2]|(b[i+3]<<8);assert(i+n+6<=b.size());if(b[i+1]==c)return true;i+=n+6;}return false;}
int main(){
 std::cout.setf(std::ios::unitbuf);
 preferences["enabled"]={'c',3}; // This fixture exercises explicitly enabled dual operation.
 assert(RadioLayout::count()==2);setup();
 assert(radioListeners[5055].listening&&radioListeners[5056].listening);
 assert(chips.size()==2); // setup must construct *actual* declared owners
 auto a=connectRadio(5055),b=connectRadio(5056);pump();
 auto* c0=chips.at(RadioLayout::at(0).hardware->pin_lora_nss);
 auto* c1=chips.at(RadioLayout::at(1).hardware->pin_lora_nss);
 Serial.tx.clear();a->output.clear();b->output.clear();
 c1->receive({1});pump();assert(b->output.empty()&&Serial.tx.empty()); // unauth RF2 never USB
 const uint8_t token[]={'s','e','c','r','e','t'};put(a,frame(CMD_AUTH,token,6));put(b,frame(CMD_AUTH,token,6));pump();
 assert(has(a->output,CMD_AUTH_OK)&&has(b->output,CMD_AUTH_OK));
 RadioConfig cfg{433775000,125000,7,5,10,0x12,16};
 put(a,frame(CMD_SET_CONFIG,(const uint8_t*)&cfg,sizeof(cfg)));cfg.freq_hz=868125000;
 put(b,frame(CMD_SET_CONFIG,(const uint8_t*)&cfg,sizeof(cfg)));pump();
 RuntimeStats::RadioSnapshot ra,rb;assert(RuntimeStats::captureRadio(0,ra)&&RuntimeStats::captureRadio(1,rb));
 assert(ra.radio.freq_hz==433775000&&rb.radio.freq_hz==868125000&&ra.txReady&&rb.txReady);
 assert(ra.tcpPort==5055&&rb.tcpPort==5056);
 a->output.clear();b->output.clear();Serial.tx.clear();
 uint8_t payload[]={0x91,0x92};put(a,frame(CMD_TX_REQUEST,payload,2));pump();assert(c0->txStarts==1);
 c1->receive({0xa5,0x5a});pump();assert(has(b->output,CMD_RX_PACKET)&&!has(a->output,CMD_RX_PACKET)&&Serial.tx.empty());
 // A saturated serial link cannot stop pending TX or the other radio.
 auto ping=frame(CMD_PING);for(int i=0;i<100;++i)Serial.rx.insert(Serial.rx.end(),ping.begin(),ping.end());
 size_t before=Serial.rx.size();loop();assert(before-Serial.rx.size()<=256&&before-Serial.rx.size()==ping.size());
 assert(has(Serial.tx,CMD_PONG));
 c0->edge(RADIOLIB_SX126X_IRQ_TX_DONE);pump();assert(has(a->output,CMD_TX_DONE)&&!has(b->output,CMD_TX_DONE));
 b->output.clear();put(b,frame(CMD_GET_WIFI));pump();assert(has(b->output,CMD_WIFI_STATUS));
 auto& out=b->output;assert((out[9]|(out[10]<<8))==5056);
 int reads=BatteryMonitor::reads;
 auto primary=RuntimeStats::capture();assert(primary.radio.freq_hz==ra.radio.freq_hz&&primary.status.battery_mv==3981);
 RuntimeStats::captureRadio(0,ra);RuntimeStats::captureRadio(1,rb);RuntimeStats::capture();
 assert(BatteryMonitor::reads-reads<=1); // one shared device sample, not repeated I2C/ADC per owner/loop
 std::cout<<"PASS full production main setup/loop + real multiport transport/runtime, auth and primary-only serial\n";
}
