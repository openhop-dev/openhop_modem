#pragma once
#include "SPI.h"
#include <functional>
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_ERR_SPI_CMD_TIMEOUT -705
#define RADIOLIB_ERR_SPI_CMD_INVALID -706
#define RADIOLIB_ERR_SPI_CMD_FAILED -707
#define RADIOLIB_NC 0xffffffffUL
#define RADIOLIB_SX126X_RX_TIMEOUT_INF 0xffffff
#define RADIOLIB_IRQ_PREAMBLE_DETECTED 2
#define RADIOLIB_IRQ_RX_DEFAULT_FLAGS 0x23
#define RADIOLIB_IRQ_RX_DEFAULT_MASK 0x23
#define RADIOLIB_IRQ_CAD_DEFAULT_FLAGS 0x180
#define RADIOLIB_IRQ_CAD_DEFAULT_MASK 0x180
#define RADIOLIB_SX126X_IRQ_TX_DONE 0x01
#define RADIOLIB_SX126X_IRQ_RX_DONE 0x02
#define RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED 0x04
#define RADIOLIB_SX126X_IRQ_HEADER_VALID 0x10
#define RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID 0x08
#define RADIOLIB_SX126X_IRQ_HEADER_ERR 0x20
#define RADIOLIB_SX126X_IRQ_TIMEOUT 0x200
#define RADIOLIB_SX126X_CMD_GET_IRQ_STATUS 0x12
#define RADIOLIB_SX126X_STATUS_MODE_STDBY_RC 0x20
#define RADIOLIB_SX126X_STATUS_MODE_STDBY_XOSC 0x30
#define RADIOLIB_SX126X_STATUS_MODE_FS 0x40
#define RADIOLIB_SX126X_STATUS_MODE_RX 0x50
#define RADIOLIB_SX126X_STATUS_MODE_TX 0x60
#define RADIOLIB_SX126X_IRQ_CRC_ERR 0x40
#define RADIOLIB_SX126X_IRQ_CAD_DONE 0x80
#define RADIOLIB_SX126X_IRQ_CAD_DETECTED 0x100
#define RADIOLIB_SX126X_IRQ_ALL 0x43ff
#define RADIOLIB_SX126X_PA_RAMP_1700U 6
#define RADIOLIB_SX126X_SYNC_WORD_PRIVATE 0x12
struct ChannelScanConfig_t {struct {uint8_t symNum=0,detPeak=0,detMin=0,exitMode=0;uint32_t timeout=0,irqFlags=0,irqMask=0;} cad;};
struct RadioLibHal {
 const uint32_t GpioLevelHigh=1;
 uint32_t digitalRead(uint32_t pin);
};
class Module {
public:
 using SPIparseStatusCb_t=int16_t(*)(uint8_t);
 struct { SPIparseStatusCb_t parseStatusCb=nullptr; } spiConfig;
 int nss,dio,rst,busy; SPIClass* spi;
 RadioLibHal defaultHal; RadioLibHal* hal=&defaultHal;
 Module(int n,int d,int r,int b,SPIClass& s=SPI):nss(n),dio(d),rst(r),busy(b),spi(&s){}
 uint32_t getIrq() const { return dio; }
 int16_t SPIreadStream(uint16_t command,uint8_t* data,size_t length,bool=true,bool=true);
};
class SX1262;
inline std::map<int,SX1262*> chips;
inline std::map<int,int> beginErrors;
inline std::map<int,std::map<std::string,std::deque<int>>> initialFailures;
class SX126x {
public:
 static int16_t SPIparseStatus(uint8_t value) {
  switch(value&0x0e) {case 6:return -705;case 8:return -706;case 10:return -707;}
  return value==0 || value==255 ? -2 : 0;
 }
};
class SX1262 : public SX126x {
public:
 Module* mod; uint32_t irq=0; void(*action)()=nullptr;
 uint32_t dioMask=RADIOLIB_SX126X_IRQ_RX_DONE;
 int dioOverride=-1, irqDataOverride=-1;
 uint8_t irqStatus=0x24;
 std::function<void()> beforeIrqRead;
 std::map<std::string,std::deque<int>> failures;
 std::vector<std::string> calls;
 std::vector<uint8_t> incoming,transmitted;
 float freq=0,bw=0,ambient=-110,packetRssi=-61,snr=4.5,tcxo=0,beginFrequency=0;
 int beginPower=0;
 int sf=0,cr=0,power=0,rxStarts=0,txStarts=0,scans=0,agc=0,readCount=0,irqReads=0;
 bool boost=false;
 ChannelScanConfig_t lastScan;
 std::function<void(const std::string&)> before;
 SX1262(Module* m):mod(m){chips[m->nss]=this;failures=initialFailures[m->nss];m->spiConfig.parseStatusCb=SPIparseStatus;}
 Module* getMod() { return mod; }
 virtual ~SX1262(){chips.erase(mod->nss);}
 int step(const char* name){calls.push_back(name);if(before)before(name);auto& q=failures[name];if(q.empty())return 0;int v=q.front();q.pop_front();return v;}
 int begin(float f,float,uint8_t,uint8_t,uint8_t,int8_t p,uint16_t,float t){beginFrequency=f;beginPower=p;tcxo=t;return beginErrors[mod->nss];}
 int standby(){int result=step("standby");if(!result)dioMask=0;return result;}
 int setFrequency(float v){freq=v;return step("frequency");}
 int setBandwidth(float v){bw=v;return step("bandwidth");}
 int setSpreadingFactor(int v){sf=v;return step("sf");}
 int setCodingRate(int v){cr=v;return step("cr");}
 int setOutputPower(int v){power=v;return step("power");}
 int setPaRampTime(int){return step("ramp");}
 int getCurrentLimit(){return 140;}
 int setCurrentLimit(int){return step("currentLimit");}
 int setDio2AsRfSwitch(bool){return step("dio2");}
 void setRfSwitchPins(uint32_t,uint32_t){}
 int setSyncWord(int){return step("sync");}
 int setPreambleLength(int){return step("preamble");}
 int explicitHeader(){return step("header");}
 int setCRC(int){return step("crc");}
 int invertIQ(bool){return step("iq");}
 int autoLDRO(){return step("ldro");}
 virtual int16_t startReceive(){++rxStarts;dioMask=RADIOLIB_SX126X_IRQ_RX_DONE;return step("rx");}
 int16_t startReceive(uint32_t,uint32_t,uint32_t,uint16_t){++rxStarts;irq=0;dioMask=RADIOLIB_SX126X_IRQ_RX_DONE;return step("rx");}
 int startTransmit(uint8_t* p,size_t n){++txStarts;dioMask=RADIOLIB_SX126X_IRQ_TX_DONE;transmitted.assign(p,p+n);return step("tx");}
 int finishTransmit(){return step("finishTx");}
 int startChannelScan(){++scans;dioMask=RADIOLIB_SX126X_IRQ_CAD_DONE|RADIOLIB_SX126X_IRQ_CAD_DETECTED;return step("scan");}
 int startChannelScan(const ChannelScanConfig_t& c){lastScan=c;return startChannelScan();}
 uint32_t getIrqFlags(){++irqReads;return irq;}
 int clearIrqFlags(uint32_t bits){irq&=~bits;return step("clear");}
 void setDio1Action(void(*f)()){action=f;}
 size_t getPacketLength(){return incoming.size();}
 int readData(uint8_t* p,size_t n){++readCount;memcpy(p,incoming.data(),n);irq=0;return step("read");}
 float getRSSI(bool packet=true){return packet?packetRssi:ambient;}
 float getSNR(){return snr;}
 uint32_t getTimeOnAir(size_t n){return 10000+n*1000;}
 int setRxBoostedGainMode(bool v){boost=v;return step("boost");}
 int resetAGC(){++agc;return step("agc");}
 int16_t readRegister(uint16_t,uint8_t* v,size_t){*v=0;return step("readReg");}
 int16_t writeRegister(uint16_t,const uint8_t*,size_t){return step("patch");}
 void edge(uint32_t flags){irq=flags;if(action)action();}
 void receive(std::initializer_list<uint8_t> p){incoming=p;edge(RADIOLIB_SX126X_IRQ_RX_DONE);}
};
inline uint32_t RadioLibHal::digitalRead(uint32_t pin) {
 for(const auto& entry:chips) {
  const auto* chip=entry.second;
  if(uint32_t(chip->mod->dio)==pin)
   return chip->dioOverride>=0 ? uint32_t(chip->dioOverride) : (chip->irq&chip->dioMask ? 1U : 0U);
 }
 return 0;
}
inline int16_t Module::SPIreadStream(uint16_t command,uint8_t* data,size_t length,bool,bool) {
 if(command!=RADIOLIB_SX126X_CMD_GET_IRQ_STATUS || length!=2)return -706;
 auto* chip=chips.at(nss);++chip->irqReads;
 if(chip->beforeIrqRead)chip->beforeIrqRead();
 const uint16_t flags=uint16_t(chip->irqDataOverride>=0 ? chip->irqDataOverride : chip->irq);
 data[0]=uint8_t(flags>>8);data[1]=uint8_t(flags);
 auto& failures=chip->failures["irq"];
 if(!failures.empty()){int result=failures.front();failures.pop_front();if(result)return result;}
 return spiConfig.parseStatusCb ? spiConfig.parseStatusCb(chip->irqStatus) : 0;
}
