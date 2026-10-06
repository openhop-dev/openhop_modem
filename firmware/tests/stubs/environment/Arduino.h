#pragma once
#include <cstdint>

extern uint32_t testMillis;
inline uint32_t millis() { return testMillis; }
inline void delayMicroseconds(uint32_t us) { testMillis += (us + 999) / 1000; }
struct TestSerial {
    void println(const char*) {}
    template <typename... Args> void printf(const char*, Args...) {}
};
extern TestSerial Serial;
