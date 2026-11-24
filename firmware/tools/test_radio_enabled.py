#!/usr/bin/env python3
"""Complete production startup/loop with only SDK/device boundaries replaced."""
import os
import subprocess
import tempfile
from test_radio_main import compile_main_test

def main():
    with tempfile.TemporaryDirectory(prefix='rf-enabled-', dir=os.environ.get('TMPDIR')) as d:
        exe = compile_main_test(d, 'tests/radio_enabled_main_test.cpp')
        for mode in ('default','off','rf2','both','fail'):
            subprocess.run([str(exe), mode], check=True)
        settings = compile_main_test(d, 'tests/radio_settings_test.cpp')
        subprocess.run([str(settings)], check=True)

if __name__ == '__main__':
    main()
