// Run both Station G2 and G3 host tests with:
//   python3 firmware/tools/test_environment_sensor.py
// The runner fetches the pinned Bosch dependency when needed. No hardware is accessed.
#include "environment_sensor.h"
#include <Arduino.h>
#include <Wire.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

uint32_t testMillis = 0;
TestSerial Serial;
TestWire Wire;

void TestWire::beginTransmission(uint8_t addr) {
    address = addr;
    outgoing.clear();
}
size_t TestWire::write(uint8_t value) {
    if (shortWrite) return 0;
    outgoing.push_back(value);
    return 1;
}
size_t TestWire::write(const uint8_t* data, size_t length) {
    if (shortWrite) return 0;
    outgoing.insert(outgoing.end(), data, data + length);
    return length;
}
uint8_t TestWire::endTransmission(bool) {
    ++transactions;
    if (!present[address] || outgoing.empty()) return 2;
    pointer = outgoing[0];
    if (outgoing.size() == 1) return 0;
    if (pointer == failWriteRegister) return 3;
    ++writes[address];
    // BME280 writes consist of register/value pairs, including burst writes.
    assert(outgoing.size() % 2 == 0);
    for (size_t i = 0; i < outgoing.size(); i += 2) {
        const uint8_t reg = outgoing[i];
        registers[address][reg] = outgoing[i + 1];
        if (reg == 0xE0) {
            registers[address][0xF2] = 0;
            registers[address][0xF4] = 0;
            registers[address][0xF5] = 0;
        }
    }
    return 0;
}
size_t TestWire::requestFrom(uint8_t addr, uint8_t length) {
    ++transactions;
    incoming.clear();
    readIndex = 0;
    if (!present[addr]) return 0;
    const uint8_t count = pointer == failReadRegister ? length - 1 : length;
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t reg = pointer + i;
        uint8_t value = registers[addr][reg];
        if (reg == 0xF3) {
            value = (stuckCalibration ? 1 : 0) | (stuckMeasurement ? 8 : 0);
            if (!stuckMeasurement) registers[addr][0xF4] &= ~3;
        }
        incoming.push_back(value);
    }
    return incoming.size();
}
int TestWire::read() {
    return readIndex < incoming.size() ? incoming[readIndex++] : -1;
}

static void attach(uint8_t addr = 0x76) {
    Wire.present[addr] = true;
    auto& r = Wire.registers[addr];
    r[0xD0] = 0x60;
    // Bosch datasheet temperature/pressure example: 25.08 C, 100653 Pa.
    const int calibration[] = {27504, 26435, -1000, 36477, -10685, 3024,
                               2855, 140, -7, 15500, -14600, 6000};
    for (size_t i = 0; i < 12; ++i) {
        const uint16_t value = static_cast<uint16_t>(calibration[i]);
        r[0x88 + 2 * i] = value & 0xFF;
        r[0x89 + 2 * i] = value >> 8;
    }
    r[0xA1] = 75;
    r[0xE1] = 362 & 0xFF; r[0xE2] = 362 >> 8;
    r[0xE3] = 0;
    r[0xE4] = 334 >> 4; r[0xE5] = (334 & 15) | ((50 & 15) << 4);
    r[0xE6] = 50 >> 4; r[0xE7] = 30;
    const uint32_t pressure = 415148, temperature = 519888;
    r[0xF7] = pressure >> 12; r[0xF8] = (pressure >> 4) & 0xFF; r[0xF9] = (pressure << 4) & 0xFF;
    r[0xFA] = temperature >> 12; r[0xFB] = (temperature >> 4) & 0xFF; r[0xFC] = (temperature << 4) & 0xFF;
    r[0xFD] = 30000 >> 8; r[0xFE] = 30000 & 0xFF;
}

static void reset() {
    Wire = {};
    testMillis = 0;
}

static void assertUnavailable() {
    const auto snap = EnvironmentSensor::snapshot();
    assert(!snap.available && snap.sensor == nullptr);
}

static void finishSample() {
    EnvironmentSensor::loop();  // Start conversion; no blocking conversion wait.
    testMillis += 20;
    EnvironmentSensor::loop();  // Read completed conversion.
}

static void testAbsentAndWrongChip() {
    reset();
    EnvironmentSensor::begin();
    assertUnavailable();
    const auto count = Wire.transactions;
    testMillis += 29999;
    EnvironmentSensor::loop();
    assert(Wire.transactions == count);
    ++testMillis;
    EnvironmentSensor::loop();
    assert(Wire.transactions > count);

    reset();
    attach();
    Wire.registers[0x76][0xD0] = 0x58;  // BMP280 must not be treated as BME280.
    EnvironmentSensor::begin();
    finishSample();
    assertUnavailable();
    assert(Wire.writes[0x76] == 0);
}

