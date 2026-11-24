// Test-only immutable carrier data; not a deployable board/pin mapping.
#include <array>
#ifndef NRADIO_COUNT
#error "NRADIO_COUNT must be explicit"
#endif
// The fallback lets the unchanged test execute a behavioral RED on the old
// source, which lacks these policy fields, instead of failing at compilation.
template<class T> auto nradioSelection(T& value, int) -> decltype(
    value.supports_radio_selection = bool{}, value.radio_default_enabled_mask = uint8_t{}, void()) {
    value.supports_radio_selection = NRADIO_SELECTABLE;
    value.radio_default_enabled_mask = NRADIO_DEFAULT_MASK;
}
template<class T> void nradioSelection(T&, long) {}
inline const std::array<BoardConfig, NRADIO_COUNT> NRADIO_HARDWARE = [] {
    std::array<BoardConfig, NRADIO_COUNT> radios{};
    for (size_t i = 0; i < radios.size(); ++i) {
        radios[i] = NRADIO_BASE;
        radios[i].pin_lora_nss = 1 + i * 8;
        radios[i].pin_lora_rst = 2 + i * 8;
        radios[i].pin_lora_busy = 3 + i * 8;
        radios[i].pin_lora_dio1 = 4 + i * 8;
        radios[i].pin_lora_sck = 5;
        radios[i].pin_lora_miso = 6;
        radios[i].pin_lora_mosi = 7;
        radios[i].pin_vext_enable_low = -1;
        radios[i].pin_user_button = -1;
        radios[i].pin_i2c_sda = -1;
        radios[i].pin_i2c_scl = -1;
        radios[i].pin_i2c_oled_rst = -1;
        radios[i].has_wifi = false;
        radios[i].boot_rx_frequency_hz = 915000000;
    }
    return radios;
}();
inline const char* NRADIO_LABELS[] = {"RF1", "RF2", "RF3", "RF4", "RF5"};
inline const std::array<RadioDescriptor, NRADIO_COUNT> NRADIO_DESCRIPTORS = [] {
    std::array<RadioDescriptor, NRADIO_COUNT> radios{};
    for (size_t i = 0; i < radios.size(); ++i)
        radios[i] = {NRADIO_LABELS[i], &NRADIO_HARDWARE[i], uint16_t(5055 + i), 0};
    return radios;
}();
inline const BoardConfig BOARD = [] {
    BoardConfig device = NRADIO_HARDWARE[0];
    device.name = "Host-only N-radio fixture";
    device.fw_suffix = "test-nradio";
    device.radios = NRADIO_DESCRIPTORS.data();
    device.radio_count = NRADIO_COUNT;
    device.ethernet.enabled = true;
    device.ethernet.phy_type = BoardConfig::EthernetPhy::IP101;
    device.ethernet.pin_mdc = 40;
    device.ethernet.pin_mdio = 41;
    device.ethernet.pin_phy_reset = 42;
    device.ethernet.phy_addr = 1;
    device.ethernet.rmii_clock_input = true;
    nradioSelection(device, 0);
    return device;
}();
