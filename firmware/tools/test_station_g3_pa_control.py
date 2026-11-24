#!/usr/bin/env python3
"""Focused source contract for Station G3-only PA mode control."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "firmware"

board_config = (FIRMWARE / "include/board_config.h").read_text()
station_g3 = (FIRMWARE / "include/boards/station_g3.h").read_text()
rf_header = (FIRMWARE / "include/rf_frontend.h").read_text()
rf_source = (FIRMWARE / "src/rf_frontend.cpp").read_text()
ota = (FIRMWARE / "src/ota_manager.cpp").read_text()

assert "bool   pa_user_selectable = false;" in board_config
assert ".pa_user_selectable = true" in station_g3

for board in (FIRMWARE / "include/boards").glob("*.h"):
    if board.name == "station_g3.h":
        continue
    assert ".pa_user_selectable = true" not in board.read_text(), board

for declaration in (
    "bool hasPaModeControl();",
    "bool isPaHighPowerEnabled();",
    "bool setPaHighPowerEnabled(bool enabled, bool persist);",
):
    assert declaration in rf_header

assert 'STATION_G3_RF_CONFIG_KEY = "g3_rf_cfg"' in rf_source
assert "hardware_.rf_frontend.pa_user_selectable" in rf_source
assert "writeConfiguredLevel(hardware_.rf_frontend.pa_mode_pin" in rf_source
assert "p.getUChar(STATION_G3_RF_CONFIG_KEY" in rf_source
assert "p.putUChar(STATION_G3_RF_CONFIG_KEY, packed)" in rf_source

begin_body = rf_source.split("void Controller::begin() {", 1)[1].split("bool Controller::hasPaModeControl()", 1)[0]
assert begin_body.index("writeConfiguredLevel(hardware_.rf_frontend.pa_mode_pin") < begin_body.index("p.getUChar(STATION_G3_RF_CONFIG_KEY")

set_rf_body = rf_source.split("bool Controller::setStationG3RfConfig", 1)[1].split("bool Controller::hasHeltecV43LnaControl", 1)[0]
assert set_rf_body.index("p.putUChar(STATION_G3_RF_CONFIG_KEY, packed)") < set_rf_body.index("paHighPowerEnabled = paHighPower")

assert 'if (RFFrontEnd::hasPaModeControl()) {' in ota
assert "Station G3 RF Front-End" in ota
assert 'action=\'/rf-pa\'' in ota
route_guard = 'if (RFFrontEnd::hasPaModeControl() && RFFrontEnd::hasStationG3LnaControl()) {\n        httpServer->on("/rf-pa", HTTP_POST, handleRfPaSave);'
assert route_guard in ota
assert 'obj["pa_high_power_enabled"]' in ota
assert '"pa_high_power_enabled"' in ota
assert "Station G3 RF front-end control is not supported on this board" in ota

parse_body = ota.split("static bool applyConfigPatch", 1)[1].split("// ─── Rollback sanity check", 1)[0]
assert "RFFrontEnd::setPaHighPowerEnabled" not in parse_body
assert "rfPatch.paHighPowerEnabled = paVal.as<bool>();" in parse_body

post_body = ota.split("static void handleApiConfigPost()", 1)[1].split("static void handleApiReboot()", 1)[0]
assert post_body.index("applyConfigPatch") < post_body.index("RFFrontEnd::setStationG3RfConfig")

print("Station G3 PA control contract: PASS")
