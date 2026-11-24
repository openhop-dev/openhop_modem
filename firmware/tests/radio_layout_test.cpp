#include "radio_layout.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    RadioLayout::Plan legacy(BOARD);
    assert(legacy.count() == 1);
    assert(legacy.valid());
    assert(!legacy.fixedPorts());
    assert(legacy.at(0).hardware == &BOARD);
    assert(legacy.port(0, 6060) == 6060);
    assert(legacy.port(0, 0) == 5055);
    assert(legacy.at(1).hardware == nullptr);
    assert(legacy.port(1, 6060) == 0);

    BoardConfig first = BOARD, second = BOARD;
    // An unrelated product, not EtherMesh-Duo: prove descriptor behavior is generic.
    first.pin_lora_nss=20; first.pin_lora_rst=26; first.pin_lora_busy=32;
    first.pin_lora_dio1=33; first.pin_lora_sck=21;
    first.pin_lora_miso=23; first.pin_lora_mosi=22;
    second.pin_lora_nss=15; second.pin_lora_rst=14; second.pin_lora_busy=6;
    second.pin_lora_dio1=5; second.pin_lora_sck=18;
    second.pin_lora_miso=16; second.pin_lora_mosi=17;
    RadioDescriptor descriptors[] = {{"uplink", &first, 5055, 0},
                                     {"downlink", &second, 5056, 1}};
    BoardConfig device = BOARD;
    device.radios=descriptors; device.radio_count=2;
    RadioLayout::Plan dual(device);
    assert(dual.valid() && dual.fixedPorts() && dual.count()==2);
    assert(dual.at(0).hardware==&first && dual.at(1).hardware==&second);
    assert(std::strcmp(dual.at(1).label,"downlink")==0);
    assert(dual.port(0,9999)==5055 && dual.port(1,9999)==5056);
    assert(dual.acceptsPrimaryPort(5055));
    assert(!dual.acceptsPrimaryPort(5056));
    assert(legacy.acceptsPrimaryPort(6060));
    assert(!legacy.acceptsPrimaryPort(0) && !legacy.acceptsPrimaryPort(80));
    assert(dual.at(2).hardware==nullptr);

    descriptors[1].tcp_port=5055; assert(!dual.valid());
    descriptors[1].tcp_port=80; assert(!dual.valid());
    descriptors[1].tcp_port=0; assert(!dual.valid());
    descriptors[1].tcp_port=5056;
    descriptors[1].hardware=nullptr; assert(!dual.valid());
    descriptors[1].hardware=&second;
    descriptors[1].label="uplink"; assert(!dual.valid());
    descriptors[1].label="downlink";
    second.pin_lora_dio1=33; assert(!dual.valid()); second.pin_lora_dio1=5;

    // Sharing one physical bus is valid only with matching bus pins and
    // distinct chip selects/control lines. Count is not SPI-controller count.
    descriptors[1].spi_bus=0; assert(!dual.valid());
    second.pin_lora_sck=21; second.pin_lora_miso=23; second.pin_lora_mosi=22;
    assert(dual.valid());
    second.pin_lora_nss=20; assert(!dual.valid()); second.pin_lora_nss=15;
    second.pin_lora_rst=26; assert(!dual.valid()); second.pin_lora_rst=14;
    second.pin_lora_busy=32; assert(!dual.valid()); second.pin_lora_busy=6;
    assert(dual.valid());
    device.radio_count=5; assert(!RadioLayout::Plan(device).valid());
    device.radio_count=0; assert(!RadioLayout::Plan(device).valid());
    device.radios=nullptr; device.radio_count=2;
    assert(!RadioLayout::Plan(device).valid());
    std::cout << "PASS: generic per-radio layout, legacy adapter, ports, bounds, shared-bus admission\n";
}
