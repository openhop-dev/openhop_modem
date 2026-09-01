#!/usr/bin/env python3
"""Focused source contract for Station G3 hardware-only features."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "firmware"

platformio = (FIRMWARE / "platformio.ini").read_text()
station_section = platformio.split("[env:station_g3]", 1)[1].split("[env:", 1)[0]
assert "board = esp32-s3-devkitc1-n16r8" in station_section
assert "adafruit/Adafruit INA219@" in station_section

rf_header = (FIRMWARE / "include/rf_frontend.h").read_text()
rf_source = (FIRMWARE / "src/rf_frontend.cpp").read_text()
for declaration in (
    "bool hasStationG3LnaControl();",
    "bool isStationG3LnaEnabled();",
    "bool setStationG3LnaEnabled(bool enabled, bool persist);",
    "bool setStationG3RfConfig(bool paHighPower, bool lnaEnabled, bool persist);",
    "void prepareStandby();",
):
    assert declaration in rf_header
assert 'STATION_G3_RF_CONFIG_KEY = "g3_rf_cfg"' in rf_source
assert "p.getUChar(STATION_G3_RF_CONFIG_KEY" in rf_source
assert "p.putUChar(STATION_G3_RF_CONFIG_KEY, packed)" in rf_source
assert "setConfiguredLna(stationG3InReceive && stationG3LnaEnabled);" in rf_source

power_header = FIRMWARE / "include/station_g3_power.h"
power_source = FIRMWARE / "src/station_g3_power.cpp"
assert power_header.is_file()
assert power_source.is_file()
power = power_source.read_text()
assert "Adafruit_INA219" in power
assert "setCalibration_32V_2A" in power
assert "SAMPLE_INTERVAL_MS" in power
assert "MAX_CONSECUTIVE_FAILURES" in power
assert "REPROBE_INTERVAL_MS" in power
assert "consecutiveFailures" in power
assert "tryBegin" in power
assert "bool voltageReadOk = ina219.success();" in power
assert "bool currentReadOk = ina219.success();" in power
assert "minimumInputVoltageV" in power
assert "maximumCurrentMa" in power

main = (FIRMWARE / "src/main.cpp").read_text()
assert '#include "station_g3_power.h"' in main
assert "StationG3Power::begin();" in main
assert "StationG3Power::loop();" in main
assert "StationG3Power::snapshot()" in main
assert "RFFrontEnd::prepareStandby();" in main
loop_body = main.split("void loop() {", 1)[1]
assert loop_body.index("if (dio1Flag && !isTxActive)") < loop_body.index("StationG3Power::loop();")
assert loop_body.index("while (Serial.available())") < loop_body.index("StationG3Power::loop();")
assert loop_body.index("TCPServer::loop()") < loop_body.index("StationG3Power::loop();")

runtime = (FIRMWARE / "include/runtime_stats.h").read_text()
for field in (
    "stationG3PowerMonitorAvailable",
    "stationG3PowerValid",
    "stationG3InputVoltageV",
    "stationG3CurrentMa",
    "stationG3PowerW",
    "stationG3MinimumInputVoltageV",
    "stationG3MaximumCurrentMa",
):
    assert field in runtime

ota = (FIRMWARE / "src/ota_manager.cpp").read_text()
assert "Station G3 external RX LNA" in ota
assert '"station_g3_external_lna_enabled"' in ota
for field in (
    "station_g3_power_monitor_available",
    "station_g3_input_voltage_v",
    "station_g3_current_ma",
    "station_g3_power_w",
    "station_g3_minimum_input_voltage_v",
    "station_g3_maximum_current_ma",
):
    assert field in ota

for field in ("bus_voltage_v", "current_ma", "power_mw"):
    assert field in ota
assert "appendStationG3PowerTelemetry(body, snap);" in ota
assert ota.count("appendStationG3PowerTelemetry(body, snap);") == 1
assert '#include "station_g3_power.h"' not in ota
assert "StationG3Power::snapshot()" not in ota
assert "static_cast<uint32_t>(snap.stationG3PowerW" not in ota
assert "String(snap.stationG3PowerW * 1000.0f, 1)" in ota

# G3 controls and power telemetry must be capability-gated, not exposed by
# unrelated board variants or added to the binary host protocol.
assert "if (RFFrontEnd::hasStationG3LnaControl())" in ota
assert "snap.stationG3PowerMonitorAvailable" in ota
assert "RFFrontEnd::setStationG3RfConfig(" in ota
assert "RFFrontEnd::setPaHighPowerEnabled(" not in ota
assert "RFFrontEnd::setStationG3LnaEnabled(" not in ota
protocol = (FIRMWARE / "include/protocol.h").read_text()
assert "station_g3" not in protocol.lower()

print("Station G3 hardware feature contract: PASS")
