#!/usr/bin/env python3
"""Prove production-coupled ownership oracles reject transport regressions.

Mutate only scratch copies of the actual TCP source. Compile failures and
crashes do NOT count as caught regressions: each mutant must reach a failing
REQUIRE in the same production-bank fixture that passed on pristine source.
No source in the checkout is rewritten; no hardware/network/PlatformIO access.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUBS = ROOT / 'tools/tcp_radio_stubs'
MUTATIONS = [
    ('event_without_auth',
     'if (isClientReady(radio)) queueOutput(sessions[radio], data, len);',
     'if (radio < listenerCount) queueOutput(sessions[radio], data, len);',
     'four_radio_events'),
    ('reply_falls_back_to_primary',
     'if (isRouteCurrent(route)) queueOutput(sessions[route.radio], data, len);',
     'if (isRouteCurrent(route)) queueOutput(sessions[0], data, len);',
     'two_radio_commands'),
    ('stale_generation_accepted',
     'sessions[route.radio].generation == route.generation;',
     'true;', 'stale_routes'),
    ('generation_reset_on_teardown',
     'session.client = WiFiClient();\n    session.authenticated = false;',
     'session.client = WiFiClient();\n    session.authenticated = false; session.generation = 0;',
     'stale_routes'),
    ('shared_parser',
     'frameparser_feed(session.parser, byte, TransportSource::TCP,',
     'frameparser_feed(sessions[0].parser, byte, TransportSource::TCP,',
     'partial_interleaving'),
    ('unauthorized_command_keeps_socket',
     'session.closing = true;\n            sendErrorToClient(session, ERR_UNAUTHORIZED);',
     'session.closing = false;\n            sendErrorToClient(session, ERR_UNAUTHORIZED);',
     'isolated_preauth_command'),
    ('wrong_token_accepted',
     'memcmp(payload, requiredToken.c_str(), len) == 0',
     'true', 'isolated_wrong_token'),
    ('lan_admission_removed',
     '!isLanAddress(incoming.remoteIP())',
     'false', 'lan_admission'),
    ('invalidation_only_primary',
     'void invalidateInterface(const IPAddress& address) {\n    for (size_t i = 0; i < listenerCount; ++i)',
     'void invalidateInterface(const IPAddress& address) {\n    for (size_t i = 0; i < (listenerCount ? 1U : 0U); ++i)',
     'interface_invalidation'),
    ('crc_reply_falls_back_to_primary',
     'else noteTransportFrameError(error);\n    sendErrorToClient(session, error);',
     'else noteTransportFrameError(error);\n    sendErrorToClient(sessions[0], error);',
     'crc_owner'),
    ('two_frames_per_pass',
     'MAX_FRAMES_PER_LOOP = 1;',
     'MAX_FRAMES_PER_LOOP = 2;', 'fair_four_forward'),
    ('byte_budget_doubled',
     'MAX_BYTES_PER_LOOP = 256;',
     'MAX_BYTES_PER_LOOP = 512;', 'byte_budget_interleaving'),
    ('crc_does_not_yield',
     '++session.parsedFrameCount; // malformed input also consumes the frame quota',
     '// no completed-frame accounting for parse errors', 'corrupt_frame_budget'),
    ('active_client_replaced',
     'if (!session.client || !session.client.connected()) {',
     'if (true) {', 'one_client_per_port'),
]


def main():
    production = (ROOT / 'src/tcp_server.cpp').read_text()
    fixture = (ROOT / 'tests/tcp_radio_sessions_test.cpp').read_text()
    with tempfile.TemporaryDirectory(prefix='openhop-radio-mutations-') as temp:
        work = Path(temp)

        def compile_source(name, source):
            transport = work / (name + '-transport.cpp')
            harness = work / (name + '.cpp')
            binary = work / name
            transport.write_text(source)
            harness.write_text(fixture.replace('#include "../src/tcp_server.cpp"',
                                               '#include "' + str(transport) + '"')
                               .replace('#include "../src/frame_parser.cpp"',
                                        '#include "' + str(ROOT / 'src/frame_parser.cpp') + '"'))
            command = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                       '-DARDUINO_ARCH_ESP32=1', '-I' + str(STUBS),
                       '-I' + str(ROOT / 'include'), str(harness), '-o', str(binary)]
            print('COMPILE:', ' '.join(command), flush=True)
            subprocess.run(command, check=True)
            return binary

        baseline = compile_source('baseline', production)
        for scenario in sorted({mutation[3] for mutation in MUTATIONS}):
            subprocess.run([str(baseline), scenario], check=True)
        caught = 0
        for name, needle, replacement, scenario in MUTATIONS:
            if production.count(needle) != 1:
                raise RuntimeError(f'{name}: expected exactly one mutation seam')
            binary = compile_source(name, production.replace(needle, replacement))
            result = subprocess.run([str(binary), scenario], text=True, capture_output=True)
            print(f'{name}: exit={result.returncode}\n{result.stdout}{result.stderr}', end='', flush=True)
            if result.returncode != 1 or not result.stderr.strip():
                raise RuntimeError(f'{name}: mutant survived or failed outside the assertion oracle')
            caught += 1
        print(f'Transport mutation oracles: {caught}/{len(MUTATIONS)} caught', flush=True)


if __name__ == '__main__':
    main()
