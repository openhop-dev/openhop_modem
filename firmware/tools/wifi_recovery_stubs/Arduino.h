#pragma once
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
class String : public std::string {
public:
 using std::string::string;
 String(const std::string& s):std::string(s){}
 bool endsWith(const char* s) const { return size() && back()==s[0]; }
 void remove(size_t n) { erase(n); }
 void trim(){auto a=find_first_not_of(" \t\r\n"); if(a==npos){clear();return;} erase(0,a);erase(find_last_not_of(" \t\r\n")+1);}
 long toInt()const{return strtol(c_str(),nullptr,10);}
};
inline uint32_t clockMs=0;
inline uint32_t millis(){return clockMs;}
inline void delay(uint32_t n){clockMs+=n;}
constexpr int OUTPUT=1, INPUT_PULLUP=2, LOW=0, HIGH=1;
inline void pinMode(int,int){}
inline void digitalWrite(int,int){}
inline int digitalRead(int){return HIGH;}
struct SerialStub { template<class... T> void printf(const char*,T...){} void println(const char*){} };
inline SerialStub Serial;
#define F(x) x
struct ESPStub { int restarts=0; void restart(){restarts++;} }; inline ESPStub ESP;
