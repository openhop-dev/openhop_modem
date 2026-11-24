#include "rf_frontend.h"
#include <Preferences.h>
#include <cassert>
#include <iostream>
int main(){
 BoardConfig hardware=BOARD;hardware.sx126x_agc_reset_interval_ms=7000;
#ifdef BOARD_STATION_G3
 preferences["g3_rf_cfg"]={'c',3};
 RFFrontEnd::Controller packed(hardware,true);packed.begin();
 assert(!gpioWrites.empty()&&gpioWrites[0].first==hardware.rf_frontend.pa_mode_pin&&gpioWrites[0].second==LOW);
 assert(packed.isPaHighPowerEnabled()&&packed.isStationG3LnaEnabled());
 packed.prepareReceive();assert(gpio[hardware.rf_frontend.lna_mode_pin]==LOW);
 packed.prepareTransmit();assert(gpio[hardware.rf_frontend.lna_mode_pin]==HIGH);
 auto writes=gpioWrites.size();preferenceWrite=false;
 assert(!packed.setStationG3RfConfig(false,false,true));assert(gpioWrites.size()==writes&&packed.isPaHighPowerEnabled());
 preferenceWrite=true;assert(packed.setStationG3RfConfig(false,false,true));assert(preferences.at("g3_rf_cfg").value==0);
 preferences.clear();preferences["g3_pa_high"]={'b',1};preferences["g3_lna_en"]={'b',0};
 RFFrontEnd::Controller legacy(hardware,true);legacy.begin();assert(legacy.isPaHighPowerEnabled()&&!legacy.isStationG3LnaEnabled());
 preferences["g3_rf_cfg"]={'b',1};preferences["g3_agc_sec"]={'b',1};
 RFFrontEnd::Controller wrongType(hardware,true);wrongType.begin();assert(!wrongType.isPaHighPowerEnabled()&&!wrongType.isStationG3LnaEnabled());
 assert(wrongType.getAgcResetIntervalSec()==7);
 preferences["g3_agc_sec"]={'s',65535};RFFrontEnd::Controller bounded(hardware,true);bounded.begin();assert(bounded.getAgcResetIntervalSec()==3600);
#else
 preferences["v43_lna_bp"]={'b',0};
 RFFrontEnd::Controller primary(hardware,true);primary.begin();assert(primary.isExternalLnaEnabled());
 primary.prepareReceive();assert(gpio[5]==LOW);primary.prepareTransmit();assert(gpio[5]==HIGH);
 primary.prepareReceive();assert(gpio[5]==LOW);primary.prepareStandby();assert(gpio[5]==HIGH);
 preferences["v43_lna_bp"]={'s',0};preferences["v43_agc_sec"]={'b',1};
 RFFrontEnd::Controller wrongType(hardware,true);wrongType.begin();assert(wrongType.isFemLnaBypassed()&&wrongType.getAgcResetIntervalSec()==7);
#endif
 std::cout<<"PASS typed NVS defaults/precedence, conservative GPIO, persistent failure and RX-only frontend\n";
}
