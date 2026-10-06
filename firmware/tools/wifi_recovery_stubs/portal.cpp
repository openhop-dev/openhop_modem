// Real complete handlers and registration, with recording platform dependencies.
#include "../../src/config_portal.cpp"
#include <cassert>
int configReads=0,saves=0,rfWrites=0;
namespace WifiManager {
const Config& getConfig(){configReads++;static Config cfg{};return cfg;}
void saveConfig(const Config&){saves++;}
bool hasWifiAntennaSwitch(){return false;}
}
namespace RFFrontEnd {
bool hasHeltecV43LnaControl(){return true;}
bool isExternalLnaEnabled(){return true;}
uint16_t getAgcResetIntervalSec(){return 0;}
bool setFemLnaBypassed(bool,bool){rfWrites++;return true;}
bool setAgcResetIntervalSec(uint16_t,bool){rfWrites++;return true;}
}
namespace WebUiShared {
std::string renderSetupPage(const SetupModel&){return "setup";}
std::string htmlEscape(const std::string& s){return s;}
}
int main(int argc,char** argv){
 assert(argc==2);
 const std::string scenario=argv[1];
 WiFi.modeValue=WIFI_AP_STA;
 ConfigPortal::begin();
 auto* server=ConfigPortal::server;
 server->socket.s=std::make_shared<SocketStub>();
 server->socket.s->local=WiFi.softAPIP();
 if(scenario=="station")server->socket.s->local=WiFi.localIP();
 if(scenario=="ipv6")server->socket.s->local=IPAddress(IPv6);
 if(scenario=="missing")server->socket.s.reset();
 if(scenario=="zero")WiFi.apIP=IPAddress();
 if(scenario=="ap_off")WiFi.modeValue=WIFI_STA;
 if(scenario=="stale_ap")WiFi.apIP=IPAddress(192,168,5,1);
 const bool allowed=scenario=="ap";
 for(const char* path:{"/","/save","/missing"}){
  server->request(path);
  if(!allowed){
   assert(server->response==403);
   assert(configReads==0 && saves==0 && rfWrites==0);
   assert(WiFi.scans==0 && server->argumentReads==0 && ESP.restarts==0);
  }else assert(server->response==(std::string(path)=="/missing"?404:200));
 }
 if(allowed)assert(configReads==2 && WiFi.scans==1 && saves==1 && rfWrites==2 && ESP.restarts==1);
 ConfigPortal::end();
}
