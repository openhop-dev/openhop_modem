#!/usr/bin/env python3
"""Production-coupled cooperative TCP output regressions (no PIO/hardware).

Real AF_UNIX socketpairs exercise the fd/send boundary; finite-capacity socket
ports exercise deterministic partial/zero/error writes. --sdk-source optionally
links the exact resolved Arduino NetworkClient::write body, verbatim, as an
independent reference control and as the legacy writer (which production must
never call). No dependency downloads or network services are used.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import tempfile

FW = Path(__file__).resolve().parents[1]


def extract_function(text, signature):
    if text.count(signature) != 1:
        raise ValueError('expected one SDK method signature: ' + signature)
    start = text.index(signature)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def sdk_port(source, directory):
    text = source.read_text()
    body = extract_function(text, 'size_t NetworkClient::write(const uint8_t *buf, size_t size)')
    defines = []
    for name in ['WIFI_CLIENT_MAX_WRITE_RETRY', 'WIFI_CLIENT_SELECT_TIMEOUT_US']:
        matches = re.findall(r'^#define\s+' + name + r'\s+(.+)$', text, re.M)
        if len(matches) != 1:
            raise ValueError('expected one SDK constant: ' + name)
        defines.append('#define ' + name + ' ' + matches[0])
    port = directory / 'sdk_write_reference.cpp'
    port.write_text(r'''#include <Arduino.h>
#include <WiFi.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <cerrno>
#include <chrono>
size_t sdkSelectCalls = 0;
static int countedSelect(int n, fd_set* r, fd_set* w, fd_set* e, timeval* t) {
    ++sdkSelectCalls; return ::select(n, r, w, e, t);
}
class NetworkClient {
    int descriptor;
public:
    bool _connected = true;
    int _timeout = 3000, _lastWriteTimeout = 0;
    explicit NetworkClient(int d) : descriptor(d) {}
    int fd() const { return descriptor; }
    int setSocketOption(int o, char* v, size_t n) { return ::setsockopt(fd(), SOL_SOCKET, o, v, n); }
    void stop() { _connected = false; }
    size_t write(const uint8_t*, size_t);
};
#define log_e(...) ((void)0)
#define select countedSelect
''' + '\n'.join(defines) + '\n' + body + r'''
#undef select
static size_t exactSdkWrite(int fd, const uint8_t* bytes, size_t size) {
    const auto start = std::chrono::steady_clock::now();
    NetworkClient client(fd);
    const size_t count = client.write(bytes, size);
    clockMs += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    return count;
}
struct InstallSdkWritePort { InstallSdkWritePort() { radioLegacySocketWrite = exactSdkWrite; } } installSdkWritePort;
''')
    identities = {str(source): hashlib.sha256(source.read_bytes()).hexdigest(),
                  'verbatim_write_method_sha256': hashlib.sha256(body.encode()).hexdigest(),
                  'constants': defines}
    header = source.with_suffix('.h')
    if header.exists():
        identities[str(header)] = hashlib.sha256(header.read_bytes()).hexdigest()
    (directory / 'sdk-identity.json').write_text(json.dumps(identities, indent=2) + '\n')
    print('SDK IDENTITY:', json.dumps(identities, sort_keys=True), flush=True)
    return port


def run(directory, sdk_source=None, scenarios=None):
    directory.mkdir(parents=True, exist_ok=True)
    sources = ['src/main.cpp', 'src/radio_settings.cpp', 'src/radio_runtime.cpp', 'src/radio_hardware.cpp',
               'src/rf_frontend.cpp', 'src/tcp_server.cpp', 'src/frame_parser.cpp',
               'tools/radio_runtime_stubs/main_device_ports.cpp',
               'tests/tcp_radio_output_main_test.cpp']
    additional = [sdk_port(sdk_source, directory)] if sdk_source else []
    binary = directory / 'tcp_output_main'
    command = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
               '-Wno-missing-field-initializers', '-DARDUINO_ARCH_ESP32',
               '-DBOARD_ETHERMESH_DUO', '-I' + str(FW / 'tools/radio_runtime_stubs'),
               '-I' + str(FW / 'include'), *[str(FW / p) for p in sources],
               *map(str, additional), '-o', str(binary)]
    identity_paths = [FW / p for p in sources]
    for tree in ['include', 'tools/radio_runtime_stubs', 'tools/tcp_radio_stubs']:
        identity_paths.extend(sorted((FW / tree).rglob('*.h')))
    identities = {str(path.relative_to(FW)): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in identity_paths}
    (directory / 'source-identity.json').write_text(json.dumps(identities, indent=2) + '\n')
    (directory / 'compile-command.json').write_text(json.dumps(command, indent=2) + '\n')
    print('COMPILE:', ' '.join(command), flush=True)
    compiled = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
    (directory / 'compile.log').write_text(compiled.stdout)
    if compiled.returncode:
        print('SETUP COMPILE ERROR (not behavioral RED):\n' + compiled.stdout, flush=True)
        return compiled.returncode
    changed = [p for p, digest in identities.items()
               if hashlib.sha256((FW / p).read_bytes()).hexdigest() != digest]
    if changed:
        raise RuntimeError('Source changed during compile; do not count results: ' + ', '.join(changed))
    results = []
    selected = scenarios or ['saturated_event', 'partial_writes', 'saturated_reply',
                                  'saturated_auth_ok', 'saturated_crc_error',
                                  'wrong_token_congestion', 'preauth_command_congestion',
                                  'wrong_token_partial', 'deadline', 'deadline_wrap',
                                  'deadline_aged_tail', 'queue_drain', 'queue_overflow_reply',
                                  'queue_overflow_event', 'queue_overflow_raw', 'queue_disconnect',
                                  'pending_completion', 'pending_completion_reconnect',
                                  'preauth_error_pressure', 'wrong_token_deadline',
                                  'oversized_write', 'null_write', 'fatal_write',
                                  'one_send_per_service', 'queued_restart', 'queued_invalidation']
    if sdk_source and not scenarios:
        selected = [*selected, 'sdk_reference_control']
    for scenario in selected:
        print('RUN:', scenario, flush=True)
        try:
            result = subprocess.run([str(binary), scenario], text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=25)
            output, exit_code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            output = (error.stdout or b'').decode() if isinstance(error.stdout, bytes) else (error.stdout or '')
            output += '\nTIMEOUT (not a passing acceptance or assertion oracle)\n'
            exit_code = 124
        (directory / (scenario + '.log')).write_text(output)
        print(output, end='', flush=True)
        results.append({'scenario': scenario, 'exit_code': exit_code,
                        'kind': 'expected_bad_sdk_reference_control' if scenario == 'sdk_reference_control' else 'acceptance'})
        (directory / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    return int(any(result['exit_code'] for result in results))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--sdk-source', type=Path, default=os.environ.get('SDK_NETWORK_CLIENT_SOURCE'))
    parser.add_argument('--scenario', action='append')
    args = parser.parse_args()
    if args.output_dir:
        return run(args.output_dir, args.sdk_source, args.scenario)
    with tempfile.TemporaryDirectory(prefix='tcp-radio-output-', dir=os.environ.get('TMPDIR')) as temp:
        return run(Path(temp), args.sdk_source, args.scenario)


if __name__ == '__main__':
    raise SystemExit(main())
