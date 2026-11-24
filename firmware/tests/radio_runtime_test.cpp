#include "radio_runtime.h"
#include <RadioLib.h>
#include <cassert>
#include <iostream>
struct Message {uint8_t radio,cmd;ResponseRoute route;std::vector<uint8_t> data;};
static std::vector<Message> replies,events;
static uint32_t generations[4]={11,22,33,44};
static bool authenticated[4]={true,true,true,true};
static bool capacity[4]={true,true,true,true};
static bool current(const ResponseRoute& r){return r.radio<4 && (r.transport==TransportSource::TCP?(authenticated[r.radio]&&r.generation==generations[r.radio]):r.radio==0);}
static bool canReply(const ResponseRoute& r){return current(r)&&capacity[r.radio];}
static void reply(const ResponseRoute& r,uint8_t cmd,const uint8_t* p,uint16_t n){assert(current(r));replies.push_back({r.radio,cmd,r,{}});if(n)replies.back().data.assign(p,p+n);}
static void event(uint8_t id,uint8_t cmd,const uint8_t* p,uint16_t n){events.push_back({id,cmd,{}, {}});if(n)events.back().data.assign(p,p+n);}
static std::vector<std::pair<uint8_t,std::string>> diagnostics;
static void diagnostic(uint8_t id,const char* text){diagnostics.push_back({id,text});}
static RadioRuntime::Hooks hooks{current,canReply,reply,event,nullptr,diagnostic};
static ResponseRoute route(uint8_t id){return {TransportSource::TCP,id,generations[id]};}
static BoardConfig board(int nss,int dio,int rst,int busy){BoardConfig b=BOARD;b.has_lora_radio=true;b.pin_lora_nss=nss;b.pin_lora_dio1=dio;b.pin_lora_rst=rst;b.pin_lora_busy=busy;b.rf_switch.en_pin=-1;return b;}
int main(){
 std::cout.setf(std::ios::unitbuf);
 BoardConfig a=board(1,2,3,4),b=board(5,6,7,8);
 RadioDescriptor da{"A",&a,5055,-1},db{"B",&b,5056,-1};
 RadioRuntime one(0,da,hooks),two(1,db,hooks);
 assert(chips.empty()); // lazy: no static/pre-presence Module or SX1262
 assert(one.begin()&&two.begin());
 assert(one.ready()&&two.ready()&&!one.txReady()&&!two.txReady());
 auto* c0=chips.at(1);auto* c1=chips.at(5);assert(c0!=c1&&c0->mod!=c1->mod);
 // A board with no band-specific boot override retains legacy startup/defaults.
 assert(c0->beginFrequency==434.0f&&c1->beginFrequency==434.0f);
 assert(one.config().freq_hz==869618000&&two.config().freq_hz==869618000);
 RadioConfig cfg=one.config();cfg.freq_hz=433775000;
 one.command(CMD_SET_CONFIG,reinterpret_cast<uint8_t*>(&cfg),sizeof(cfg),route(0));
 assert(one.txReady()&&!two.txReady());assert(one.config().freq_hz==433775000);assert(two.config().freq_hz!=433775000);
 c0->receive({1,2});c1->receive({3,4,5});one.serviceIrq();two.serviceIrq();
 assert(one.status().rx_count==1&&two.status().rx_count==1);
 assert(events.size()==2&&events[0].radio==0&&events[1].radio==1);
 assert(events[0].data[6]==1&&events[1].data[6]==3);
 assert(c0->readCount==1&&c1->readCount==1);
 std::cout<<"PASS lazy hardware/config/RX isolation\n";
 two.command(CMD_SET_CONFIG,reinterpret_cast<uint8_t*>(&cfg),sizeof(cfg),route(1));
 uint8_t tx[]={9,8,7};replies.clear();const auto delayBefore=delayCalls;
 one.command(CMD_TX_REQUEST,tx,sizeof(tx),route(0));
 assert(one.busy()&&c0->txStarts==1&&replies.empty());
 assert(delayCalls==delayBefore); // async command must return without polling or sleeping
 one.command(CMD_SET_CONFIG,reinterpret_cast<uint8_t*>(&cfg),sizeof(cfg),route(0));
 assert(replies.back().cmd==CMD_ERROR&&replies.back().data[0]==ERR_RADIO_BUSY);
 one.command(CMD_GET_CONFIG,nullptr,0,route(0));assert(replies.back().cmd==CMD_CONFIG_RESP);
 c1->receive({6});two.serviceIrq();assert(two.status().rx_count==2&&one.busy());
 c0->edge(RADIOLIB_SX126X_IRQ_RX_DONE);one.serviceIrq();
 assert(one.busy()&&one.status().tx_count==0); // stale/wrong-kind edge is not TX_DONE
 c0->edge(RADIOLIB_SX126X_IRQ_TX_DONE);one.serviceIrq();
 assert(!one.busy()&&one.status().tx_count==1&&replies.back().cmd==CMD_TX_DONE);
 assert(replies.back().route.generation==11);
 replies.clear();one.command(CMD_TX_REQUEST,tx,sizeof(tx),route(0));
 ++generations[0];c0->edge(RADIOLIB_SX126X_IRQ_TX_DONE);one.serviceIrq();
 assert(!one.busy()&&replies.empty()); // finish hardware, never answer replacement
 std::cout<<"PASS cooperative TX/wrong IRQ/busy/stale completion isolation\n";
 uint8_t cad[]={2,29,13,0};one.command(CMD_SET_CAD_PARAMS,cad,4,route(0));
 replies.clear();one.command(CMD_CAD_REQUEST,nullptr,0,route(0));
 assert(one.busy()&&c0->scans==1&&c0->lastScan.cad.detPeak==29);
 c1->receive({0xab});two.serviceIrq();assert(two.status().rx_count==3);
 c0->edge(RADIOLIB_SX126X_IRQ_RX_DONE);one.serviceIrq();assert(one.busy());
 c0->edge(RADIOLIB_SX126X_IRQ_CAD_DONE);one.serviceIrq();
 assert(!one.busy()&&replies.back().cmd==CMD_CAD_RESP&&replies.back().data[0]==0);
 one.command(CMD_CAD_REQUEST,nullptr,0,route(0));clockMs+=501;one.serviceIrq();
 assert(!one.busy()&&replies.back().cmd==CMD_ERROR&&replies.back().data[0]==ERR_CAD_FAILED);
 assert(one.ready()&&two.ready());
 uint8_t on=1;one.command(CMD_SET_AUTO_CAD,&on,1,route(0));
 const int txBefore=c0->txStarts,scanBefore=c0->scans;
 one.command(CMD_TX_REQUEST,tx,sizeof(tx),route(0));
 assert(one.busy()&&c0->txStarts==txBefore&&c0->scans==scanBefore+1);
 c0->edge(RADIOLIB_SX126X_IRQ_CAD_DONE|RADIOLIB_SX126X_IRQ_CAD_DETECTED);one.serviceIrq();
 c1->receive({0xcd});two.serviceIrq();assert(two.status().rx_count==4);
 clockMs+=200;one.serviceIrq();assert(c0->scans==scanBefore+2);
 c0->edge(RADIOLIB_SX126X_IRQ_CAD_DONE);one.serviceIrq();
 assert(c0->txStarts==txBefore+1&&one.busy());
 c0->edge(RADIOLIB_SX126X_IRQ_TX_DONE);one.serviceIrq();assert(!one.busy());
 one.command(CMD_TX_REQUEST,tx,sizeof(tx),route(0));++generations[0];
 c0->edge(RADIOLIB_SX126X_IRQ_CAD_DONE);one.serviceIrq();assert(!one.busy()&&c0->txStarts==txBefore+1);
 assert(delayCalls==delayBefore);
 std::cout<<"PASS cooperative explicit/custom/automatic CAD and backoff\n";
 uint8_t fem[]={FEM_STATE_TX_PA,FEM_STATE_TX_PA};uint8_t boost=1;
 c0->edge(RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED);replies.clear();
 one.command(CMD_SET_FEM_STATE,fem,2,route(0));
 one.command(CMD_GET_RX_BOOST,nullptr,0,route(0));assert(replies.empty());
 assert(!one.frontEnd().isPaHighPowerEnabled());
 ++generations[0];c0->irq=0;one.service();
 assert(replies.empty()&&!one.frontEnd().isPaHighPowerEnabled());
 c0->edge(RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED);
 one.command(CMD_SET_FEM_STATE,fem,2,route(0));
 one.command(CMD_SET_RX_BOOST,&boost,1,route(0));
 one.command(CMD_GET_FEM_STATE,nullptr,0,route(0));
 one.command(CMD_GET_RX_BOOST,nullptr,0,route(0));
 one.command(CMD_GET_RX_BOOST,nullptr,0,route(0));assert(replies.back().data[0]==ERR_RADIO_BUSY);
 c0->irq=0;capacity[0]=false;one.service();assert(!one.frontEnd().isPaHighPowerEnabled());
 capacity[0]=true;one.service();
 assert(one.frontEnd().isPaHighPowerEnabled()&&!two.frontEnd().isPaHighPowerEnabled());
 assert(c0->boost&&!c1->boost&&replies.back().cmd==CMD_RX_BOOST_RESP);
 // Failed gain writes roll back; failed rollbacks and RX restart are faults.
 c0->failures["boost"]={-1,0};boost=0;one.command(CMD_SET_RX_BOOST,&boost,1,route(0));
 assert(c0->boost&&one.ready()&&replies.back().data[0]==ERR_RADIO_INIT);
 c0->failures["boost"]={-1,-1};one.command(CMD_SET_RX_BOOST,&boost,1,route(0));
 assert(!one.txReady()&&one.status().radio_state==2&&replies.back().data[0]==ERR_RADIO_FAULT);
 one.command(CMD_SET_RX_BOOST,&boost,1,route(0));assert(one.ready()&&two.ready());
 std::cout<<"PASS per-owner deferred FIFO/stale-before-mutation/backpressure/boost recovery\n";
 const int rxBefore=c0->rxStarts;
 one.command(CMD_RADIO_STANDBY,nullptr,0,route(0));assert(one.standby());
 c0->receive({1});one.serviceIrq();assert(c0->rxStarts==rxBefore);
 one.command(CMD_RX_START,nullptr,0,route(0));assert(c0->rxStarts==rxBefore);
 one.command(CMD_RADIO_RESUME,nullptr,0,route(0));assert(!one.standby()&&c0->rxStarts==rxBefore+1);
 for(const char* method:{"standby","frequency","bandwidth","sf","cr","power","sync","preamble","header","crc","iq","ldro"}){
   one.command(CMD_SET_CONFIG,reinterpret_cast<uint8_t*>(&cfg),sizeof(cfg),route(0));assert(one.txReady());
   c0->failures[method]={-1};
   one.command(CMD_SET_CONFIG,reinterpret_cast<uint8_t*>(&cfg),sizeof(cfg),route(0));
   assert(!one.txReady());one.command(CMD_TX_REQUEST,tx,3,route(0));assert(replies.back().data[0]==ERR_INVALID_CONFIG);
 }
 one.command(CMD_SET_CONFIG,reinterpret_cast<uint8_t*>(&cfg),sizeof(cfg),route(0));
 one.command(CMD_SET_CONFIG,reinterpret_cast<uint8_t*>(&cfg),sizeof(cfg)-1,route(0));
 assert(!one.txReady());
 {BoardConfig absent=board(9,10,11,12);absent.has_lora_radio=false;RadioDescriptor d{"none",&absent,5057,-1};
  RadioRuntime none(2,d,hooks);assert(!none.begin()&&!chips.count(9));
  for(uint8_t cmd:{CMD_RADIO_STANDBY,CMD_RADIO_RESUME,CMD_SET_AUTO_CAD,CMD_RX_START}){none.command(cmd,&on,1,route(2));assert(replies.back().data[0]==ERR_NO_RADIO);}}
 {BoardConfig failed=board(9,10,11,12);RadioDescriptor d{"fail",&failed,5057,-1};beginErrors[9]=-1;
  RadioRuntime bad(2,d,hooks);assert(!bad.begin()&&!bad.ready());c1->receive({8});two.serviceIrq();assert(two.ready());beginErrors[9]=0;}
 std::cout<<"PASS standby/malformed/reapply-failure/no-radio/init-failure isolation\n";
 one.command(CMD_SET_CONFIG,reinterpret_cast<uint8_t*>(&cfg),sizeof(cfg),route(0));
 uint8_t interval[]={1,0};one.command(CMD_SET_AGC_INTERVAL,interval,2,route(0));
 assert(replies.back().cmd==CMD_AGC_INTERVAL_RESP&&one.frontEnd().getAgcResetIntervalSec()==1);
 assert(two.frontEnd().getAgcResetIntervalSec()!=1);
 // Establish changed-interval scheduling, then exercise the full production maintenance.
 one.maintenance();const int agcBefore=c0->agc;clockMs+=11000;one.maintenance();
 assert(c0->agc==agcBefore+1&&c1->agc==0&&one.agcResetCount()==1);
 c0->ambient=-116;c1->ambient=-97;
 for(int n=0;n<20;++n){clockMs+=11;one.maintenance();two.maintenance();}
 assert(one.status().noise_floor_x10==-1160&&two.status().noise_floor_x10==-970);
 assert(c0->ambient!=c0->packetRssi);
 c0->failures["agc"]={-1};clockMs+=1100;one.maintenance();assert(!one.txReady()&&one.status().radio_state==2&&two.ready());
 one.command(CMD_GET_RF_CAPS,nullptr,0,route(0));assert(replies.back().cmd==CMD_RF_CAPS_RESP);
 one.command(CMD_NOISE_REQ,nullptr,0,route(0));assert(replies.back().cmd==CMD_NOISE_RESP);
 std::cout<<"PASS independent maintenance/AGC fault/noise/capability queries\n";
 const int irq0=c0->irqReads,irq1=c1->irqReads;one.serviceIrq();two.serviceIrq();
 assert(c0->irqReads==irq0&&c1->irqReads==irq1);
 for(const char* method:{"currentLimit","boost","patch"}){
   BoardConfig option=board(9,10,11,12);option.sx126x_current_limit_ma=140;option.sx126x_rx_boosted_gain=true;option.sx126x_register_patch=true;
   RadioDescriptor d{"options",&option,5057,-1};initialFailures[9][method]={-1};
   RadioRuntime bad(2,d,hooks);assert(!bad.begin());
   bad.command(CMD_SET_CONFIG,(const uint8_t*)&cfg,sizeof(cfg),route(2));
   bad.command(CMD_TX_REQUEST,tx,3,route(2));assert(!bad.txReady()&&chips.at(9)->txStarts==0);
   initialFailures[9].clear();
 }
 std::cout<<"PASS idle IRQ budget and mandatory bringup failure cannot revive TX\n";
 {BoardConfig bus=board(9,10,11,12);RadioDescriptor d{"bus",&bus,5057,1};spiBeginFailures[HSPI]=true;
  auto* hw=RadioHardware::create(d);assert(!hw&&!chips.count(9));
  spiBeginFailures[HSPI]=false;hw=RadioHardware::create(d);assert(hw&&chips.count(9));delete hw;}
 std::cout<<"PASS failed SPI begin is not constructed/cached\n";
 uint8_t off=0;two.command(CMD_SET_AUTO_CAD,&off,1,route(1));replies.clear();
 two.command(CMD_TX_REQUEST,tx,3,route(1));capacity[1]=false;
 c1->edge(RADIOLIB_SX126X_IRQ_TX_DONE);two.serviceIrq();assert(replies.empty());
 capacity[1]=true;two.service();assert(replies.size()==1&&replies.back().cmd==CMD_TX_DONE&&replies.back().radio==1);
 replies.clear();two.command(CMD_CAD_REQUEST,nullptr,0,route(1));capacity[1]=false;
 c1->edge(RADIOLIB_SX126X_IRQ_CAD_DONE);two.serviceIrq();assert(replies.empty());
 ++generations[1];capacity[1]=true;two.service();assert(replies.empty());
 std::cout<<"PASS asynchronous completion backpressure and stale queued reply cancellation\n";
 diagnostics.clear();c1->failures["power"]={-9};
 c1->before=[&](const std::string& name){if(name=="standby")assert(!diagnostics.empty()&&diagnostics[0].second.find("SET_CONFIG")!=std::string::npos);};
 two.command(CMD_SET_CONFIG,(const uint8_t*)&cfg,sizeof(cfg),route(1));c1->before={};
 assert(diagnostics.size()==2&&diagnostics[0].first==1&&diagnostics[1].first==1);
 assert(diagnostics[1].second.find("setOutputPower=-9")!=std::string::npos);
 assert(diagnostics[1].second.find("ocp_before=140mA")!=std::string::npos);
 std::cout<<"PASS exact-owner requested-config and power/OCP diagnostics\n";
 two.command(CMD_SET_CONFIG,(const uint8_t*)&cfg,sizeof(cfg),route(1));
 two.command(CMD_CAD_REQUEST,nullptr,0,route(1));replies.clear();c1->failures["rx"]={-1};
 c1->edge(RADIOLIB_SX126X_IRQ_CAD_DONE);two.serviceIrq();
 assert(replies.size()==1&&replies[0].cmd==CMD_ERROR&&replies[0].data[0]==ERR_RADIO_FAULT&&!two.txReady());
 std::cout<<"PASS CAD rearm failure never reports clear\n";
 {BoardConfig boot=board(9,10,11,12);RadioDescriptor d{"receive-only",&boot,5057,-1};RadioRuntime third(2,d,hooks);
  assert(third.begin());third.command(CMD_CAD_REQUEST,nullptr,0,route(2));clockMs+=501;third.serviceIrq();
  assert(!third.txReady());third.command(CMD_TX_REQUEST,tx,3,route(2));assert(chips.at(9)->txStarts==0);}
 {BoardConfig boot=board(9,10,11,12);RadioDescriptor d{"config-fail",&boot,5057,-1};initialFailures[9]["frequency"]={-1};RadioRuntime third(2,d,hooks);
  assert(!third.begin());initialFailures[9].clear();
  third.command(CMD_SET_CONFIG,(const uint8_t*)&cfg,sizeof(cfg),route(2));assert(third.txReady());
  chips.at(9)->receive({0x42});third.serviceIrq();assert(third.status().rx_count==1);
  chips.at(9)->failures["frequency"]={-1};third.command(CMD_SET_CONFIG,(const uint8_t*)&cfg,sizeof(cfg),route(2));
  third.command(CMD_RX_START,nullptr,0,route(2));assert(!third.ready()&&!third.txReady());}
 std::cout<<"PASS recovery keeps receive-only admission and IRQ binding/config validity\n";
 {BoardConfig test=board(9,10,11,12);RadioDescriptor d{"guard",&test,5057,-1};RadioRuntime third(2,d,hooks);assert(third.begin());
  third.command(CMD_SET_CONFIG,(const uint8_t*)&cfg,sizeof(cfg),route(2));
  auto* c2=chips.at(9);c2->edge(RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED);
  third.command(CMD_TX_REQUEST,tx,3,route(2));clockMs+=1000;c2->failures["clear"]={-1};
  third.command(CMD_TX_REQUEST,tx,3,route(2));assert(c2->txStarts==0&&!third.txReady());}
 {BoardConfig test=board(9,10,11,12);RadioDescriptor d{"web-arbitration",&test,5057,-1};RadioRuntime third(2,d,hooks);assert(third.begin());
  third.command(CMD_SET_CONFIG,(const uint8_t*)&cfg,sizeof(cfg),route(2));third.command(CMD_TX_REQUEST,tx,3,route(2));
  assert(!third.frontEnd().setStationG3RfConfig(true,false,false));
  assert(!third.frontEnd().setAgcResetIntervalSec(27,false));
  auto* c2=chips.at(9);c2->edge(RADIOLIB_SX126X_IRQ_TX_DONE);third.serviceIrq();
  assert(third.frontEnd().setStationG3RfConfig(true,false,false));}
 std::cout<<"PASS receive-guard hardware failure blocks TX and primary facade uses owner arbitration\n";
 {BoardConfig test=board(9,10,11,12);RadioDescriptor d{"standby-CAD",&test,5057,-1};RadioRuntime third(2,d,hooks);assert(third.begin());
  third.command(CMD_SET_CONFIG,(const uint8_t*)&cfg,sizeof(cfg),route(2));third.command(CMD_RADIO_STANDBY,nullptr,0,route(2));
  third.command(CMD_CAD_REQUEST,nullptr,0,route(2));auto* c2=chips.at(9);int starts=c2->rxStarts;
  c2->edge(RADIOLIB_SX126X_IRQ_CAD_DONE);third.serviceIrq();assert(third.standby()&&c2->rxStarts==starts);
  assert(gpio[test.rf_frontend.lna_mode_pin]==HIGH);}
 std::cout<<"PASS standby CAD restores frontend bypass without RX\n";
 {BoardConfig test=board(9,10,11,12);RadioDescriptor d{"timeout",&test,5057,-1};RadioRuntime third(2,d,hooks);assert(third.begin());
  third.command(CMD_SET_CONFIG,(const uint8_t*)&cfg,sizeof(cfg),route(2));third.command(CMD_TX_REQUEST,tx,3,route(2));
  chips.at(9)->failures["finishTx"]={-1};clockMs+=4501;third.serviceIrq();assert(!third.txReady()&&third.status().radio_state==2);}
 std::cout<<"PASS TX timeout finish failure remains a fault\n";
}
