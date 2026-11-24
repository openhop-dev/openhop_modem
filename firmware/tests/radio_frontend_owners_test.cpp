#include "rf_frontend.h"
#include "board_config.h"
#include <Preferences.h>
#include <cassert>
#include <iostream>
int main() {
 BoardConfig a=BOARD,b=BOARD;
 b.rf_frontend.pa_mode_pin=41;b.rf_frontend.lna_mode_pin=42;
 RFFrontEnd::Controller one(a,true),two(b,false);
 one.begin();two.begin();
 assert(!one.isPaHighPowerEnabled()&&!two.isPaHighPowerEnabled());
 assert(one.setStationG3RfConfig(true,false,false));
 assert(one.isPaHighPowerEnabled()&&!two.isPaHighPowerEnabled());
 one.prepareReceive();two.prepareReceive();
 assert(gpio[a.rf_frontend.lna_mode_pin]==HIGH); // active-low: one disabled
 assert(gpio[b.rf_frontend.lna_mode_pin]==LOW); // two independent enabled
 assert(one.setAgcResetIntervalSec(17,false));
 assert(two.getAgcResetIntervalSec()!=17);
 assert(!two.setStationG3RfConfig(true,false,true));
 preferenceWrite=false;
 assert(!one.setStationG3RfConfig(false,true,true));
 assert(one.isPaHighPowerEnabled()&&!one.isStationG3LnaEnabled());
 two.prepareTransmit();assert(gpio[42]==HIGH);
 std::cout<<"PASS frontend owner isolation/persist-before-apply\n";
}
