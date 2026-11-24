// Unrelated board-device ports only. Main, radio, TCP and parser are production.
#include <Arduino.h>
#include "board_config.h"
#include "wifi_manager.h"
#include "ethernet_manager.h"
#include "ota_manager.h"
#include "oled_display.h"
#include "station_g3_power.h"
#include "environment_sensor.h"
#include "battery_monitor.h"
#include "tbeam_1w_fan.h"
#include "gps_manager.h"
#include "pmu_manager.h"
#include "dhcp_routes.h"
namespace WifiManager {
Config cfg=[](){Config c{};c.hostname="test-modem";c.tcpToken="secret";c.tcpPort=6200;return c;}();
void checkResetButton(){} void loadConfigOnly(){} void begin(){} void loop(){}
uint32_t consumeSTAInvalidation(){return 0;}
Mode getMode(){return Mode::OFFLINE;} const char* getSSID(){return "---";}
const char* getIPString(){return "---";} const char* getHostname(){return "test-modem";}
bool isSTAConnected(){return false;} bool isAPActive(){return false;}
bool hasWifiAntennaSwitch(){return false;} void applyWifiAntennaSwitch(){}
const Config& getConfig(){return cfg;} void saveConfig(const Config& c){cfg=c;} void factoryReset(){}
}
namespace EthernetManager {
void begin(const char*,bool,const IPAddress&,const IPAddress&,const IPAddress&,const IPAddress&,const IPAddress&){}
void end(){} void loop(){} bool isLinkUp(){return true;} bool hasIP(){return true;}
const char* getIPString(){return "192.168.1.20";} const char* getMACString(){return "test";}
}
namespace OTAManager {int ticks=0;void begin(const String&,const String&){} void loop(){++ticks;}void notifyValidFrame(){}const char* getHostname(){return "test";}}
namespace StationG3Power {void begin(){}void loop(){}Snapshot snapshot(){return {};}}
namespace EnvironmentSensor {void begin(){}void loop(){}Snapshot snapshot(){return {};}}
namespace BatteryMonitor {int reads=0;void loop(const BatterySenseConfig&){}uint16_t readMilliVolts(const BatterySenseConfig&){++reads;return 3981;}bool readChargeRatePctPerHour(const BatterySenseConfig&,float&){return false;}}
namespace TBeam1WFan {void begin(){}void powerOff(){}float temperatureC(){return NAN;}bool isEnabled(){return false;}}
namespace GPSManager {void begin(bool){}void loop(){}}
namespace PmuManager {bool begin(){return true;}}
namespace DhcpRoutes {void begin(){}}
void OledDisplay::begin(){}void OledDisplay::showSplash(){}void OledDisplay::showBoot(const char*){}
void OledDisplay::showStatus(uint32_t,uint32_t,const char*,const char*,const char*,const char*,uint16_t,float){}
void OledDisplay::showRadioConfig(uint32_t,uint32_t,uint8_t,uint8_t,int8_t,uint16_t,uint8_t,const char*){}
void OledDisplay::showDiagnostics(uint32_t,const char*,uint32_t,uint32_t,uint32_t,uint32_t,uint16_t,float,const char*){}
void OledDisplay::showError(const char*){}void OledDisplay::clear(){}void OledDisplay::turnOff(){}void OledDisplay::turnOn(){}
void OledDisplay::setRadioInfo(uint32_t,uint8_t,uint32_t,uint8_t,int8_t,uint8_t,uint16_t){}
void OledDisplay::setDisplayName(const char*){}void OledDisplay::setStandby(bool){}
