#pragma once
#include "Arduino.h"
enum IPType {IPv4, IPv6};
class IPAddress {
 uint32_t v;
 IPType kind=IPv4;
public:
 IPAddress(IPType t):v(0),kind(t){}
 IPType type()const{return kind;}
 bool fromString(const String& s){unsigned a,b,c,d;if(sscanf(s.c_str(),"%u.%u.%u.%u",&a,&b,&c,&d)!=4)return false;v=a|(b<<8)|(c<<16)|(d<<24);return true;}
 IPAddress(uint32_t x=0):v(x){}
 IPAddress(uint8_t a,uint8_t b,uint8_t c,uint8_t d):v(a | (b<<8) | (c<<16) | (d<<24)){}
 operator uint32_t() const{return v;}
 uint8_t operator[](int n)const{return v>>(n*8);}
 String toString()const{char b[32];snprintf(b,sizeof b,"%u.%u.%u.%u",(*this)[0],(*this)[1],(*this)[2],(*this)[3]);return b;}
};
