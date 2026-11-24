#!/usr/bin/env python3
"""Full-main CS admission before first SPI call, every N3/N4 enable mask."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
from nradio_test_support import compile_main


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--red-probe', action='store_true')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='nradio-cs-',dir=os.environ.get('TMPDIR')) as d:
        for count in ((3,) if args.red_probe else (1,3,4)):
            exe=compile_main(Path(d)/str(count),'tests/nradio_startup_order_test.cpp',count,selectable=count>1)
            masks=(7,) if args.red_probe else (1,) if count==1 else range(1<<count)
            for mask in masks:
                subprocess.run([str(exe),str(mask)],check=True)


if __name__=='__main__':main()
