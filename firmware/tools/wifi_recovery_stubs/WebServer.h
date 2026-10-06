#pragma once
#include "WiFi.h"
#include <map>
constexpr int HTTP_GET=0, HTTP_POST=1;
struct WebServer {
 // Deliberately wildcard: reproduce the pinned IPv6 listener ignoring bind IP.
 WebServer(int){}
 WebServer(IPAddress,int){}
 std::map<std::string,std::function<void()>> routes;
 std::function<void()> missing;
 WiFiClient socket;
 int response=0, argumentReads=0;
 void on(const char* path,int,std::function<void()> fn){routes[path]=fn;}
 void onNotFound(std::function<void()> fn){missing=fn;}
 void send(int code,const char*,const String&){response=code;}
 WiFiClient& client(){return socket;}
 String arg(const char* name){argumentReads++;return std::string(name)=="ssid"?"test-network":"";}
 bool hasArg(const char*){argumentReads++;return false;}
 void begin(){} void stop(){} void handleClient(){}
 void request(const std::string& path){response=0;auto it=routes.find(path);if(it==routes.end())missing();else it->second();}
};
