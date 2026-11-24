#!/usr/bin/env python3
"""Run production TCP listener/parser tests with recording Arduino sockets.

Only g++ and local source are used: no hardware, network, PlatformIO or RF.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUBS = ROOT / 'tools/tcp_radio_stubs'

class RadioSessionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='openhop-radio-tcp-')
        cls.binary = Path(cls.temp.name) / 'radio_sessions'
        flags = os.environ.get('TCP_RADIO_CXXFLAGS', '').split()
        command = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                   '-DARDUINO_ARCH_ESP32=1', *flags,
                   '-I' + str(STUBS), '-I' + str(ROOT / 'include'),
                   str(ROOT / 'tests/tcp_radio_sessions_test.cpp'), '-o', str(cls.binary)]
        print('COMPILE:', ' '.join(command), flush=True)
        subprocess.run(command, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def scenario(self, name):
        result = subprocess.run([str(self.binary), name], text=True, capture_output=True)
        print(result.stdout, end='', flush=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_nrf52_unused_transport_compiles_without_wifi(self):
        output = Path(self.temp.name) / 'nrf52_unused.o'
        command = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                   '-DARDUINO_ARCH_NRF52=1', '-I' + str(STUBS / 'no_wifi'),
                   '-I' + str(STUBS), '-I' + str(ROOT / 'include'),
                   '-c', str(ROOT / 'src/tcp_server.cpp'), '-o', str(output)]
        result = subprocess.run(command, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        symbols = subprocess.check_output(['nm', '-C', str(output)], text=True)
        self.assertNotRegex(symbols, r'\b[Tt]\s+TCPServer::')

    def test_isolated_wrong_token(self):
        self.scenario('isolated_wrong_token')

    def test_isolated_preauth_command(self):
        self.scenario('isolated_preauth_command')

    def test_partial_interleaving(self):
        self.scenario('partial_interleaving')

    def test_lan_admission(self):
        self.scenario('lan_admission')

    def test_one_client_per_port(self):
        self.scenario('one_client_per_port')

    def test_interface_invalidation(self):
        self.scenario('interface_invalidation')

    def test_legacy_primary(self):
        self.scenario('legacy_primary')

    def test_stale_routes(self):
        self.scenario('stale_routes')

    def test_exhausted_generation(self):
        self.scenario('exhausted_generation')

    def test_custom_ports(self):
        self.scenario('custom_ports')

    def test_corrupt_frame_budget(self):
        self.scenario('corrupt_frame_budget')

    def test_crc_owner(self):
        self.scenario('crc_owner')

    def test_fair_two_forward(self):
        self.scenario('fair_two_forward')

    def test_fair_two_reverse(self):
        self.scenario('fair_two_reverse')

    def test_fair_four_forward(self):
        self.scenario('fair_four_forward')

    def test_fair_four_reverse(self):
        self.scenario('fair_four_reverse')

    def test_byte_budget_interleaving(self):
        self.scenario('byte_budget_interleaving')

    def test_four_radio_events(self):
        self.scenario('four_radio_events')

    def test_invalid_definitions(self):
        self.scenario('invalid_definitions')

    def test_two_radio_commands(self):
        self.scenario('two_radio_commands')

if __name__ == '__main__':
    unittest.main(verbosity=2)
