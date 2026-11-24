#pragma once
#include "Arduino.h"
struct StoredPreference { char type; unsigned value; };
inline std::map<std::string,StoredPreference> preferences;
inline bool preferenceOpen=true,preferenceWrite=true;
inline size_t preferenceReads=0;
class Preferences {
public:
 bool begin(const char*,bool){return preferenceOpen;}
 void end(){}
 bool isKey(const char* k){return preferences.count(k);}
 unsigned get(const char* k,char t,unsigned d){++preferenceReads;auto i=preferences.find(k);return i!=preferences.end()&&i->second.type==t?i->second.value:d;}
 bool getBool(const char* k,bool d){return get(k,'b',d);}
 uint8_t getUChar(const char* k,uint8_t d){return get(k,'c',d);}
 uint16_t getUShort(const char* k,uint16_t d){return get(k,'s',d);}
 size_t put(const char* k,char t,unsigned v){if(!preferenceWrite)return 0;preferences[k]={t,v};return 1;}
 size_t putBool(const char* k,bool v){return put(k,'b',v);}
 size_t putUChar(const char* k,uint8_t v){return put(k,'c',v);}
 size_t putUShort(const char* k,uint16_t v){return put(k,'s',v);}
};
