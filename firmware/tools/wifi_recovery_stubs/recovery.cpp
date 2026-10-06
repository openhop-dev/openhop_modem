#include "WiFi.h"
#include "Preferences.h"
#include "config_portal.h"
#include <iostream>
namespace ConfigPortal {bool active=false;int serviced=0;void begin(){active=true;}void end(){active=false;}void loop(){serviced++;}bool isActive(){return active;}}
#include "../../src/wifi_manager.cpp"
#include "../../src/tcp_server.cpp"
#include "../../src/frame_parser.cpp"
void processHostCommand(uint8_t,const uint8_t*,uint16_t,TransportSource){}
void noteTransportFrameError(uint8_t){}
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": " #x "\n";return 1;}}while(0)
void tick(uint32_t n){clockMs+=n;WifiManager::loop();}
void connected(){WiFi.statusValue=WL_CONNECTED;WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP);WifiManager::loop();}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 std::string t=argv[1];
 if(t=="no_config_ap")savedNetwork=false;
 if(t=="backoff_wrap")clockMs=0xfffff000;
 if(t=="ap_start_failure"){savedNetwork=false;WiFi.failAP=true;}
 auto start=clockMs;WifiManager::begin();
 if(t=="nonblocking_boot"){CHECK(clockMs==start);CHECK(WiFi.begins==1);CHECK(WiFi.sleep);CHECK(!WiFi.autoReconnect);}
 else if(t=="ap_start_failure"){CHECK(!ConfigPortal::isActive());WiFi.failAP=false;tick(5000);CHECK(ConfigPortal::isActive());CHECK(WiFi.begins==0);}
 else if(t=="no_config_ap"){for(int i=0;i<50;i++)tick(30000);CHECK(WiFi.begins==0);CHECK(ConfigPortal::isActive());}
 else if(t=="startup_fallback_retry" || t=="backoff_wrap"){
 tick(30000);CHECK(ConfigPortal::isActive());int b=WiFi.begins;tick(4999);CHECK(WiFi.begins==b);tick(1);CHECK(WiFi.begins>b);CHECK(ConfigPortal::isActive());}
 else if(t=="ap_stop_failure"){tick(30000);WiFi.failAPStop=true;connected();int calls=WiFi.apStops;CHECK(ConfigPortal::isActive());for(int i=0;i<100;i++){tick(1);}CHECK(WiFi.apStops==calls);WiFi.failAPStop=false;tick(5000);CHECK(!ConfigPortal::isActive());CHECK(WifiManager::isSTAConnected());}
 else if(t=="static_policy"){WifiManager::cfg.useStaticIP=true;WiFi.failConfig=true;tick(30000);int b=WiFi.begins;tick(5000);CHECK(WiFi.begins==b);CHECK(WiFi.configs==1);WiFi.failConfig=false;tick(30000);tick(10000);CHECK(WiFi.begins>b);CHECK(WiFi.configs==2);CHECK(WiFi.sleep);}
 else if(t=="retry_cap"){for(int i=0;i<10000;i++){tick(1000);}CHECK(WifiManager::backoffMs==60000);CHECK(WiFi.begins<150);CHECK(WiFi.begins>100);}
 else if(t=="portal_teardown"){tick(30000);CHECK(ConfigPortal::isActive());tick(5000);connected();CHECK(!ConfigPortal::isActive());CHECK(!WiFi.ap);CHECK(WifiManager::isSTAConnected());}
 else if(t=="persistent_restart"){for(int i=0;i<100;i++)tick(1000);CHECK(WiFi.begins>=3);for(int i=0;i<150;i++)tick(1000);CHECK(WiFi.stationStops>=1);CHECK(ConfigPortal::isActive());}
 else if(t=="api_failures"){WiFi.failBegin=true;WiFi.failMode=true;for(int i=0;i<200;i++)tick(1000);WiFi.failMode=false;WiFi.failBegin=false;int b=WiFi.begins;for(int i=0;i<100;i++)tick(1000);CHECK(WiFi.begins>b);connected();CHECK(WifiManager::isSTAConnected());}
 else {
 connected();
 if(t=="renewal_preserved"){WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP);tick(1);CHECK(WifiManager::consumeSTAInvalidation()==0);}
 else if(t=="new_client_auth"){TCPServer::begin(5055,"secret");incomingClient.s=std::make_shared<SocketStub>();TCPServer::loop();TCPServer::authenticated=true;WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED);WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP);tick(1);TCPServer::invalidateInterface(IPAddress(WifiManager::consumeSTAInvalidation()));incomingClient.s=std::make_shared<SocketStub>();TCPServer::loop();CHECK(!TCPServer::isClientReady());CHECK(TCPServer::parser.state==FrameParser::WAIT_SYNC);}
 else if(t=="lost_ip_stale_status"){WiFi.event(ARDUINO_EVENT_WIFI_STA_LOST_IP);tick(1);CHECK(!WifiManager::isSTAConnected());connected();CHECK(WifiManager::isSTAConnected());}
 else if(t=="ip_refresh"){WiFi.ip=IPAddress(192,168,1,99);tick(1);CHECK(std::string(WifiManager::getIPString())=="192.168.1.99");}
 else if(t=="short_outage"){
 // A short event outage must invalidate even though status is already connected.
 WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED,202);WiFi.event(ARDUINO_EVENT_WIFI_STA_GOT_IP);tick(1);
 #ifdef RECOVERY_IMPLEMENTED
 CHECK(WifiManager::consumeSTAInvalidation()!=0);CHECK(WifiManager::consumeSTAInvalidation()==0);
 #else
 CHECK(false && "missing event invalidation API");
 #endif
 }
 else if(t=="tcp_cleanup" || t=="ethernet_preserved" || t=="socket_address_loss"){
 TCPServer::begin(5055,"secret");incomingClient.s=std::make_shared<SocketStub>();auto socket=incomingClient.s;
 if(t=="ethernet_preserved")socket->local=IPAddress(10,0,0,20);
 TCPServer::loop();TCPServer::authenticated=true;
 TCPServer::parser.state=FrameParser::READ_PAYLOAD;
 if(t=="socket_address_loss")socket->local=IPAddress();
 #ifdef RECOVERY_IMPLEMENTED
 TCPServer::invalidateInterface(IPAddress(192,168,1,20));
 #endif
 CHECK(socket->open==(t=="ethernet_preserved"));
 CHECK((TCPServer::parser.state==FrameParser::READ_PAYLOAD)==(t=="ethernet_preserved"));CHECK(TCPServer::authenticated==(t=="ethernet_preserved"));
 }
 else {int b=WiFi.begins;WiFi.statusValue=WL_DISCONNECTED;
 WiFi.event(t=="lost_ip"?ARDUINO_EVENT_WIFI_STA_LOST_IP:ARDUINO_EVENT_WIFI_STA_DISCONNECTED,t=="auth_failure"?202:200);
 tick(1);CHECK(!WifiManager::isSTAConnected());CHECK(std::string(WifiManager::getIPString())=="---");
 for(int i=0;i<50;i++){tick(1000);}CHECK(WiFi.begins>b);CHECK(!ConfigPortal::isActive());}
 }
 std::cout<<"PASS "<<t<<"\n";
}
