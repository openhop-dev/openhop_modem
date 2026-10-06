#include "rf_frontend.h"

#include "board_config.h"

#ifdef ARDUINO_ARCH_ESP32
#include <Preferences.h>
#endif

namespace RFFrontEnd {

namespace {

#ifdef ARDUINO_ARCH_ESP32
static constexpr const char* NVS_NAMESPACE = "lora_modem";
static constexpr const char* STATION_G3_RF_CONFIG_KEY = "g3_rf_cfg";
static constexpr const char* STATION_G3_PA_HIGH_KEY = "g3_pa_high";
static constexpr const char* STATION_G3_LNA_ENABLED_KEY = "g3_lna_en";
static constexpr uint8_t STATION_G3_PA_HIGH_BIT = 0x01;
static constexpr uint8_t STATION_G3_LNA_ENABLED_BIT = 0x02;
#endif

static bool paHighPowerEnabled = false;
static bool stationG3LnaEnabled = true;
static bool stationG3InReceive = false;

static void writeConfiguredLevel(int8_t pin, bool active, bool activeHigh) {
    if (pin < 0) return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, active == activeHigh ? HIGH : LOW);
}

static void setConfiguredLna(bool enabled) {
    writeConfiguredLevel(BOARD.rf_frontend.lna_mode_pin,
                         enabled,
                         BOARD.rf_frontend.lna_enabled_active_high);
}

#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
static constexpr int8_t HELTEC_V43_CTX_PIN = 5;
static constexpr const char* V43_LNA_BYPASS_KEY = "v43_lna_bp";
static bool femLnaBypassed = true;

static void writeHeltecV43Ctx(bool bypassLna, const char* reason) {
    pinMode(HELTEC_V43_CTX_PIN, OUTPUT);
    // KCT8103L CTX: HIGH = RX LNA bypass, LOW = RX LNA enabled.
    // Keep CTX HIGH for TX regardless of the saved RX-LNA setting; holding
    // CTX LOW selects the FEM receive-LNA path and can block the TX path.
    digitalWrite(HELTEC_V43_CTX_PIN, bypassLna ? HIGH : LOW);
    Serial.printf("[RF] Heltec V4.3 FEM RX LNA %s for %s (GPIO%d=%s)\n",
                  bypassLna ? "bypassed" : "enabled",
                  reason ? reason : "state",
                  (int)HELTEC_V43_CTX_PIN,
                  bypassLna ? "HIGH" : "LOW");
}

static void applyHeltecV43LnaState() {
    writeHeltecV43Ctx(femLnaBypassed, "RX");
}
#endif

#if (defined(BOARD_HELTEC_V43) || defined(BOARD_STATION_G2) || defined(BOARD_STATION_G3)) && defined(ARDUINO_ARCH_ESP32)
static constexpr uint16_t MAX_AGC_RESET_INTERVAL_SEC = 3600;
#if defined(BOARD_HELTEC_V43)
static constexpr const char* AGC_RESET_INTERVAL_KEY = "v43_agc_sec";
#elif defined(BOARD_STATION_G3)
static constexpr const char* AGC_RESET_INTERVAL_KEY = "g3_agc_sec";
#else
static constexpr const char* AGC_RESET_INTERVAL_KEY = "g2_agc_sec";
#endif
static uint16_t agcResetIntervalSec = 0;
#endif

}  // namespace

