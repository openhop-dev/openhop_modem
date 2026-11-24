#pragma once
#include <Arduino.h>
#include "rf_fem_state.h"
#include "board_config.h"
namespace RFFrontEnd {
constexpr uint16_t MAX_AGC_RESET_INTERVAL_SEC = 3600;
// Hardware and mutable front-end state are permanently owned by one radio.
// Only the primary controller may access the historical device NVS keys.
class Controller {
public:
    explicit Controller(const BoardConfig& hardware, bool primary = false);
    using MutationGuard = bool (*)(void*, bool receiveSensitive);
    void setMutationGuard(MutationGuard guard, void* owner) { guard_ = guard; guardOwner_ = owner; }
    void begin();
    bool hasPaModeControl();
    bool isPaHighPowerEnabled();
    bool setPaHighPowerEnabled(bool enabled, bool persist);
    bool hasStationG3LnaControl();
    bool isStationG3LnaEnabled();
    bool setStationG3LnaEnabled(bool enabled, bool persist);
    bool setStationG3RfConfig(bool paHighPower, bool lnaEnabled, bool persist);
    bool hasHeltecV43LnaControl();
    bool isFemLnaBypassed();
    bool isExternalLnaEnabled();
    bool setFemLnaBypassed(bool bypass, bool persist);
    bool hasAgcResetIntervalControl();
    FemState getFemState();
    bool setFemState(uint8_t apply, uint8_t value, bool persist, FemState& out);
    void prepareTransmit();
    void prepareReceive();
    void prepareStandby();
    uint16_t getAgcResetIntervalSec();
    bool setAgcResetIntervalSec(uint16_t intervalSec, bool persist);
private:
    MutationGuard guard_ = nullptr;
    void* guardOwner_ = nullptr;
    bool canMutate(bool receiveSensitive = true) { return !guard_ || guard_(guardOwner_, receiveSensitive); }
    const BoardConfig& hardware_;
    bool primary_;
    bool heltecV43_ = false;
    const char* agcKey_ = nullptr;
    bool paHighPowerEnabled = false;
    bool stationG3LnaEnabled = true;
    bool stationG3InReceive = false;
    bool femLnaBypassed = true;
    uint16_t agcResetIntervalSec = 0;
    void setConfiguredLna(bool enabled);
    void writeHeltecV43Ctx(bool bypassLna, const char* reason);
    void applyHeltecV43LnaState();
};
Controller& primary();
void begin();
bool hasPaModeControl();
bool isPaHighPowerEnabled();
bool setPaHighPowerEnabled(bool enabled, bool persist);
bool hasStationG3LnaControl();
bool isStationG3LnaEnabled();
bool setStationG3LnaEnabled(bool enabled, bool persist);
bool setStationG3RfConfig(bool paHighPower, bool lnaEnabled, bool persist);
bool hasHeltecV43LnaControl();
bool isFemLnaBypassed();
bool isExternalLnaEnabled();
bool setFemLnaBypassed(bool bypass, bool persist);
bool hasAgcResetIntervalControl();
FemState getFemState();
bool setFemState(uint8_t apply, uint8_t value, bool persist, FemState& out);
void prepareTransmit();
void prepareReceive();
void prepareStandby();
uint16_t getAgcResetIntervalSec();
bool setAgcResetIntervalSec(uint16_t intervalSec, bool persist);
}  // namespace RFFrontEnd
