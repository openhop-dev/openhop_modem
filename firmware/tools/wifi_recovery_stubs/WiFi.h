#pragma once
#include "IPAddress.h"
#include <functional>
enum arduino_event_id_t {ARDUINO_EVENT_WIFI_STA_START, ARDUINO_EVENT_WIFI_STA_STOP, ARDUINO_EVENT_WIFI_STA_CONNECTED, ARDUINO_EVENT_WIFI_STA_DISCONNECTED, ARDUINO_EVENT_WIFI_STA_GOT_IP, ARDUINO_EVENT_WIFI_STA_LOST_IP, ARDUINO_EVENT_WIFI_AP_START, ARDUINO_EVENT_WIFI_AP_STOP};
struct arduino_event_info_t { struct {int channel=1;} wifi_sta_connected; struct {int reason=0;} wifi_sta_disconnected; struct {bool ip_changed=false;} got_ip; };
enum {WL_IDLE_STATUS=0,WL_NO_SSID_AVAIL=1,WL_CONNECTED=3,WL_CONNECT_FAILED=4,WL_DISCONNECTED=6, WIFI_OFF=0,WIFI_STA=1,WIFI_AP=2,WIFI_AP_STA=3};
struct WifiStub {
 int statusValue=WL_DISCONNECTED, modeValue=WIFI_OFF, begins=0, disconnects=0, stationStops=0;
 int apStops=0,configs=0;
 bool failMode=false,failBegin=false,failConfig=false,failAP=false,failAPStop=false,failDisconnect=false,autoReconnect=true,sleep=true,ap=false;
 IPAddress ip{192,168,1,20};
 IPAddress apIP{192,168,4,1};
 int scans=0;
 int getMode(){return modeValue;}
 int scanNetworks(bool,bool){scans++;return 0;}
 void scanDelete(){}
 String SSID(int){return "test";}
 int RSSI(int){return -50;}
 std::function<void(arduino_event_id_t,arduino_event_info_t)> callback;
 void onEvent(decltype(callback) cb){callback=cb;}
 void event(arduino_event_id_t e,int reason=0){arduino_event_info_t i; i.wifi_sta_disconnected.reason=reason;callback(e,i);}
 void persistent(bool){}
 bool setSleep(bool b){sleep=b;return true;}
 bool setAutoReconnect(bool b){autoReconnect=b;return true;}
 bool mode(int m){if(failMode)return false; if((modeValue&1)&&!(m&1)){stationStops++;statusValue=WL_DISCONNECTED;}modeValue=m;return true;}
 bool setHostname(const char*){return true;}
 bool config(IPAddress,IPAddress,IPAddress,IPAddress,IPAddress){configs++;return !failConfig;}
 int begin(const char*,const char*){begins++;statusValue=WL_DISCONNECTED;return failBegin?WL_CONNECT_FAILED:WL_IDLE_STATUS;}
 bool disconnect(bool=false,bool erase=false){if(erase)abort();disconnects++;statusValue=WL_DISCONNECTED;return !failDisconnect;}
 bool reconnect(){begins++;return !failBegin;}
 int status(){return statusValue;}
 IPAddress localIP(){return ip;}
 IPAddress softAPIP(){return apIP;}
 int RSSI(){return -50;}
 void macAddress(uint8_t* m){memset(m,0,6);}
 bool softAP(const char*,const char*){ap=!failAP;return ap;}
 bool softAPdisconnect(bool){apStops++;if(failAPStop)return false;ap=false;return true;}
};
inline WifiStub WiFi;
struct SocketStub {bool open=true;IPAddress local{192,168,1,20};std::deque<uint8_t> input;};
struct WiFiClient {
 std::shared_ptr<SocketStub> s;
 explicit operator bool()const{return s && s->open;}
 bool connected(){return bool(*this);}
 void stop(){if(s)s->open=false;}
 IPAddress remoteIP(){return IPAddress(192,168,1,2);}
 IPAddress localIP(){return s?s->local:IPAddress();}
 void setNoDelay(bool){}
 size_t write(const uint8_t*,size_t n){return n;}
 int available(){return s?s->input.size():0;}
 int read(){int b=s->input.front();s->input.pop_front();return b;}
};
inline WiFiClient incomingClient;
struct WiFiServer {explicit WiFiServer(uint16_t){} void begin(){} void end(){} void setNoDelay(bool){}
 WiFiClient available(){auto c=incomingClient;incomingClient={};return c;}
};