static void testAddressesAndCachedUnits() {
    for (const uint8_t addr : {0x76, 0x77}) {
        reset();
        attach(addr);
        EnvironmentSensor::begin();
        assertUnavailable();
        const uint32_t start = testMillis;
        EnvironmentSensor::loop();
        assert(testMillis == start);  // Starting a measurement never delays.
        assertUnavailable();
        const auto count = Wire.transactions;
        EnvironmentSensor::loop();
        assert(Wire.transactions == count);  // No early conversion polling.
        testMillis += 20;
        EnvironmentSensor::loop();
        const auto snap = EnvironmentSensor::snapshot();
        assert(snap.available && std::strcmp(snap.sensor, "bme280") == 0);
        assert(std::fabs(snap.temperatureC - 25.08f) < 0.01f);
        assert(std::fabs(snap.pressureHpa - 1006.53f) < 0.02f);
        assert(snap.humidityPct > 40.0f && snap.humidityPct < 60.0f);
        const auto sampledCount = Wire.transactions;
        for (int i = 0; i < 100; ++i) {
            assert(EnvironmentSensor::snapshot().available);
            EnvironmentSensor::loop();
        }
        assert(Wire.transactions == sampledCount);
        testMillis = start + 15000;
        assertUnavailable();
        assert(Wire.transactions == sampledCount);  // Expiry also reads no I2C.
    }
    reset();
    attach(0x76);
    attach(0x77);
    EnvironmentSensor::begin();
    finishSample();
    assert(EnvironmentSensor::snapshot().available);
    assert(Wire.writes[0x76] > 0 && Wire.writes[0x77] == 0);
}

static void testDisconnectAndRecovery() {
    reset();
    attach();
    EnvironmentSensor::begin();
    finishSample();
    Wire.present[0x76] = false;
    testMillis += 5000;
    EnvironmentSensor::loop();
    assertUnavailable();
    const auto count = Wire.transactions;
    attach(0x77);  // Retry probes both addresses, including after relocation.
    testMillis += 29999;
    EnvironmentSensor::loop();
    assert(Wire.transactions == count);
    ++testMillis;
    EnvironmentSensor::loop();
    finishSample();
    assert(EnvironmentSensor::snapshot().available);
}

static void testFailedAndIncompleteTransactions() {
    for (const int reg : {0x88, 0xE1, 0xF3, 0xF7}) {
        reset();
        attach();
        Wire.failReadRegister = reg;
        EnvironmentSensor::begin();
        finishSample();
        assertUnavailable();
    }
    reset();
    attach();
    Wire.failWriteRegister = 0xF2;
    EnvironmentSensor::begin();
    finishSample();
    assertUnavailable();

    reset();
    attach();
    EnvironmentSensor::begin();
    finishSample();
    Wire.failReadRegister = 0xF7;
    testMillis += 5000;
    finishSample();
    assertUnavailable();  // Do not continue serving the previously valid sample.

    reset();
    attach();
    Wire.shortWrite = true;
    EnvironmentSensor::begin();
    assertUnavailable();
    assert(Wire.transactions == 2);  // endTransmission still releases the bus.
}

static void testBoundedWaitsAndInvalidRawData() {
    reset();
    attach();
    Wire.stuckCalibration = true;
    EnvironmentSensor::begin();
    assertUnavailable();
    assert(testMillis < 100);

    reset();
    attach();
    EnvironmentSensor::begin();
    Wire.stuckMeasurement = true;
    finishSample();
    testMillis += 100;
    EnvironmentSensor::loop();
    assertUnavailable();
    const auto count = Wire.transactions;
    EnvironmentSensor::loop();
    assert(Wire.transactions == count);  // Busy forever enters retry backoff.

    for (const uint8_t reg : {0xF7, 0xFA, 0xFD}) {
        reset();
        attach();
        Wire.registers[0x76][reg] = 0x80;
        Wire.registers[0x76][reg + 1] = 0;
        if (reg != 0xFD) Wire.registers[0x76][reg + 2] = 0;
        EnvironmentSensor::begin();
        finishSample();
        assertUnavailable();
    }
}

static void testClockWrapAndDelayedRead() {
    reset();
    attach();
    testMillis = UINT32_MAX - 10;
    EnvironmentSensor::begin();
    finishSample();
    assert(EnvironmentSensor::snapshot().available);
    testMillis += 5000;
    EnvironmentSensor::loop();
    testMillis += 15000;  // An old conversion must not be published as fresh.
    EnvironmentSensor::loop();
    assertUnavailable();
    finishSample();
    assert(EnvironmentSensor::snapshot().available);
}

int main() {
    std::cout << std::unitbuf;
    std::cout << "absent / wrong chip\n";
    testAbsentAndWrongChip();
    std::cout << "addresses / cached units\n";
    testAddressesAndCachedUnits();
    std::cout << "disconnect / recovery\n";
    testDisconnectAndRecovery();
    std::cout << "transaction failures\n";
    testFailedAndIncompleteTransactions();
    std::cout << "bounded waits / raw sentinels\n";
    testBoundedWaitsAndInvalidRawData();
    std::cout << "clock wrap / delayed read\n";
    testClockWrapAndDelayedRead();
    std::cout << "environment sensor tests passed\n";
}
