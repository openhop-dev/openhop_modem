#include <Arduino.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <ETH.h>
#include "protocol.h"
#include "radio_layout.h"
#include "radio_settings.h"
#include "runtime_stats.h"
#include "ethernet_manager.h"
#include <cassert>
#include <iostream>
void setup();void loop();
namespace WifiManager { extern int beginCalls; }
namespace OTAManager { extern int beginCalls,validMarks;extern uint32_t healthStart; }
static void pump(int n=4){while(n--)loop();}
static std::vector<uint8_t> frame(uint8_t cmd,const uint8_t* data=nullptr,uint16_t len=0){
 std::vector<uint8_t> out{PROTO_SYNC,cmd,uint8_t(len),uint8_t(len>>8)};
 if(len)out.insert(out.end(),data,data+len);
 const auto crc=crc16_ccitt(out.data()+1,out.size()-1);out.push_back(crc);out.push_back(crc>>8);return out;
}
static bool has(const std::vector<uint8_t>& data,uint8_t cmd){
 for(size_t p=0;p+6<=data.size();){assert(data[p]==PROTO_SYNC);size_t len=data[p+2]|(data[p+3]<<8);assert(p+len+6<=data.size());if(data[p+1]==cmd)return true;p+=len+6;}return false;
}
static void hostQuery(unsigned slot){
 auto client=connectRadio(5055+slot);pump();
 const uint8_t token[]={'s','e','c','r','e','t'};auto bytes=frame(CMD_AUTH,token,sizeof(token));
 client->input.insert(client->input.end(),bytes.begin(),bytes.end());pump();assert(has(client->output,CMD_AUTH_OK));
 client->output.clear();bytes=frame(CMD_PING);client->input.insert(client->input.end(),bytes.begin(),bytes.end());pump();assert(has(client->output,CMD_PONG));
}
int main(int argc,char** argv){
 std::cout.setf(std::ios::unitbuf);assert(argc==2);const std::string scenario=argv[1];
 const unsigned high=1U<<(NRADIO_COUNT-1);
 const bool off=scenario=="all_off"||scenario=="late_link_off";
 const unsigned mask=off?0U:scenario=="sparse_no_host"?(1U|high):high;
 preferences["enabled"]={'c',mask};
 if(scenario=="failed_high_no_host")beginErrors[RadioLayout::at(NRADIO_COUNT-1).hardware->pin_lora_nss]=-1;
 ETH.cable=scenario!="late_link_off";ETH.leaseReady=scenario!="late_dhcp_high";
 setup();assert(!BOARD.has_wifi&&RadioLayout::count()==NRADIO_COUNT&&RadioSettings::activeMask()==mask);
 assert(ETH.begins==1&&ETH.active&&ETH.ends==0&&WifiManager::beginCalls==0);
 if(scenario=="late_link_off"||scenario=="late_dhcp_high"){
  assert(!EthernetManager::hasIP()&&radioListeners.empty()&&OTAManager::beginCalls==0);
  // No protocol traffic before the no-frame health gate.
  ETH.cable=true;ETH.leaseReady=true;
  for(int i=0;i<30;++i){clockMs+=1000;pump();}
 }
 assert(EthernetManager::hasIP()&&OTAManager::beginCalls==1&&ETH.ends==0);
 for(unsigned i=0;i<NRADIO_COUNT;++i){
  assert(radioListeners[5055+i].listening);
  RuntimeStats::RadioSnapshot s{};assert(RuntimeStats::captureRadio(i,s));
  assert(s.enabled==bool(mask&(1U<<i)));
  assert(s.ready==(s.enabled&&!(scenario=="failed_high_no_host"&&i==NRADIO_COUNT-1)));
  assert(!s.txReady);
 }
 const auto start=OTAManager::healthStart;
 assert(OTAManager::validMarks==0);
 clockMs=start+60000U;pump();assert(OTAManager::validMarks==0);
 clockMs=start+119999U;pump();assert(OTAManager::validMarks==0&&"120-second healthy-time gate must not be shortened");
 clockMs=start+120000U;pump();
 assert(OTAManager::validMarks==(off?1:0)&&"only explicit all-off may accept OTA without a host frame");
 if(!off){
  clockMs+=30000U;pump();assert(OTAManager::validMarks==0&&"any enabled owner requires a valid host frame, even failed bring-up");
  hostQuery(NRADIO_COUNT-1);pump();assert(OTAManager::validMarks==1);
 }
 pump();assert(OTAManager::validMarks==1);
 for(const auto& c:chips)assert(c.second->txStarts==0);
 std::cout<<"PASS N="<<NRADIO_COUNT<<" "<<scenario<<": real setup/loop/Ethernet, actual attemptMarkValid, unchanged120s, no TX\n";
}
