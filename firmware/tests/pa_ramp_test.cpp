#include "../include/pa_ramp.h"

#include <cassert>
#include <cstdint>
#include <vector>

struct MockRadio {
    std::vector<char> calls;
    int16_t powerResult = 0;
    int16_t rampResult = 0;
    int16_t txResult = 0;
    bool txReady = false;

    int16_t setOutputPower(int8_t) {
        calls.push_back('P');
        txReady = false;
        return powerResult;
    }
    int16_t setPaRampTime(uint8_t) {
        calls.push_back('R');
        txReady = rampResult == 0;
        return rampResult;
    }
    int16_t startTransmit() {
        calls.push_back('T');
        return txReady ? txResult : -1;
    }
};

int main() {
    MockRadio radio;
    assert(applyOutputPowerAndRamp(radio, 22, 0x06) == 0);
    assert(radio.startTransmit() == 0);
    assert((radio.calls == std::vector<char>{'P', 'R', 'T'}));

    radio.calls.clear();
    radio.rampResult = -3;
    assert(applyOutputPowerAndRamp(radio, 5, 0x06) == -3);
    assert(radio.startTransmit() == -1);
    assert((radio.calls == std::vector<char>{'P', 'R', 'T'}));

    radio.calls.clear();
    radio.rampResult = 0;
    assert(applyOutputPowerAndRamp(radio, 10, 0x06) == 0);
    assert(radio.startTransmit() == 0);
    assert((radio.calls == std::vector<char>{'P', 'R', 'T'}));
    return 0;
}
