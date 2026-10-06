#pragma once
#include "Arduino.h"
inline bool savedNetwork=true;
struct Preferences {
 bool begin(const char*,bool){return true;}
 String getString(const char* k,const char* d){return strcmp(k,"ssid")==0 && savedNetwork ? "test-network" : d;}
 bool getBool(const char*,bool d){return d;}
 uint32_t getUInt(const char*,uint32_t d){return d;}
 uint16_t getUShort(const char*,uint16_t d){return d;}
 template<class T> void putString(const char*,T){}
 template<class T> void putBool(const char*,T){}
 template<class T> void putUInt(const char*,T){}
 template<class T> void putUShort(const char*,T){}
 void end(){} void clear(){abort();}
};
