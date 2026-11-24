#include "rf_frontend.h"
#include "radio_layout.h"
#ifdef ARDUINO_ARCH_ESP32
#include <Preferences.h>
#endif
namespace RFFrontEnd {
namespace {
constexpr const char* NVS_NAMESPACE = "lora_modem";
constexpr const char* STATION_G3_RF_CONFIG_KEY = "g3_rf_cfg";
constexpr const char* STATION_G3_PA_HIGH_KEY = "g3_pa_high";
constexpr const char* STATION_G3_LNA_ENABLED_KEY = "g3_lna_en";
constexpr const char* V43_LNA_BYPASS_KEY = "v43_lna_bp";
constexpr uint8_t STATION_G3_PA_HIGH_BIT = 0x01;
constexpr uint8_t STATION_G3_LNA_ENABLED_BIT = 0x02;
constexpr int8_t HELTEC_V43_CTX_PIN = 5;
void writeConfiguredLevel(int8_t pin, bool active, bool activeHigh) {
    if (pin < 0) return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, active == activeHigh ? HIGH : LOW);
}
}
Controller::Controller(const BoardConfig& hardware, bool primary)
    : hardware_(hardware), primary_(primary) {
#ifdef ARDUINO_ARCH_ESP32
    if (primary_) {
#if defined(BOARD_HELTEC_V43)
        heltecV43_ = true;
        agcKey_ = "v43_agc_sec";
#elif defined(BOARD_STATION_G3)
        agcKey_ = "g3_agc_sec";
#elif defined(BOARD_STATION_G2)
        agcKey_ = "g2_agc_sec";
#endif
    }
#endif
}
void Controller::setConfiguredLna(bool enabled) {
    writeConfiguredLevel(hardware_.rf_frontend.lna_mode_pin, enabled,
                         hardware_.rf_frontend.lna_enabled_active_high);
}
void Controller::writeHeltecV43Ctx(bool bypassLna, const char* reason) {
    (void)reason;
    pinMode(HELTEC_V43_CTX_PIN, OUTPUT);
    digitalWrite(HELTEC_V43_CTX_PIN, bypassLna ? HIGH : LOW);
}
void Controller::applyHeltecV43LnaState() {
    writeHeltecV43Ctx(femLnaBypassed, "RX");
}
void Controller::begin() {
    agcResetIntervalSec = (uint16_t)(hardware_.sx126x_agc_reset_interval_ms / 1000U);
    // Establish the conservative PA level before touching NVS so GPIO9 cannot
    // float into the higher-power mode during boot.
    paHighPowerEnabled = false;
    writeConfiguredLevel(hardware_.rf_frontend.pa_mode_pin,
                         false,
                         hardware_.rf_frontend.pa_high_active_high);
    stationG3LnaEnabled = false;
    stationG3InReceive = false;
    setConfiguredLna(false);

    paHighPowerEnabled = hardware_.rf_frontend.pa_default_high;
    stationG3LnaEnabled = hardware_.rf_frontend.lna_default_enabled;
#ifdef ARDUINO_ARCH_ESP32
    if (primary_ && hasPaModeControl() && hasStationG3LnaControl()) {
        Preferences p;
        if (p.begin(NVS_NAMESPACE, true)) {
            if (p.isKey(STATION_G3_RF_CONFIG_KEY)) {
                uint8_t packed = p.getUChar(STATION_G3_RF_CONFIG_KEY, 0);
                paHighPowerEnabled = (packed & STATION_G3_PA_HIGH_BIT) != 0;
                stationG3LnaEnabled = (packed & STATION_G3_LNA_ENABLED_BIT) != 0;
            } else {
                // Migrate settings written by early Station G3 development builds.
                paHighPowerEnabled = p.getBool(STATION_G3_PA_HIGH_KEY,
                                               hardware_.rf_frontend.pa_default_high);
                stationG3LnaEnabled = p.getBool(STATION_G3_LNA_ENABLED_KEY,
                                                hardware_.rf_frontend.lna_default_enabled);
            }
            p.end();
        }
    } else if (primary_ && hasPaModeControl()) {
        Preferences p;
        if (p.begin(NVS_NAMESPACE, true)) {
            paHighPowerEnabled = p.getBool(STATION_G3_PA_HIGH_KEY,
                                           hardware_.rf_frontend.pa_default_high);
            p.end();
        }
    }
#endif
    writeConfiguredLevel(hardware_.rf_frontend.pa_mode_pin,
                         paHighPowerEnabled,
                         hardware_.rf_frontend.pa_high_active_high);
    // The external LNA is RX-only. Leave it bypassed until prepareReceive().
    setConfiguredLna(false);
#ifdef ARDUINO_ARCH_ESP32
    if (heltecV43_) {
    Preferences p;
    if (p.begin(NVS_NAMESPACE, true)) {
        femLnaBypassed = p.getBool(V43_LNA_BYPASS_KEY, true);
        p.end();
    }
    applyHeltecV43LnaState();
    }
#endif
#ifdef ARDUINO_ARCH_ESP32
    if (primary_ && agcKey_) {
    agcResetIntervalSec =
        (uint16_t)(hardware_.sx126x_agc_reset_interval_ms / 1000U);
    Preferences agcPrefs;
    if (agcPrefs.begin(NVS_NAMESPACE, true)) {
        agcResetIntervalSec = agcPrefs.getUShort(
            agcKey_,
            (uint16_t)(hardware_.sx126x_agc_reset_interval_ms / 1000U));
        agcPrefs.end();
    }
    if (agcResetIntervalSec > MAX_AGC_RESET_INTERVAL_SEC) {
        agcResetIntervalSec = MAX_AGC_RESET_INTERVAL_SEC;
    }
    Serial.printf("[AGC] %s agc.reset.interval=%u s\n",
                  hardware_.name, (unsigned)agcResetIntervalSec);
    }
#endif
}

