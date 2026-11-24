#include "radio_runtime.h"
#include <cassert>
#include <iostream>
struct Message { uint8_t cmd; std::vector<uint8_t> data; };
static std::vector<Message> replies;
static bool current(const ResponseRoute&) { return true; }
static bool capacity(const ResponseRoute&) { return true; }
static void reply(const ResponseRoute&,uint8_t cmd,const uint8_t* p,uint16_t n) {
 replies.push_back({cmd,{}});if(n)replies.back().data.assign(p,p+n);
}
static void event(uint8_t,uint8_t,const uint8_t*,uint16_t) {}
static const RadioRuntime::Hooks hooks{current,capacity,reply,event,nullptr,nullptr};
static BoardConfig board(int n,int d,int r,int busy) {
 BoardConfig b=BOARD;b.has_lora_radio=true;b.pin_lora_nss=n;b.pin_lora_dio1=d;
 b.pin_lora_rst=r;b.pin_lora_busy=busy;b.rf_switch.en_pin=-1;return b;
}
static const ResponseRoute route{TransportSource::TCP,0,1};
struct Fixture {
 BoardConfig a=board(1,2,3,4),b=board(5,6,7,8);
 RadioDescriptor da{"A",&a,5055,-1},db{"B",&b,5056,-1};
 RadioRuntime one{0,da,hooks},two{1,db,hooks};
 SX1262 *chip,*peer;
 Fixture() {
  replies.clear();assert(one.begin()&&two.begin());chip=chips.at(1);peer=chips.at(5);
  auto config=one.config();config.power_dbm=18;
  one.command(CMD_SET_CONFIG,reinterpret_cast<const uint8_t*>(&config),sizeof(config),route);
  assert(one.txReady());replies.clear();
 }
 void tx() { const uint8_t p[]={1,2,3};one.command(CMD_TX_REQUEST,p,sizeof(p),route);assert(one.busy()); }
};
int main() {
 {
  Fixture f;auto& chip=static_cast<OpenHopSX1262&>(*f.chip);
  auto parser=f.chip->mod->spiConfig.parseStatusCb;
  for(const auto status:{0x71,0xff,0x26,0x00,0x12}) {
   f.chip->irqStatus=status;f.chip->irqDataOverride=1;
   auto sample=chip.readIrqChecked();assert(!sample.valid&&sample.spiResult!=0);
   assert(f.chip->mod->spiConfig.parseStatusCb==parser);
  }
  // Original hardware RX: status D4, RX_DONE+HEADER+PREAMBLE=0016.
  // RFU status bits need not be zero; native errors and all IRQ bits remain checked.
  for(const auto status:{0xa4,0xd4,0xe4}) {
   f.chip->irqStatus=status;f.chip->irqDataOverride=0x16;
   const auto sample=chip.readIrqChecked();assert(sample.valid&&sample.spiResult==0&&sample.flags==0x16);
   assert(f.chip->mod->spiConfig.parseStatusCb==parser);
  }
  f.chip->irqStatus=0x24;
  for(const auto flags:{0x7171,0x4001,0xffff}) {
   f.chip->irqDataOverride=flags;auto sample=chip.readIrqChecked();assert(!sample.valid&&sample.spiResult==0);
  }
  f.chip->irqDataOverride=0;assert(chip.readIrqChecked().valid);
  f.chip->irqDataOverride=-1;
  f.chip->dioOverride=1;assert(chip.dio1Asserted());
  assert(!static_cast<OpenHopSX1262&>(*f.peer).dio1Asserted());
 }
 std::cout<<"PASS checked original transaction, strict status, LoRa bitmap, parser restoration and per-owner DIO\n";
 {
  Fixture f;f.tx();const auto reads=f.chip->irqReads;
  f.chip->irqStatus=0x71;f.chip->irqDataOverride=0x7171;
  f.one.serviceIrq();assert(f.one.busy()&&replies.empty()&&f.chip->irqReads==reads);
  f.chip->dioOverride=1;f.one.onIrq();f.one.serviceIrq();
  assert(f.one.busy()&&replies.empty()&&f.one.status().tx_count==0);
  f.peer->receive({4,5});f.two.serviceIrq();assert(f.two.status().rx_count==1);
  f.chip->irqStatus=0x24;f.chip->irqDataOverride=-1;f.chip->dioOverride=-1;
  f.chip->irq=RADIOLIB_SX126X_IRQ_TX_DONE; // latched DIO, deliberately no ISR
  f.one.serviceIrq();assert(!f.one.busy()&&f.one.status().tx_count==1&&replies.back().cmd==CMD_TX_DONE);
 }
 std::cout<<"PASS no on-air polling, corrupt/stale edge rejected, lost-edge completion and peer progress\n";
 {
  Fixture f;const auto starts=f.chip->rxStarts;f.chip->irqStatus=0xd4;f.chip->receive({1,2});
  f.chip->failures["irq"]={-705};f.one.serviceIrq();
  assert(f.one.status().rx_count==0&&f.chip->readCount==0&&f.chip->rxStarts==starts);
  assert(f.chip->irq==RADIOLIB_SX126X_IRQ_RX_DONE);
  f.one.serviceIrq();assert(f.one.status().rx_count==1&&f.chip->readCount==1);
  f.one.serviceIrq();assert(f.one.status().rx_count==1);
 }
 std::cout<<"PASS failed RX read retains packet/notification and retries without a new edge\n";
 {
  Fixture f;bool fired=false;f.one.onIrq();
  f.chip->beforeIrqRead=[&] {if(!fired){fired=true;f.one.onIrq();}};
  const auto reads=f.chip->irqReads;f.one.serviceIrq();f.chip->beforeIrqRead=nullptr;
  f.one.serviceIrq();assert(f.chip->irqReads==reads+2);
  f.one.serviceIrq();assert(f.chip->irqReads==reads+2);
 }
 std::cout<<"PASS edge arriving during SPI is retained and idle reads remain bounded\n";
 {
  Fixture f;f.chip->irqStatus=0x71;f.chip->irqDataOverride=0;
  const uint8_t p[]={1};f.one.command(CMD_TX_REQUEST,p,1,route);
  assert(f.chip->txStarts==0&&!replies.empty()&&replies.back().cmd==CMD_ERROR);
  f.one.command(CMD_CAD_REQUEST,nullptr,0,route);assert(f.chip->scans==0);
  f.chip->irqStatus=0x24;f.chip->irqDataOverride=-1;
  f.chip->irq=RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED;
  assert(!static_cast<OpenHopSX1262&>(*f.chip).dio1Asserted());
  f.one.command(CMD_TX_REQUEST,p,1,route);assert(f.chip->txStarts==0);
  clockMs+=10000;f.chip->irqStatus=0x71;
  f.one.command(CMD_TX_REQUEST,p,1,route);assert(f.chip->txStarts==0);
 }
 std::cout<<"PASS receive guard polls preamble without DIO and fails closed on unknown\n";
 for(int kind=0;kind<3;++kind)for(bool invalid:{false,true}) {
  Fixture f;uint32_t deadline;
  if(kind==0){f.tx();deadline=4500;}
  else if(kind==1){f.one.command(CMD_CAD_REQUEST,nullptr,0,route);deadline=500;}
  else {uint8_t on=1;f.one.command(CMD_SET_AUTO_CAD,&on,1,route);replies.clear();f.tx();deadline=200;}
  if(invalid){f.chip->dioOverride=1;f.chip->irqStatus=0x71;f.chip->irqDataOverride=0x7171;}
  clockMs+=deadline+1;f.one.serviceIrq();assert(!f.one.busy()&&!replies.empty());
  assert(replies.back().cmd==CMD_ERROR&&f.one.status().tx_count==0);
 }
 std::cout<<"PASS original TX/CAD/AutoCAD timeouts with no edge and with persistent invalid reads\n";
 {
  Fixture f;uint8_t on=1;f.one.command(CMD_SET_AUTO_CAD,&on,1,route);replies.clear();f.tx();
  f.chip->edge(RADIOLIB_SX126X_IRQ_CAD_DONE|RADIOLIB_SX126X_IRQ_CAD_DETECTED);f.one.serviceIrq();
  const auto scans=f.chip->scans;clockMs+=201;f.chip->irqStatus=0x71;f.chip->irqDataOverride=0x7171;
  f.one.serviceIrq();assert(!f.one.busy()&&f.chip->scans==scans&&f.chip->txStarts==0&&replies.back().cmd==CMD_ERROR);
 }
 {
  Fixture f;clockMs=0xfffffff0U;f.tx();clockMs+=4501;f.one.serviceIrq();
  assert(!f.one.busy()&&replies.back().cmd==CMD_ERROR&&replies.back().data[0]==ERR_TX_TIMEOUT);
 }
 std::cout<<"PASS Backoff advances but cannot transmit on unknown RX, and deadline arithmetic wraps\n";
}