void begin() {
    // Establish the conservative PA level before touching NVS so GPIO9 cannot
    // float into the higher-power mode during boot.
    paHighPowerEnabled = false;
    writeConfiguredLevel(BOARD.rf_frontend.pa_mode_pin,
                         false,
                         BOARD.rf_frontend.pa_high_active_high);
    stationG3LnaEnabled = false;
    stationG3InReceive = false;
    setConfiguredLna(false);

    paHighPowerEnabled = BOARD.rf_frontend.pa_default_high;
    stationG3LnaEnabled = BOARD.rf_frontend.lna_default_enabled;
#ifdef ARDUINO_ARCH_ESP32
    if (hasPaModeControl() && hasStationG3LnaControl()) {
        Preferences p;
        if (p.begin(NVS_NAMESPACE, true)) {
            if (p.isKey(STATION_G3_RF_CONFIG_KEY)) {
                uint8_t packed = p.getUChar(STATION_G3_RF_CONFIG_KEY, 0);
                paHighPowerEnabled = (packed & STATION_G3_PA_HIGH_BIT) != 0;
                stationG3LnaEnabled = (packed & STATION_G3_LNA_ENABLED_BIT) != 0;
            } else {
                // Migrate settings written by early Station G3 development builds.
                paHighPowerEnabled = p.getBool(STATION_G3_PA_HIGH_KEY,
                                               BOARD.rf_frontend.pa_default_high);
                stationG3LnaEnabled = p.getBool(STATION_G3_LNA_ENABLED_KEY,
                                                BOARD.rf_frontend.lna_default_enabled);
            }
            p.end();
        }
    } else if (hasPaModeControl()) {
        Preferences p;
        if (p.begin(NVS_NAMESPACE, true)) {
            paHighPowerEnabled = p.getBool(STATION_G3_PA_HIGH_KEY,
                                           BOARD.rf_frontend.pa_default_high);
            p.end();
        }
    }
#endif
    writeConfiguredLevel(BOARD.rf_frontend.pa_mode_pin,
                         paHighPowerEnabled,
                         BOARD.rf_frontend.pa_high_active_high);
    // The external LNA is RX-only. Leave it bypassed until prepareReceive().
    setConfiguredLna(false);
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    Preferences p;
    if (p.begin(NVS_NAMESPACE, true)) {
        femLnaBypassed = p.getBool(V43_LNA_BYPASS_KEY, true);
        p.end();
    }
    applyHeltecV43LnaState();
#endif
#if (defined(BOARD_HELTEC_V43) || defined(BOARD_STATION_G2) || defined(BOARD_STATION_G3)) && defined(ARDUINO_ARCH_ESP32)
    agcResetIntervalSec =
        (uint16_t)(BOARD.sx126x_agc_reset_interval_ms / 1000U);
    Preferences agcPrefs;
    if (agcPrefs.begin(NVS_NAMESPACE, true)) {
        agcResetIntervalSec = agcPrefs.getUShort(
            AGC_RESET_INTERVAL_KEY,
            (uint16_t)(BOARD.sx126x_agc_reset_interval_ms / 1000U));
        agcPrefs.end();
    }
    if (agcResetIntervalSec > MAX_AGC_RESET_INTERVAL_SEC) {
        agcResetIntervalSec = MAX_AGC_RESET_INTERVAL_SEC;
    }
    Serial.printf("[AGC] %s agc.reset.interval=%u s\n",
                  BOARD.name, (unsigned)agcResetIntervalSec);
#endif
}

bool hasPaModeControl() {
    return BOARD.rf_frontend.pa_user_selectable &&
           BOARD.rf_frontend.pa_mode_pin >= 0;
}

bool isPaHighPowerEnabled() {
    return hasPaModeControl() && paHighPowerEnabled;
}