bool Controller::hasPaModeControl() {
    return hardware_.rf_frontend.pa_user_selectable &&
           hardware_.rf_frontend.pa_mode_pin >= 0;
}

bool Controller::isPaHighPowerEnabled() {
    return hasPaModeControl() && paHighPowerEnabled;
}

bool Controller::setPaHighPowerEnabled(bool enabled, bool persist) {
    if (!canMutate()) return false;
    if (!hasPaModeControl()) return false;

    if (hasStationG3LnaControl()) {
        return setStationG3RfConfig(enabled, stationG3LnaEnabled, persist);
    }

    if (persist) {
#ifdef ARDUINO_ARCH_ESP32
        if (!primary_) return false;
        Preferences p;
        if (!p.begin(NVS_NAMESPACE, false)) return false;
        bool ok = p.putBool(STATION_G3_PA_HIGH_KEY, enabled) > 0;
        p.end();
        if (!ok) return false;
#else
        return false;
#endif
    }

    paHighPowerEnabled = enabled;
    writeConfiguredLevel(hardware_.rf_frontend.pa_mode_pin,
                         paHighPowerEnabled,
                         hardware_.rf_frontend.pa_high_active_high);
    return true;
}

bool Controller::hasStationG3LnaControl() {
    return hardware_.rf_frontend.lna_user_selectable &&
           hardware_.rf_frontend.lna_mode_pin >= 0;
}

bool Controller::isStationG3LnaEnabled() {
    return hasStationG3LnaControl() && stationG3LnaEnabled;
}

bool Controller::setStationG3LnaEnabled(bool enabled, bool persist) {
    if (!canMutate()) return false;
    if (!hasStationG3LnaControl()) return false;

    return setStationG3RfConfig(paHighPowerEnabled, enabled, persist);
}

