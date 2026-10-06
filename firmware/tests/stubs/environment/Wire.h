#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Register-level I2C fake. Production code and the real Bosch driver are linked
// unchanged; tests supply only the Arduino clock, serial output, and Wire bus.
struct TestWire {
    uint8_t registers[128][256] = {};
    bool present[128] = {};
    unsigned transactions = 0;
    unsigned writes[128] = {};
    int failReadRegister = -1;
    int failWriteRegister = -1;
    bool stuckCalibration = false;
    bool stuckMeasurement = false;
    bool shortWrite = false;
    uint16_t timeout = 0;
    uint8_t address = 0;
    uint8_t pointer = 0;
    std::vector<uint8_t> outgoing;
    std::vector<uint8_t> incoming;
    size_t readIndex = 0;

    void setTimeOut(uint16_t ms) { timeout = ms; }
    void beginTransmission(uint8_t addr);
    size_t write(uint8_t value);
    size_t write(const uint8_t* data, size_t length);
    uint8_t endTransmission(bool);
    size_t requestFrom(uint8_t addr, uint8_t length);
    int read();
};
extern TestWire Wire;
