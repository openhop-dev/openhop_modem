"""Synthetic board DATA over copied production headers; production .cpp stays real.

Only the selected Heltec header's BOARD declaration is replaced. It is not a
production target or a physical Heltec wiring claim. Every other header's bytes
are copied unchanged, so quoted includes resolve to the same projected tree.
"""
from pathlib import Path
import shutil
import subprocess

FW = Path(__file__).resolve().parents[1]


def prepare_headers(directory, count, default_mask=None, selectable=True):
    directory = Path(directory)
    include = directory / 'include'
    shutil.copytree(FW / 'include', include)
    board = include / 'boards/heltec_v3.h'
    original = board.read_text()
    anchor = 'inline const BoardConfig BOARD ='
    assert original.count(anchor) == 1
    fixture = (FW / 'tests/nradio_board_fixture.h').read_text()
    board.write_text(original.replace(anchor, 'inline const BoardConfig NRADIO_BASE =') + '\n' + fixture)
    mask = (1 << count) - 1 if default_mask is None else default_mask
    flags = ['-DBOARD_HELTEC_V3', '-DARDUINO_ARCH_ESP32',
             f'-DNRADIO_COUNT={count}', f'-DNRADIO_DEFAULT_MASK={mask}',
             f'-DNRADIO_SELECTABLE={int(selectable)}']
    return include, flags


def compile_main(directory, source, count, default_mask=None, selectable=True,
                 ports=None, extra_sources=(), extra_include=()):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    include, flags = prepare_headers(directory, count, default_mask, selectable)
    sources = ['src/main.cpp', 'src/radio_settings.cpp', 'src/radio_runtime.cpp',
               'src/radio_hardware.cpp', 'src/rf_frontend.cpp', 'src/tcp_server.cpp',
               'src/frame_parser.cpp', *extra_sources]
    if ports is None:
        ports = FW / 'tools/radio_runtime_stubs/main_device_ports.cpp'
    exe = directory / Path(source).stem
    command = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
               '-Wno-missing-field-initializers', *flags,
               *[f'-I{p}' for p in extra_include],
               f'-I{FW / "tools/radio_runtime_stubs"}', f'-I{include}',
               *[str(FW / p) for p in sources], str(ports), str(FW / source), '-o', str(exe)]
    subprocess.run(command, check=True)
    return exe