bool Controller::setStationG3RfConfig(bool paHighPower, bool lnaEnabled, bool persist) {
    if (!canMutate()) return false;
    if (!hasPaModeControl() || !hasStationG3LnaControl()) return false;

    if (persist) {
#ifdef ARDUINO_ARCH_ESP32
        if (!primary_) return false;
        Preferences p;
        if (!p.begin(NVS_NAMESPACE, false)) return false;
        uint8_t packed = (paHighPower ? STATION_G3_PA_HIGH_BIT : 0) |
                         (lnaEnabled ? STATION_G3_LNA_ENABLED_BIT : 0);
        bool ok = p.putUChar(STATION_G3_RF_CONFIG_KEY, packed) > 0;
        p.end();
        if (!ok) return false;
#else
        return false;
#endif
    }

    paHighPowerEnabled = paHighPower;
    stationG3LnaEnabled = lnaEnabled;
    writeConfiguredLevel(hardware_.rf_frontend.pa_mode_pin,
                         paHighPowerEnabled,
                         hardware_.rf_frontend.pa_high_active_high);
    setConfiguredLna(stationG3InReceive && stationG3LnaEnabled);
    return true;
}

bool Controller::hasHeltecV43LnaControl() { return heltecV43_; }
bool Controller::isFemLnaBypassed() { return heltecV43_ && femLnaBypassed; }

bool Controller::isExternalLnaEnabled() {
    return hasHeltecV43LnaControl() && !isFemLnaBypassed();
}

FemState Controller::getFemState() {
    FemState state;
    // Station G3 LNA writes go through setStationG3RfConfig(), which also
    // needs PA control; without it the bit could be read but never set.
    if (hasHeltecV43LnaControl() ||
        (hasStationG3LnaControl() && hasPaModeControl())) {
        state.capability |= FEM_STATE_RX_LNA;
    }
    if (hasPaModeControl()) {
        state.capability |= FEM_STATE_TX_PA;
    }
    if (hasHeltecV43LnaControl() && isExternalLnaEnabled()) {
        state.value |= FEM_STATE_RX_LNA;
    }
    if (hasStationG3LnaControl() && isStationG3LnaEnabled()) {
        state.value |= FEM_STATE_RX_LNA;
    }
    if (isPaHighPowerEnabled()) {
        state.value |= FEM_STATE_TX_PA;
    }
    state.value &= state.capability;
    return state;
}

bool Controller::setFemState(uint8_t apply, uint8_t value, bool persist, FemState& out) {
    if (!canMutate()) return false;
    const FemState current = getFemState();
    uint8_t next = current.value;
    if (applyFemMask(current.capability, current.value, apply, value, next) !=
        FemApplyStatus::Applied) {
        out = current;
        return false;
    }

    const bool lnaOn = (next & FEM_STATE_RX_LNA) != 0;
    const bool paHigh = (next & FEM_STATE_TX_PA) != 0;

    if ((apply & FEM_STATE_RX_LNA) != 0 && hasHeltecV43LnaControl()) {
        if (!setFemLnaBypassed(!lnaOn, persist)) {
            out = getFemState();
            return false;
        }
    }

    if (hasPaModeControl() && hasStationG3LnaControl()) {
        if ((apply & (FEM_STATE_RX_LNA | FEM_STATE_TX_PA)) != 0) {
            if (!setStationG3RfConfig(paHigh, lnaOn, persist)) {
                out = getFemState();
                return false;
            }
        }
    } else if ((apply & FEM_STATE_TX_PA) != 0 && hasPaModeControl()) {
        if (!setPaHighPowerEnabled(paHigh, persist)) {
            out = getFemState();
            return false;
        }
    } else if ((apply & FEM_STATE_RX_LNA) != 0 && hasStationG3LnaControl()) {
        if (!setStationG3LnaEnabled(lnaOn, persist)) {
            out = getFemState();
            return false;
        }
    }

    out = getFemState();
    return true;
}

bool Controller::setFemLnaBypassed(bool bypass, bool persist) {
    if (!canMutate()) return false;
    if (!hasHeltecV43LnaControl()) return false;
    if (persist) {
#ifdef ARDUINO_ARCH_ESP32
        if (!primary_) return false;
        Preferences p;
        if (!p.begin(NVS_NAMESPACE, false)) return false;
        bool ok = p.putBool(V43_LNA_BYPASS_KEY, bypass) > 0;
        p.end();
        if (!ok) return false;
#else
        return false;
#endif
    }
    femLnaBypassed = bypass;
    applyHeltecV43LnaState();
    return true;
}

