#!/usr/bin/env python3
"""Host execution of production PR79 control paths; no hardware claims."""
from pathlib import Path
import subprocess
import tempfile

FW = Path(__file__).resolve().parents[1]

def function(text, marker):
    start = text.index(marker)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

with tempfile.TemporaryDirectory(prefix='pr79-host-') as directory:
    tmp = Path(directory)
    def run(name, source, flags=()):
        path = tmp / (name + '.cpp')
        path.write_text(source)
        binary = tmp / name
        subprocess.run(['g++', '-std=c++17', '-Werror', '-Wno-missing-field-initializers',
                        '-I' + str(FW / 'include'), *flags, str(path), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print('PASS ' + name, flush=True)

    for board in ('HELTEC_T114', 'XIAO_NRF52_WIO', 'RAK4631_WISMESH_ETH', 'RAK4631_USB', 'RAK3401'):
        run(board, '''#include "board_config.h"
#include <cassert>
int main(){assert(BOARD.max_tx_power_dbm>=21 && BOARD.max_tx_power_dbm<=22); assert(BOARD.thermistor.ntc_pin==-1);
assert(BOARD.thermistor.fan_pin==-1); assert(BOARD.pa_ramp_time_us==0);
BatterySenseConfig b{5,-1,true,0,0,2,0,3000,12,173,100,8,2500,4500,4};
assert(b.minimum_plausible_mv==2500 && b.maximum_plausible_mv==4500);
assert(b.minimum_valid_samples==4 && b.adc_attenuation_db==-1);}
''', ['-DBOARD_' + board])

    apply = function((FW / 'src/main.cpp').read_text(), 'bool applyConfig(const RadioConfig& cfg)')
    run('apply_config_every_failure', '''#include "board_config.h"
#include "pa_ramp.h"
#include <cassert>
#include <string>
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_SX126X_PA_RAMP_1700U 6
#define LOG_R_INFO(...) ((void)0)
bool radioTxReady=true;
struct RadioConfig {uint32_t freq_hz=868000000,bandwidth_hz=125000; int sf=7,cr=5,power_dbm=22,syncword=18,preamble_len=8;};
struct FakeRadio {int calls=0,fail=0; int step(){assert(!radioTxReady); return ++calls==fail?-1:0;}
int standby(){return step();} int setFrequency(float){return step();}
int setBandwidth(float){return step();} int setSpreadingFactor(int){return step();}
int setCodingRate(int){return step();} int setOutputPower(int){return step();}
int setPaRampTime(int){return step();} int getCurrentLimit(){return 140;}
int setSyncWord(int){return step();} int setPreambleLength(int){return step();}
int explicitHeader(){return step();} int setCRC(int){return step();}
int invertIQ(bool){return step();} int autoLDRO(){return step();}} radio;
struct Display {int calls=0; template<class... T> void setRadioInfo(T...){assert(radioTxReady);++calls;}} oled;
struct {int last_rssi=0,last_snr=0;} status;
std::string fwVersion;
''' + apply + '''
int main(){RadioConfig cfg; for(int fail=1;fail<=13;++fail){radio={0,fail}; radioTxReady=true; oled.calls=0;
assert(!applyConfig(cfg)); assert(!radioTxReady); assert(radio.calls==fail); assert(oled.calls==0);}
radio={};assert(applyConfig(cfg));assert(radio.calls==13 && radioTxReady && oled.calls==1);}
''', ['-DBOARD_LILYGO_TBEAM_1W'])

    run('non_target_fan_no_esp_adc_dependency', '''#include "''' + str(FW / 'src/tbeam_1w_fan.cpp') + '''"
#include <cassert>
int main(){TBeam1WFan::begin(); TBeam1WFan::powerOff();
assert(!TBeam1WFan::isEnabled()); assert(!std::isfinite(TBeam1WFan::temperatureC()));}
''', ['-DARDUINO_ARCH_ESP32', '-DBOARD_PHOTON_1W_XIAO_ESP32C6'])

    task = function((FW / 'src/tbeam_1w_fan.cpp').read_text(), 'void task(void*)')
    run('fan_full_batch_failure', '''#include "board_config.h"
#include "tbeam_1w_fan.h"
#include <cassert>
#include <vector>
using namespace TBeam1WFan;
using esp_err_t=int; using adc_unit_t=int; using adc_channel_t=int;
using adc_oneshot_unit_handle_t=void*; using adc_cali_handle_t=void*;
constexpr int ESP_OK=0,ESP_ERR_INVALID_RESPONSE=1,ADC_UNIT_2=2,OUTPUT=1,HIGH=1,LOW=0;
constexpr int ADC_RTC_CLK_SRC_DEFAULT=0,ADC_ULP_MODE_DISABLE=0,ADC_ATTEN_DB_12=12,ADC_BITWIDTH_12=12;
struct adc_oneshot_unit_init_cfg_t{int unit_id,clk_src,ulp_mode;};
struct adc_oneshot_chan_cfg_t{int atten,bitwidth;};
struct adc_cali_curve_fitting_config_t{int unit_id,chan,atten,bitwidth;};
struct {template<class... T> void printf(T...) {}} Serial;
int mutex=0; float cachedTemperature=NAN; uint32_t cachedAt=0; bool cachedFanEnabled=true,shutdown=false;
int sample=0,batch=0,failureIndex=0,failureKind=0; std::vector<int> fan;
void pinMode(int,int){} void digitalWrite(int,int level){fan.push_back(level);}
uint32_t millis(){return 100;}
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#define pdMS_TO_TICKS(x) (x)
void vTaskDelete(void*){} void vTaskDelay(int){if(++batch==2)throw 1;}
int adc_oneshot_io_to_channel(int,int* u,int* c){*u=2;*c=1;return 0;}
int adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t*,void**){return 0;}
int adc_oneshot_config_channel(void*,int,const adc_oneshot_chan_cfg_t*){return 0;}
int adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t*,void**){return 0;}
int adc_oneshot_get_calibrated_result(void*,void*,int,int* mv){
int i=sample++%8; *mv=1650;
if(batch==1 && i==failureIndex){*mv=failureKind==0?0:failureKind==1?-1:3100;return failureKind==3?9:0;}return 0;}
void adc_oneshot_del_unit(void*){} void adc_cali_delete_scheme_curve_fitting(void*){}
''' + task + '''
int main(){for(failureKind=0;failureKind<4;++failureKind)for(failureIndex=0;failureIndex<8;++failureIndex){
sample=batch=0;fan.clear();cachedFanEnabled=true;cachedTemperature=0;
try{task(nullptr);}catch(int){}
assert(fan.size()==3 && fan[0]==HIGH && fan[1]==LOW && fan[2]==HIGH);
assert(cachedFanEnabled && !std::isfinite(cachedTemperature));}}
''', ['-DBOARD_LILYGO_TBEAM_1W'])
