#!/usr/bin/env python3
"""Execute production boot diagnostics with recorded MCU/core boundaries."""
from pathlib import Path
import subprocess
import tempfile
import unittest

FIRMWARE = Path(__file__).resolve().parents[1]

ARDUINO = r'''#pragma once
#include <cstdint>
uint32_t readResetReason();
float readCPUTemperature();
void analogReadResolution(int);
int analogRead(uint8_t);
float temperatureRead();
struct FakeEsp {
    uint32_t getFreeHeap() const { return 0; }
    uint32_t getMinFreeHeap() const { return 0; }
};
extern FakeEsp ESP;
'''
NRF = r'''#pragma once
#include <cstdint>
struct FakeWdt {
    uint32_t TASKS_START=0, RUNSTATUS=0, CRV=0xFFFFFFFFu, RREN=1, CONFIG=1;
    uint32_t RR[8]={};
};
struct FakeFicr { uint32_t DEVICEID[2]={}; };
struct FakePower { uint32_t RESETREAS=0; };
extern FakeWdt fakeWdt;
extern FakeFicr fakeFicr;
extern FakePower fakePower;
#define NRF_WDT (&fakeWdt)
#define NRF_FICR (&fakeFicr)
#define NRF_POWER (&fakePower)
#define POWER_RESETREAS_RESETPIN_Msk (1u << 0)
#define POWER_RESETREAS_DOG_Msk (1u << 1)
#define POWER_RESETREAS_SREQ_Msk (1u << 2)
#define POWER_RESETREAS_LOCKUP_Msk (1u << 3)
#define WDT_CONFIG_SLEEP_Msk (1u << 0)
#define WDT_CONFIG_HALT_Msk (1u << 3)
'''
PRELUDE = r'''
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include "compat.h"
#if !defined(ARDUINO_ARCH_ESP32)
FakeWdt fakeWdt;
FakeFicr fakeFicr;
FakePower fakePower;
#endif
uint32_t savedReset=0;
unsigned savedReads=0;
uint32_t readResetReason() { ++savedReads; return savedReset; }
float readCPUTemperature() { return 20; }
void analogReadResolution(int) {}
int analogRead(uint8_t) { return 0; }
float temperatureRead() { return 20; }
FakeEsp ESP;
int esp_reset_reason() { return static_cast<int>(savedReset); }
int esp_task_wdt_reset() { return 0; }
int esp_task_wdt_reconfigure(const esp_task_wdt_config_t*) { return 0; }
int esp_task_wdt_add(void*) { return 0; }
void esp_efuse_mac_get_default(uint8_t*) {}
struct RecordingSerial {
    std::string output;
    void printf(const char* format, ...) {
        char buffer[512];
        va_list args;
        va_start(args, format);
        const int count=vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        if (count<0 || static_cast<size_t>(count)>=sizeof(buffer)) std::abort();
        output += buffer;
    }
} Serial;
static constexpr uint32_t LOOP_WDT_TIMEOUT_S=30;
const std::string fwVersion="v1.4.1-test";
struct { const char* name; } BOARD={"test board"};
'''
# Available to either build so unused framework boundary definitions compile.
ESP_TASK = r'''#pragma once
#include <cstdint>
struct esp_task_wdt_config_t { uint32_t timeout_ms; uint32_t idle_core_mask; bool trigger_panic; };
int esp_task_wdt_reset();
int esp_task_wdt_reconfigure(const esp_task_wdt_config_t*);
int esp_task_wdt_add(void*);
'''


class BootDiagnosticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="openhop-nrf52-boot-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        main = (FIRMWARE / "src/main.cpp").read_text()
        # The exact production setup block is compiled, not copied/reimplemented.
        start = main.index("    // Print the reason this boot started")
        end = main.index("    // On boards with a PMU", start)
        reset = main[start:end]
        start = main.index("    // Arm the task watchdog LAST")
        end = main.index("\n}\n\n// ─── Noise floor sampling", start)
        watchdog = main[start:end]
        (work / "Arduino.h").write_text(ARDUINO)
        (work / "nrf.h").write_text(NRF)
        (work / "esp_task_wdt.h").write_text(ESP_TASK)
        (work / "esp_system.h").write_text("#pragma once\nint esp_reset_reason();\n")
        (work / "esp_mac.h").write_text("#pragma once\n#include <cstdint>\nvoid esp_efuse_mac_get_default(uint8_t*);\n")
        cpp = ('#include "esp_task_wdt.h"\n' + PRELUDE + "\nvoid reportReset() {\n" + reset +
               "\n}\nvoid armAndReportWatchdog() {\n" + watchdog + "\n}\n") + r'''
int main(int argc, char** argv) {
    if (argc!=2) return 2;
    const std::string scenario=argv[1];
    if (scenario.find("wdt_")==0) {
#if !defined(ARDUINO_ARCH_ESP32)
        if (scenario=="wdt_inherited") {
            fakeWdt.RUNSTATUS=1; fakeWdt.CRV=327679; fakeWdt.RREN=9; fakeWdt.CONFIG=8;
        } else if (scenario=="wdt_max") {
            fakeWdt.RUNSTATUS=1; fakeWdt.CRV=0xFFFFFFFFu; fakeWdt.RREN=1; fakeWdt.CONFIG=1;
        } else if (scenario=="wdt_default") {
            fakeWdt.RUNSTATUS=1; fakeWdt.CRV=983039; fakeWdt.RREN=1; fakeWdt.CONFIG=1;
        }
#endif
        armAndReportWatchdog();
        std::cout << Serial.output;
#if !defined(ARDUINO_ARCH_ESP32)
        std::cout << "registers=" << fakeWdt.RUNSTATUS << "," << fakeWdt.CRV << "," << fakeWdt.RREN << "," << fakeWdt.CONFIG << "\n";
#endif
        return 0;
    }
    savedReset=static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
    reportReset();
    std::cout << Serial.output << "saved_reads=" << savedReads << "\n";
}
'''
        source = work / "boot.cpp"
        source.write_text(cpp)
        cls.binaries = {}
        for platform, flag in (("nrf52", "-DNRF52_SERIES=1"), ("esp32", "-DARDUINO_ARCH_ESP32=1")):
            binary = work / platform
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", flag,
                            "-I" + str(work), "-I" + str(FIRMWARE / "include"),
                            str(source), "-o", str(binary)], check=True)
            cls.binaries[platform] = binary

    def output(self, platform, flags):
        return subprocess.check_output([str(self.binaries[platform]), str(flags)], text=True)

    def test_uses_core_captured_watchdog_reset_not_cleared_register(self):
        text = self.output("nrf52", 2)
        self.assertIn("reset_reason=0x00000002", text)
        self.assertIn("watchdog=1", text)
        self.assertIn("saved_reads=1", text)

    def test_software_and_combined_flags_are_not_esp32_enum_values(self):
        for flags, expected in ((4, "watchdog=0 software=1"), (6, "watchdog=1 software=1")):
            with self.subTest(flags=flags):
                text = self.output("nrf52", flags)
                self.assertIn(f"reset_reason=0x{flags:08X}", text)
                self.assertIn(expected, text)
                self.assertNotIn("TASK_WDT", text)
                self.assertNotIn("PANIC", text)

    def test_reports_pin_and_lockup_flags(self):
        text = self.output("nrf52", 9)
        self.assertIn("pin=1 watchdog=0 software=0 lockup=1", text)

    def test_zero_and_unknown_flags_preserve_raw_value(self):
        for flags in (0, 0x01000000):
            with self.subTest(flags=flags):
                text = self.output("nrf52", flags)
                self.assertIn(f"reset_reason=0x{flags:08X}", text)
                self.assertIn("pin=0 watchdog=0 software=0 lockup=0", text)

    def test_esp32_enum_log_remains_unchanged(self):
        self.assertEqual(self.output("esp32", 6), "[BOOT] reset_reason=6 (TASK_WDT)\nsaved_reads=0\n")

    def test_inherited_watchdog_reports_its_locked_configuration(self):
        text = self.output("nrf52", "wdt_inherited")
        self.assertIn("loop WDT running, timeout=10000ms, RR=0x09, sleep=pause, debug=run", text)
        self.assertIn("registers=1,327679,9,8", text)
        self.assertNotIn("30s", text)

    def test_inactive_watchdog_is_not_reported_as_armed(self):
        # This register model deliberately does not acknowledge TASKS_START.
        # The log must describe observed RUNSTATUS, not claim silicon started.
        text = self.output("nrf52", "wdt_inactive")
        self.assertIn("loop WDT inactive", text)
        self.assertIn("registers=0,983039,1,1", text)

    def test_maximum_inherited_crv_does_not_wrap_displayed_timeout(self):
        text = self.output("nrf52", "wdt_max")
        self.assertIn("timeout=131072000ms", text)
        self.assertIn("registers=1,4294967295,1,1", text)

    def test_default_watchdog_state_and_esp32_banner(self):
        self.assertIn("loop WDT running, timeout=30000ms, RR=0x01, sleep=run, debug=pause",
                      self.output("nrf52", "wdt_default"))
        self.assertEqual(self.output("esp32", "wdt_default"),
                         "[BOOT] firmware v1.4.1-test on test board ready (loop WDT 30s)\n")


if __name__ == "__main__":
    unittest.main(verbosity=2)
