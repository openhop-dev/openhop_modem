#!/usr/bin/env python3
"""Non-Duo descriptor counts through real complete modem setup/loop and transport."""
import os
from pathlib import Path
import subprocess
import tempfile
from nradio_test_support import compile_main


def main():
    with tempfile.TemporaryDirectory(prefix='nradio-main-', dir=os.environ.get('TMPDIR')) as directory:
        for count in (1, 3, 4, 5):
            out = Path(directory) / str(count)
            exe = compile_main(out, 'tests/radio_nradio_main_test.cpp', count,
                               selectable=count > 1)
            scenarios = ('invalid',) if count == 5 else ('all',) if count == 1 else ('all', 'sparse', 'highest', 'off', 'failed')
            for scenario in scenarios:
                subprocess.run([str(exe), scenario], check=True)


if __name__ == '__main__':
    main()
