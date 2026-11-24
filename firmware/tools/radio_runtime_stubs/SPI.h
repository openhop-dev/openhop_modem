#pragma once
#include "Arduino.h"
#include <functional>
#define FSPI 0
#define HSPI 1
#define NRF_SPIM2 2
inline std::map<int,bool> spiBeginFailures;
inline std::function<void()> beforeSpiBegin;
class SPIClass {
public:
 int bus=0,begins=0,sck=-1,miso=-1,mosi=-1,nss=-1;
 SPIClass(int id=0,int mi=-1,int sc=-1,int mo=-1):bus(id),sck(sc),miso(mi),mosi(mo){}
 bool begin(int sc=-1,int mi=-1,int mo=-1,int ns=-1){if(beforeSpiBegin)beforeSpiBegin();++begins;sck=sc;miso=mi;mosi=mo;nss=ns;return !spiBeginFailures[bus];}
};
inline SPIClass SPI;