void Controller::prepareTransmit() {
    // A board-declared external LNA is receive-only. Bypass it before the
    // SX1262 enters TX so the front end cannot hold the RF path in RX mode.
    stationG3InReceive = false;
    setConfiguredLna(false);
    if (heltecV43_) {
    // The web UI's "external LNA enabled" setting is RX-only. Always bypass
    // the FEM RX LNA before TX/CAD-to-TX so KCT8103L does not hold the RF path
    // in receive-LNA mode.
    writeHeltecV43Ctx(true, "TX");
    }
}

void Controller::prepareReceive() {
    stationG3InReceive = true;
    setConfiguredLna(hasStationG3LnaControl() ? stationG3LnaEnabled : true);
    if (heltecV43_) {
    applyHeltecV43LnaState();
    }
}

void Controller::prepareStandby() {
    stationG3InReceive = false;
    setConfiguredLna(false);
    if (heltecV43_) {
    writeHeltecV43Ctx(true, "standby");
    }
}

uint16_t Controller::getAgcResetIntervalSec() { return agcResetIntervalSec; }
bool Controller::hasAgcResetIntervalControl() { return agcKey_ != nullptr; }
bool Controller::setAgcResetIntervalSec(uint16_t intervalSec, bool persist) {
    if (!canMutate(false)) return false;
    if (intervalSec > MAX_AGC_RESET_INTERVAL_SEC) intervalSec = MAX_AGC_RESET_INTERVAL_SEC;
    if (persist) {
#ifdef ARDUINO_ARCH_ESP32
        if (!primary_ || !agcKey_) return false;
        Preferences p;
        if (!p.begin(NVS_NAMESPACE, false)) return false;
        bool ok = p.putUShort(agcKey_, intervalSec) > 0;
        p.end();
        if (!ok) return false;
#else
        return false;
#endif
    }
    agcResetIntervalSec = intervalSec;
    return true;
}
Controller& primary() {
    static Controller controller(*RadioLayout::at(0).hardware, true);
    return controller;
}
void begin() { primary().begin(); }
bool hasPaModeControl() { return primary().hasPaModeControl(); }
bool isPaHighPowerEnabled() { return primary().isPaHighPowerEnabled(); }
bool setPaHighPowerEnabled(bool enabled, bool persist) { return primary().setPaHighPowerEnabled(enabled, persist); }
bool hasStationG3LnaControl() { return primary().hasStationG3LnaControl(); }
bool isStationG3LnaEnabled() { return primary().isStationG3LnaEnabled(); }
bool setStationG3LnaEnabled(bool enabled, bool persist) { return primary().setStationG3LnaEnabled(enabled, persist); }
bool setStationG3RfConfig(bool paHighPower, bool lnaEnabled, bool persist) { return primary().setStationG3RfConfig(paHighPower, lnaEnabled, persist); }
bool hasHeltecV43LnaControl() { return primary().hasHeltecV43LnaControl(); }
bool isFemLnaBypassed() { return primary().isFemLnaBypassed(); }
bool isExternalLnaEnabled() { return primary().isExternalLnaEnabled(); }
bool setFemLnaBypassed(bool bypass, bool persist) { return primary().setFemLnaBypassed(bypass, persist); }
bool hasAgcResetIntervalControl() { return primary().hasAgcResetIntervalControl(); }
FemState getFemState() { return primary().getFemState(); }
bool setFemState(uint8_t apply, uint8_t value, bool persist, FemState& out) { return primary().setFemState(apply, value, persist, out); }
void prepareTransmit() { primary().prepareTransmit(); }
void prepareReceive() { primary().prepareReceive(); }
void prepareStandby() { primary().prepareStandby(); }
uint16_t getAgcResetIntervalSec() { return primary().getAgcResetIntervalSec(); }
bool setAgcResetIntervalSec(uint16_t intervalSec, bool persist) { return primary().setAgcResetIntervalSec(intervalSec, persist); }
} // namespace RFFrontEnd
