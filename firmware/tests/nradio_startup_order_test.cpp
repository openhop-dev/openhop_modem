#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <Preferences.h>
#include "radio_layout.h"
#include "radio_settings.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
void setup(); void loop();
int main(int argc,char**argv){
    assert(argc==2);const unsigned mask=std::strtoul(argv[1],nullptr,10);
    preferences["enabled"]={'c',mask};unsigned busBegins=0;
    beforeSpiBegin=[&](){
        ++busBegins;
        for(size_t i=0;i<RadioLayout::count();++i){
            const auto pin=RadioLayout::at(i).hardware->pin_lora_nss;
            if(NRADIO_COUNT==1){
                assert(!gpio.count(pin)&&!gpioModes.count(pin)&&"legacy single-radio startup ordering unchanged");
            }else if(mask&(1U<<i)){
                assert(gpioModes.count(pin)&&gpioModes.at(pin)==OUTPUT&&
                       gpio.count(pin)&&gpio.at(pin)==HIGH&&
                       "every enabled NSS must be inactive before the first shared SPI begin");
            }else{
                assert(!gpioModes.count(pin)&&!gpio.count(pin)&&"disabled slot GPIO must remain untouched");
            }
        }
    };
    setup();
    assert(busBegins==(mask?1U:0U));
    for(size_t i=0;i<RadioLayout::count();++i)if(!(mask&(1U<<i))){
        const auto pin=RadioLayout::at(i).hardware->pin_lora_nss;
        assert(!gpio.count(pin)&&!gpioModes.count(pin));
    }
    std::cout<<"PASS startup-CS N="<<NRADIO_COUNT<<" mask="<<mask
             <<": production setup deselects enabled peers before SPI; disabled GPIO untouched\n";
}
