#!/usr/bin/env python3
"""Host-side contract for the v0.8 RF frame commands."""

from __future__ import annotations

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "firmware"

EXPECTED_COMMANDS = {
    "CMD_GET_RF_CAPS": 0x14,
    "CMD_RF_CAPS_RESP": 0x15,
    "CMD_SET_AGC_INTERVAL": 0x16,
    "CMD_GET_AGC_INTERVAL": 0x17,
    "CMD_AGC_INTERVAL_RESP": 0x18,
    "CMD_SET_FEM_STATE": 0x19,
    "CMD_GET_FEM_STATE": 0x1A,
    "CMD_FEM_STATE_RESP": 0x1B,
    "CMD_SET_RX_BOOST": 0x1C,
    "CMD_GET_RX_BOOST": 0x1D,
    "CMD_RX_BOOST_RESP": 0x1E,
    "ERR_UNSUPPORTED": 0x0F,
    "ERR_RADIO_FAULT": 0x10,
    "RF_CAP_AGC": 0x01,
    "RF_CAP_FEM_RX_LNA": 0x02,
    "RF_CAP_FEM_TX_PA": 0x04,
    "RF_CAP_RX_BOOSTED_GAIN": 0x08,
    "FEM_STATE_RX_LNA": 0x01,
    "FEM_STATE_TX_PA": 0x02,
}


def command_values(protocol: str) -> dict[str, int]:
    values: dict[str, int] = {}
    for name, raw in re.findall(r"^#define\s+(CMD_\w+|ERR_\w+|RF_CAP_\w+|FEM_STATE_\w+)\s+(0x[0-9A-Fa-f]+)", protocol, re.M):
        values[name] = int(raw, 16)
    return values


def main() -> int:
    protocol = (FIRMWARE / "include/protocol.h").read_text()
    values = command_values(protocol)
    for name, expected in EXPECTED_COMMANDS.items():
        assert values.get(name) == expected, f"{name} is {values.get(name)!r}, expected {expected:#x}"

    # CMD_PING and CMD_PONG share 0xFF by design: direction distinguishes them.
    shared_ok = {("CMD_PING", "CMD_PONG"), ("CMD_PONG", "CMD_PING")}
    opcodes = {name: value for name, value in values.items() if name.startswith("CMD_")}
    seen: dict[int, str] = {}
    for name, value in opcodes.items():
        previous = seen.get(value)
        if previous is not None and (previous, name) not in shared_ok:
            raise AssertionError(f"{name} collides with {previous} at {value:#x}")
        seen[value] = name

    errors = {name: value for name, value in values.items() if name.startswith("ERR_")}
    seen_errors: dict[int, str] = {}
    for name, value in errors.items():
        previous = seen_errors.get(value)
        assert previous is None, f"{name} collides with {previous} at {value:#x}"
        seen_errors[value] = name

    for snippet in (
        "SET_AGC_INTERVAL",
        "SET_FEM_STATE",
        "SET_RX_BOOST",
        "Above 3600 is ERR_INVALID_CONFIG",
        "Not persisted",
    ):
        assert snippet in protocol

    frontend = (FIRMWARE / "include/rf_frontend.h").read_text()
    source = (FIRMWARE / "src/rf_frontend.cpp").read_text()
    main = (FIRMWARE / "src/main.cpp").read_text()
    assert "bool setFemState(uint8_t apply, uint8_t value, bool persist, FemState& out);" in frontend
    assert "applyFemMask(" in source
    runtime = (FIRMWARE / "src/radio_runtime.cpp").read_text()
    assert "setAgcResetIntervalSec(sec, false)" in runtime
    assert "setFemState(req.apply, req.value, false, fem)" in runtime
    assert "setBoost(rxBoost_.enabled, true)" in runtime
    assert runtime.count("chip().setRxBoostedGainMode(") == 1
    assert "RxBoostControl::noteAgcReset(" in runtime
    assert "RxBoostControl::replyForGet(rxBoost_)" in runtime
    assert "if (!current(req.src)) return;" in runtime
    assert "hooks_.canReply(req.src)" in runtime
    assert "return !current(req.src);" in runtime
    assert "serviceRadioIrqs();" in main and "serviceRadioWork();" in main
    # Exercise the real complete class and complete main graph; source contracts
    # above supplement, rather than replace, behavioral acceptance.
    subprocess.run([sys.executable, str(FIRMWARE / 'tools/test_radio_runtime.py')], check=True)
    subprocess.run([sys.executable, str(FIRMWARE / 'tools/test_radio_main.py')], check=True)

    compiler = shutil.which("g++")
    if compiler is None:
        raise SystemExit("g++ is required for the FEM unit tests")
    with tempfile.TemporaryDirectory(prefix="openhop-rf-fem-") as temp_dir:
        for test in ("rf_fem_state_test", "rf_request_queue_test", "rx_boost_control_test"):
            executable = pathlib.Path(temp_dir) / test
            subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{FIRMWARE / 'include'}",
                    str(FIRMWARE / "tests" / f"{test}.cpp"),
                    "-o",
                    str(executable),
                ],
                check=True,
            )
            subprocess.run([str(executable)], check=True)

    subprocess.run([sys.executable, str(FIRMWARE / "tools/test_rf_request_connections.py")], check=True)
    print("RF frame control contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
