#include "radio_layout.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    assert(RadioLayout::valid() && RadioLayout::fixedPorts());
    assert(RadioLayout::count()==2);
    assert(std::strcmp(BOARD.name,"EtherMesh-Duo")==0);
    const int pins[2][8]={{20,21,22,23,26,27,32,33},{15,18,17,16,14,19,6,5}};
    for (size_t i=0; i<2; ++i) {
        const auto& d=RadioLayout::at(i);
        const auto& hw=*d.hardware;
        assert(std::strcmp(d.label,i==0?"RF1":"RF2")==0);
        assert(RadioLayout::port(i,7000)==5055+i);
        assert(d.spi_bus==int(i));
        assert(hw.has_lora_radio && !hw.has_wifi && hw.has_network);
        assert(hw.pin_lora_nss==pins[i][0]);
        assert(hw.pin_lora_sck==pins[i][1]);
        assert(hw.pin_lora_mosi==pins[i][2]);
        assert(hw.pin_lora_miso==pins[i][3]);
        assert(hw.pin_lora_rst==pins[i][4]);
        assert(hw.static_gpio_count==1);
        assert(hw.static_gpios[0].pin==pins[i][5]);
        assert(hw.static_gpios[0].level_high);
        assert(hw.pin_lora_busy==pins[i][6]);
        assert(hw.pin_lora_dio1==pins[i][7]);
        assert(hw.rf_switch.en_pin==-1 && hw.rf_switch.en_low_hold_ms==0);
        assert(hw.rf_switch.rx_pin==-1 && hw.rf_switch.tx_pin==-1);
        assert(hw.rf_switch.dio2_as_rf_switch);
        assert(hw.use_dio3_tcxo && hw.tcxo_voltage==1.8f);
        assert(hw.sx126x_current_limit_ma==140);
        assert(hw.sx126x_rx_boosted_gain && hw.sx126x_register_patch);
        assert(hw.max_tx_power_dbm==22); // SX1262 drive, NOT external EIRP
        assert(hw.ethernet.enabled && hw.ethernet.pin_mdc==31);
        assert(hw.ethernet.pin_mdio==52 && hw.ethernet.pin_phy_reset==51);
        assert(hw.pin_user_button==-1);
    }
    assert(RadioLayout::at(0).hardware!=RadioLayout::at(1).hardware);
    std::cout<<"PASS: Duo two real radio descriptors, separate SPI/IRQ, ports and RAK policy\n";
}
