#!/usr/bin/env python3
"""Compile real board headers and battery reader against host GPIO/I2C stubs.

Run: python3 firmware/tools/test_battery_gpio_safety.py (requires g++).
Hardware calls are recorded, not executed; this is not board validation.
"""
from pathlib import Path
import subprocess
import tempfile

FW = Path(__file__).resolve().parents[1]
ARDUINO = r'''#pragma once
#include <stdint.h>
#include <vector>
#include <tuple>
constexpr int INPUT=0, OUTPUT=1, LOW=0, HIGH=1, AR_INTERNAL_3_0=3;
inline std::vector<std::tuple<int,int,int>> calls;
inline uint32_t now=0;
inline void pinMode(int p,int v){calls.emplace_back(0,p,v);}
inline void digitalWrite(int p,int v){calls.emplace_back(1,p,v);}
inline void delay(int v){calls.emplace_back(2,-1,v);}
inline uint32_t millis(){return now;}
inline void analogReference(int v){calls.emplace_back(3,-1,v);}
inline void analogReadResolution(int v){calls.emplace_back(4,-1,v);}
inline int analogRead(int p){calls.emplace_back(5,p,0); return 3000;}
constexpr int ADC_11db=11;
inline void analogSetPinAttenuation(int p,int v){calls.emplace_back(7,p,v);}
inline int analogReadMilliVolts(int p){calls.emplace_back(6,p,0); return 1000;}
'''
WIRE = r'''#pragma once
struct WireStub {
 int count=0; bool fail=false;
 void setTimeOut(int){}
 void beginTransmission(int){count=0;}
 void write(int){}
 int endTransmission(bool){return fail ? 1 : 0;}
 int requestFrom(unsigned char,unsigned char){return 2;}
 int read(){return count++ == 0 ? 0xc8 : 0;}
};
inline WireStub Wire;
'''
TEST = r'''
#include <Arduino.h>
#include <Wire.h>
#include "board_config.h"
#include "battery_monitor.h"
#include <limits>
#include <cstdio>
#include <string>
int failures=0;
void check(bool ok, const char* name){printf("%s: %s\n",ok?"PASS":"FAIL",name); failures+=!ok;}
void untouched(BatterySenseConfig c, const char* name){
 calls.clear(); BatteryMonitor::loop(c);
 auto mv=BatteryMonitor::readMilliVolts(c);
 check(mv==BatteryMonitor::MILLIVOLTS_UNAVAILABLE && calls.empty(), name);
}
int main(){
 untouched(BOARD.battery,"real Heltec V3 omitted battery never touches GPIO0");
 BatterySenseConfig empty{};
 check(empty.pin==-1 && empty.enable_pin==-1 && empty.multiplier==0.0f && empty.enable_active_high,
       "optional battery defaults disabled");
 untouched(empty,"empty battery has no side effects");
 for(float value : {0.0f,-1.0f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
  BatterySenseConfig c{1,0,true,value}; untouched(c,"invalid calibrated multiplier has no side effects");
 }
 BatterySenseConfig c{1,0,true,4.0f};
#if defined(ARDUINO_ARCH_ESP32)
 for(bool high : {false,true}) {
  c.enable_active_high=high; calls.clear();
  check(BatteryMonitor::readMilliVolts(c)==4000,"calibrated ADC scale preserved");
  check(calls.size()==19 && calls[0]==std::make_tuple(0,0,OUTPUT) &&
        calls[1]==std::make_tuple(1,0,high?HIGH:LOW),"valid explicit GPIO0 and gate polarity preserved");
 }
 c.adc_attenuation_db=11; calls.clear();
 check(BatteryMonitor::readMilliVolts(c)==4000,"board-specific calibrated attenuation preserves voltage");
 check(calls[3]==std::make_tuple(7,1,ADC_11db),"explicit board attenuation applied");
 c.adc_attenuation_db=-1;
#else
 untouched(c,"unsupported calibrated ADC never drives gate");
#endif
 c={-1,0,false,0.0f}; c.fuel_gauge_i2c_addr=0x36;
 calls.clear(); check(BatteryMonitor::readMilliVolts(c)==4000 && calls.empty(),"fuel gauge independent of ADC pins/multiplier");
 Wire.fail=true; untouched(c,"missing fuel gauge unavailable without GPIO"); Wire.fail=false;
 c={5,-1,true,0.0f}; c.adc_reference_mv=3000; c.adc_resolution_bits=12;
 c.adc_divider_numerator=173; c.adc_divider_denominator=100; c.sample_count=1;
 c.minimum_plausible_mv=2500; c.maximum_plausible_mv=4500; c.minimum_valid_samples=1;
 calls.clear(); BatteryMonitor::loop(c);
 check(BatteryMonitor::readMilliVolts(c)==3801 && !calls.empty(),"raw ADC works with zero calibrated multiplier");
 return failures ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix="battery-gpio-test-") as directory:
    tmp = Path(directory)
    (tmp / "Arduino.h").write_text(ARDUINO)
    (tmp / "Wire.h").write_text(WIRE)
    (tmp / "test.cpp").write_text(TEST)
    failed = False
    for arch in ("ESP32", "NRF52"):
        binary = tmp / arch
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                        "-Wno-missing-field-initializers",  # intentional board defaults
                        "-DARDUINO", "-DBOARD_HELTEC_V3", f"-DARDUINO_ARCH_{arch}",
                        f"-I{tmp}", f"-I{FW / 'include'}", str(tmp / "test.cpp"),
                        str(FW / "src/battery_monitor.cpp"), "-o", str(binary)], check=True)
        print(f"=== {arch} hardware-call contract ===", flush=True)
        failed |= subprocess.run([str(binary)]).returncode != 0
    raise SystemExit(1 if failed else 0)
