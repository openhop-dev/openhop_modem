#!/usr/bin/env python3
"""Real settings/store with actual projected BoardConfig and non-Duo N=1..4."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
from nradio_test_support import FW, prepare_headers


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--count', type=int, choices=(1, 2, 3, 4))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='nradio-settings-', dir=os.environ.get('TMPDIR')) as directory:
        for count in ([args.count] if args.count else (1, 2, 3, 4)):
            out = Path(directory) / str(count)
            out.mkdir()
            mask = (1 << (count - 1)) | 1
            include, flags = prepare_headers(out, count, mask, selectable=count > 1)
            exe = out / 'settings'
            command = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                       '-Wno-missing-field-initializers', *flags,
                       f'-I{FW / "tools/radio_runtime_stubs"}', f'-I{include}',
                       str(FW / 'src/radio_settings.cpp'),
                       str(FW / 'tests/nradio_settings_test.cpp'), '-o', str(exe)]
            subprocess.run(command, check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
