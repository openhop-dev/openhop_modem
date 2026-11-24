#pragma once
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <vector>
#include <utility>
#include <memory>
class String : public std::string {
public:
 using std::string::string;
 String(const std::string& s):std::string(s){}
 String(unsigned value):std::string(std::to_string(value)){}
 bool endsWith(const char* s) const { return size() && back()==s[0]; }
 void remove(size_t n) { erase(n); }
 void trim(){auto a=find_first_not_of(" \t\r\n"); if(a==npos){clear();return;} erase(0,a);erase(find_last_not_of(" \t\r\n")+1);}
 long toInt()const{return strtol(c_str(),nullptr,10);}
};
inline uint32_t clockMs=0, delayCalls=0;
inline uint32_t millis(){return clockMs;}
inline uint32_t micros(){return clockMs*1000;}
inline void delay(uint32_t n){clockMs+=n;++delayCalls;}
constexpr int OUTPUT=1, INPUT_PULLUP=2, LOW=0, HIGH=1, SERIAL_8N1=0;
inline std::map<int,int> gpio;
inline std::map<int,int> gpioModes;
inline std::vector<std::pair<int,int>> gpioWrites;
inline void pinMode(int pin,int mode){gpioModes[pin]=mode;}
inline void digitalWrite(int p,int v){gpio[p]=v;gpioWrites.push_back({p,v});}
inline int digitalRead(int){return HIGH;}
inline void analogReadResolution(int){}
inline int analogRead(int){return 0;}
inline float temperatureRead(){return 31.0f;}
struct SerialStub {
 std::deque<uint8_t> rx; std::vector<uint8_t> tx;
 template<class... T> void printf(const char*,T...){}
 void println(const char*){} void begin(uint32_t,int=0,int=-1,int=-1){}
 int available()const{return rx.size();} int read(){int b=rx.front();rx.pop_front();return b;}
 int availableForWrite()const{return 4096;}
 size_t write(const uint8_t* p,size_t n){tx.insert(tx.end(),p,p+n);return n;}
 void flush(){} explicit operator bool()const{return false;}
};
inline SerialStub Serial, Serial1, Serial2;
#define F(x) x
#define IRAM_ATTR
struct ESPStub { int restarts=0; void restart(){restarts++;} int getFreeHeap(){return 100000;} int getMinFreeHeap(){return 90000;} }; inline ESPStub ESP;