bool setPaHighPowerEnabled(bool enabled, bool persist) {
    if (!hasPaModeControl()) return false;

    if (hasStationG3LnaControl()) {
        return setStationG3RfConfig(enabled, stationG3LnaEnabled, persist);
    }

    if (persist) {
#ifdef ARDUINO_ARCH_ESP32
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
    writeConfiguredLevel(BOARD.rf_frontend.pa_mode_pin,
                         paHighPowerEnabled,
                         BOARD.rf_frontend.pa_high_active_high);
    return true;
}

bool hasStationG3LnaControl() {
    return BOARD.rf_frontend.lna_user_selectable &&
           BOARD.rf_frontend.lna_mode_pin >= 0;
}

bool isStationG3LnaEnabled() {
    return hasStationG3LnaControl() && stationG3LnaEnabled;
}

bool setStationG3LnaEnabled(bool enabled, bool persist) {
    if (!hasStationG3LnaControl()) return false;

    return setStationG3RfConfig(paHighPowerEnabled, enabled, persist);
}

bool setStationG3RfConfig(bool paHighPower, bool lnaEnabled, bool persist) {
    if (!hasPaModeControl() || !hasStationG3LnaControl()) return false;

    if (persist) {
#ifdef ARDUINO_ARCH_ESP32
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
    writeConfiguredLevel(BOARD.rf_frontend.pa_mode_pin,
                         paHighPowerEnabled,
                         BOARD.rf_frontend.pa_high_active_high);
    setConfiguredLna(stationG3InReceive && stationG3LnaEnabled);
    return true;
}

bool hasHeltecV43LnaControl() {
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    return true;
#else
    return false;
#endif
}

bool isFemLnaBypassed() {
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    return femLnaBypassed;
#else
    return false;
#endif
}

bool isExternalLnaEnabled() {
    return hasHeltecV43LnaControl() && !isFemLnaBypassed();
}

bool setFemLnaBypassed(bool bypass, bool persist) {
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    if (persist) {
        Preferences p;
        if (!p.begin(NVS_NAMESPACE, false)) return false;
        bool ok = p.putBool(V43_LNA_BYPASS_KEY, bypass) > 0;
        p.end();
        if (!ok) return false;
    }

    femLnaBypassed = bypass;
    applyHeltecV43LnaState();
    return true;
#else
    (void)bypass;
    (void)persist;
    return false;
#endif
}

void prepareTransmit() {
    // A board-declared external LNA is receive-only. Bypass it before the
    // SX1262 enters TX so the front end cannot hold the RF path in RX mode.
    stationG3InReceive = false;
    setConfiguredLna(false);
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    // The web UI's "external LNA enabled" setting is RX-only. Always bypass
    // the FEM RX LNA before TX/CAD-to-TX so KCT8103L does not hold the RF path
    // in receive-LNA mode.
    writeHeltecV43Ctx(true, "TX");
#endif
}

void prepareReceive() {
    stationG3InReceive = true;
    setConfiguredLna(hasStationG3LnaControl() ? stationG3LnaEnabled : true);
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    applyHeltecV43LnaState();
#endif
}

void prepareStandby() {
    stationG3InReceive = false;
    setConfiguredLna(false);
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    writeHeltecV43Ctx(true, "standby");
#endif
}

uint16_t getAgcResetIntervalSec() {
#if (defined(BOARD_HELTEC_V43) || defined(BOARD_STATION_G2) || defined(BOARD_STATION_G3)) && defined(ARDUINO_ARCH_ESP32)
    return agcResetIntervalSec;
#else
    return 0;
#endif
}

bool hasAgcResetIntervalControl() {
#if (defined(BOARD_HELTEC_V43) || defined(BOARD_STATION_G2) || defined(BOARD_STATION_G3)) && defined(ARDUINO_ARCH_ESP32)
    return true;
#else
    return false;
#endif
}

bool setAgcResetIntervalSec(uint16_t intervalSec, bool persist) {
#if (defined(BOARD_HELTEC_V43) || defined(BOARD_STATION_G2) || defined(BOARD_STATION_G3)) && defined(ARDUINO_ARCH_ESP32)
    if (intervalSec > MAX_AGC_RESET_INTERVAL_SEC) {
        intervalSec = MAX_AGC_RESET_INTERVAL_SEC;
    }
    if (persist) {
        Preferences p;
        if (!p.begin(NVS_NAMESPACE, false)) return false;
        bool ok = p.putUShort(AGC_RESET_INTERVAL_KEY, intervalSec) > 0;
        p.end();
        if (!ok) return false;
    }

    agcResetIntervalSec = intervalSec;
    return true;
#else
    (void)intervalSec;
    (void)persist;
    return false;
#endif
}

}  // namespace RFFrontEnd
