#!/usr/bin/env python3
"""Duo power/readiness isolation through production main, TCP, parser and RF.

Only the unavailable Arduino/network/radio SDK and unrelated device ports are
recording fakes. No source-string assertions, hardware, RF or live services.
Each case runs in a fresh process so complete setup/loop is exercised.
"""
import os
import subprocess
import sys
import tempfile

from test_radio_main import compile_main_test

SCENARIOS = (
    "supported", "invalid_power", "setter_failure", "malformed",
    "stale_generation", "boot_recovery", "init_failure", "boot_band", "identity",
    "boot_recovery_rf2", "init_failure_rf2", "options_failure", "options_failure_rf2",
    "no_auto_bridge",
)


def main():
    selected = sys.argv[1:] or SCENARIOS
    if any(case not in SCENARIOS for case in selected):
        raise SystemExit("Unknown scenario; choose from: " + ", ".join(SCENARIOS))
    failed = []
    with tempfile.TemporaryDirectory(prefix="duo-power-config-", dir=os.environ.get("TMPDIR")) as directory:
        binary = compile_main_test(directory, "tests/duo_power_config_test.cpp")
        for case in selected:
            print("CASE: " + case, flush=True)
            result = subprocess.run([str(binary), case], check=False)
            print("RESULT: " + case + " exit=" + str(result.returncode), flush=True)
            if result.returncode:
                failed.append(case)
    if failed:
        raise SystemExit("FAIL: " + ", ".join(failed))
    print("PASS: Duo production-coupled power/config/readiness isolation (" + str(len(selected)) + " cases)")


if __name__ == "__main__":
    main()
