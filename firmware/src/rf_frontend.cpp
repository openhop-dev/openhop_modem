#include "rf_frontend.h"

#include "board_config.h"

#ifdef ARDUINO_ARCH_ESP32
#include <Preferences.h>
#endif

namespace RFFrontEnd {

namespace {

#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
static constexpr int8_t HELTEC_V43_CTX_PIN = 5;
static constexpr const char* NVS_NAMESPACE = "lora_modem";
static constexpr const char* V43_LNA_BYPASS_KEY = "v43_lna_bp";
static constexpr const char* V43_AGC_RESET_INTERVAL_KEY = "v43_agc_sec";
static constexpr uint16_t MAX_AGC_RESET_INTERVAL_SEC = 3600;
static bool femLnaBypassed = true;
static uint16_t agcResetIntervalSec = 0;

static void applyHeltecV43LnaState() {
    pinMode(HELTEC_V43_CTX_PIN, OUTPUT);
    // KCT8103L CTX: HIGH = RX LNA bypass, LOW = RX LNA enabled.
    digitalWrite(HELTEC_V43_CTX_PIN, femLnaBypassed ? HIGH : LOW);
    Serial.printf("[RF] Heltec V4.3 FEM RX LNA %s (GPIO%d=%s)\n",
                  femLnaBypassed ? "bypassed" : "enabled",
                  (int)HELTEC_V43_CTX_PIN,
                  femLnaBypassed ? "HIGH" : "LOW");
}
#endif

}  // namespace

void begin() {
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    Preferences p;
    if (p.begin(NVS_NAMESPACE, true)) {
        femLnaBypassed = p.getBool(V43_LNA_BYPASS_KEY, true);
        agcResetIntervalSec = p.getUShort(V43_AGC_RESET_INTERVAL_KEY, 0);
        if (agcResetIntervalSec > MAX_AGC_RESET_INTERVAL_SEC) {
            agcResetIntervalSec = MAX_AGC_RESET_INTERVAL_SEC;
        }
        p.end();
    }
    applyHeltecV43LnaState();
    Serial.printf("[RF] Heltec V4.3 agc.reset.interval=%u s\n",
                  (unsigned)agcResetIntervalSec);
#endif
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
    femLnaBypassed = bypass;
    applyHeltecV43LnaState();

    if (persist) {
        Preferences p;
        if (!p.begin(NVS_NAMESPACE, false)) return false;
        bool ok = p.putBool(V43_LNA_BYPASS_KEY, femLnaBypassed) > 0;
        p.end();
        return ok;
    }
    return true;
#else
    (void)bypass;
    (void)persist;
    return false;
#endif
}

uint16_t getAgcResetIntervalSec() {
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    return agcResetIntervalSec;
#else
    return 0;
#endif
}

bool setAgcResetIntervalSec(uint16_t intervalSec, bool persist) {
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    if (intervalSec > MAX_AGC_RESET_INTERVAL_SEC) {
        intervalSec = MAX_AGC_RESET_INTERVAL_SEC;
    }
    agcResetIntervalSec = intervalSec;

    if (persist) {
        Preferences p;
        if (!p.begin(NVS_NAMESPACE, false)) return false;
        bool ok = p.putUShort(V43_AGC_RESET_INTERVAL_KEY, agcResetIntervalSec) > 0;
        p.end();
        return ok;
    }
    return true;
#else
    (void)intervalSec;
    (void)persist;
    return false;
#endif
}

}  // namespace RFFrontEnd
