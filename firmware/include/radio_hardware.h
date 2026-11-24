#pragma once
#include <RadioLib.h>
#include "radio_layout.h"

// Preserve OpenHop's preamble-aware receive mask and SX126x patch on every chip.
class OpenHopSX1262 : public SX1262 {
public:
    using SX1262::SX1262;
    int16_t startReceive() override;
    int16_t applyRegisterPatch08B5();
    struct IrqSample { int16_t spiResult; uint16_t flags; bool valid; };
    IrqSample readIrqChecked();
    bool dio1Asserted();
private:
    static int16_t strictIrqStatus(uint8_t status);
};
class RadioHardware {
public:
    static RadioHardware* create(const RadioDescriptor& descriptor);
    static bool validBindings();
    static void prepareBoot(uint8_t enabledMask = 0xff);
    static void enableRails(uint8_t enabledMask = 0xff);
    ~RadioHardware();
    OpenHopSX1262& chip() { return *chip_; }
private:
    RadioHardware(Module* module, OpenHopSX1262* chip) : module_(module), chip_(chip) {}
    Module* module_;
    OpenHopSX1262* chip_;
};
