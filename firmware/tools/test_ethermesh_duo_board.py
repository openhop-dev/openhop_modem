#!/usr/bin/env python3
"""Compile actual Duo board policy and legacy trailing-field compatibility."""
from pathlib import Path
import os
import subprocess
import tempfile

FW=Path(__file__).resolve().parents[1]

def main():
    with tempfile.TemporaryDirectory(prefix="openhop-duo-board-",dir=os.environ.get("TMPDIR")) as temp:
        out=Path(temp)
        for macro,source in [("BOARD_ETHERMESH_DUO",FW/"tests/ethermesh_duo_board_test.cpp"),
                             ("BOARD_HELTEC_V3",FW/"tests/radio_layout_test.cpp"),
                             ("BOARD_RAK3401",FW/"tests/radio_layout_test.cpp")]:
            exe=out/macro
            subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-Wno-missing-field-initializers",
                            f"-D{macro}",f"-I{FW/'include'}",str(source),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__=="__main__": main()
