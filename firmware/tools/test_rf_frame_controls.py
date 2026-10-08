#!/usr/bin/env python3
"""Host-side contract for the v0.8 RF frame commands."""

from __future__ import annotations

import pathlib
import re
import shutil
import subprocess
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
    assert "setAgcResetIntervalSec(sec, false)" in main
    assert "setFemState(req.apply, req.value, false, fem)" in main
    assert "RfRequests::Target::Fem,\n                        payload[0], payload[1], src)" in main
    assert "RfRequests::Target::RxBoost,\n                        0, payload[0], src)" in main
    assert "applyRxBoostedGainMode(rxBoostedGainEnabled)" in main
    agc_body = main.split("void maybeResetAgc() {", 1)[1].split("\n}\n", 1)[0]
    assert "hasAgcResetIntervalControl" not in agc_body
    assert "hasHeltecV43LnaControl" not in agc_body
    fem_case = main.split("case CMD_SET_FEM_STATE:", 1)[1].split("case CMD_GET_RX_BOOST:", 1)[0]
    boost_case = main.split("case CMD_SET_RX_BOOST:", 1)[1].split("case CMD_STATUS_REQ:", 1)[0]
    assert "ERR_RADIO_BUSY" not in fem_case
    assert "ERR_RADIO_BUSY" not in boost_case
    rf_drain = main.split("static void processRfRequests() {", 1)[1].split("static void submitRfRequest(", 1)[0]
    assert "isReceivingPacket()" in rf_drain
    loop = main.split("void loop() {", 1)[1]
    assert "processRfRequests();" in loop
    agc_reset = main.split("void maybeResetAgc() {", 1)[1].split("radio.standby();", 1)[0]
    assert "isReceivingPacket()" in agc_reset
    boost_answer = main.split("static void answerRxBoostRequest(", 1)[1].split("static void answerRfRequest(", 1)[0]
    assert "const bool applied = applyRxBoostedGainMode" in boost_answer
    assert "sendError(ERR_RADIO_INIT, req.src)" in boost_answer

    compiler = shutil.which("g++")
    if compiler is None:
        raise SystemExit("g++ is required for the FEM unit tests")
    with tempfile.TemporaryDirectory(prefix="openhop-rf-fem-") as temp_dir:
        for test in ("rf_fem_state_test", "rf_request_queue_test"):
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

    print("RF frame control contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
