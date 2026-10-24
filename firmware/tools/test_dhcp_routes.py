#!/usr/bin/env python3
"""Execute production DHCP wrappers against recording SDK boundaries.

Requires g++ with ASan/UBSan. Each case gets a fresh process; no hardware,
network, or firmware flashing. The original SDK's packet-accept/reject decisions
are fixtures, not a second DHCP implementation or a full-stack network test.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

FIRMWARE = Path(__file__).resolve().parents[1]
SCENARIOS = (
    "no_default", "offer_not_published", "rejected_not_published",
    "renew_without_routes", "link_down", "admin_down", "address_loss", "netif_removed",
    "delayed_bind", "delayed_link_loss", "delayed_xid_change", "concatenated_options",
    "malformed_replacement", "owner_isolation", "removed_during_receive", "callback_lifecycle",
    "prl_order_and_bounds", "249_only", "121_wins_first", "121_wins_last",
    "malformed121_blocks249", "oversize_replacement", "rejected_replacement_then_acceptance",
    "begin_does_not_reset", "zero_router_ignored", "static_address_replaces_lease",
    "longest_prefix_and_default", "route_capacity", "route_payload_fuzz", "ipv6_not_intercepted",
)


def compile_routes(directory, source=None, fixture=None):
    directory = Path(directory)
    binary = directory / "routes"
    ota = (FIRMWARE / "src/ota_manager.cpp").read_text()
    display = ("static IPAddress displayGateway(" +
               ota.split("static IPAddress displayGateway(", 1)[1].split("\n}\n", 1)[0] + "\n}\n")
    (directory / "network_display.h").write_text(display)
    main = (FIRMWARE / "src/main.cpp").read_text()
    start = main.index("    const uint32_t invalidSTA = WifiManager::consumeSTAInvalidation();")
    consumer = main[start:main.index("\n#endif", start)]
    preamble = (FIRMWARE / "tools/wifi_recovery_stubs/recovery.cpp").read_text().split("int main(", 1)[0]
    (directory / "network_wifi.h").write_text(preamble + "\nvoid consumeFromMain(){\n" + consumer + "\n}\n")
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-DARDUINO_ARCH_ESP32=1", "-I" + str(directory),
        "-I" + str(FIRMWARE / "tools/dhcp_route_stubs"),
        "-I" + str(FIRMWARE / "tools/wifi_recovery_stubs"),
        "-I" + str(FIRMWARE / "include"),
        str(source or FIRMWARE / "src/dhcp_routes.cpp"),
        str(fixture or FIRMWARE / "tests/dhcp_routes_test.cpp"), "-o", str(binary),
    ], check=True)
    return binary


class InitializationTests(unittest.TestCase):
    def test_setup_omits_esp32_dhcp_initialization_on_nrf52(self):
        source = (FIRMWARE / "src/main.cpp").read_text()
        start = source.index("    // Initialize the DHCP option 121 module")
        end = source.index("    WifiManager::loadConfigOnly();", start)
        # Compile the actual setup block, not a copy or a string assertion.
        initialization = source[start:end]
        cpp = ('#include "dhcp_routes.h"\nvoid initializeNetwork() {\n' + initialization +
               '\n}\nint main(){initializeNetwork();}\n')
        with tempfile.TemporaryDirectory(prefix="modem86-init-") as directory:
            path = Path(directory) / "init.cpp"
            path.write_text(cpp)
            result = subprocess.run([
                "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I" + str(FIRMWARE / "include"), str(path), "-o", str(path.with_suffix("")),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(path.with_suffix(""))], check=True)


class RoutingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="modem86-routes-")
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.binary = compile_routes(cls.tmp.name)

    def scenario(self, name):
        result = subprocess.run([str(self.binary), name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class WifiGatewayTests(unittest.TestCase):
    def test_gateway_callbacks_do_not_invalidate_tcp_but_real_loss_does(self):
        with tempfile.TemporaryDirectory(prefix="modem86-wifi-") as directory:
            binary = compile_routes(directory, fixture=FIRMWARE / "tests/dhcp_routes_wifi_test.cpp")
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


for name in SCENARIOS:
    setattr(RoutingTests, "test_" + name, lambda self, name=name: self.scenario(name))


if __name__ == "__main__":
    unittest.main(verbosity=2)
