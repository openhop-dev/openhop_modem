# pyright: reportMissingImports=false, reportUndefinedVariable=false
"""RAK4631-only PlatformIO post-build hook for deterministic firmware.ota."""

from __future__ import annotations

import sys
from pathlib import Path

Import("env")  # noqa: F821 - provided by PlatformIO/SCons

tools_dir = Path(env.subst("$PROJECT_DIR")) / "tools"  # noqa: F821
sys.path.insert(0, str(tools_dir))

from package_nrf52_ota import (  # noqa: E402
    create_package_bytes,
    firmware_version_from_source,
    read_dfu_payload,
)


def generate_ota(source, target, env) -> None:
    del source, target
    build_dir = Path(env.subst("$BUILD_DIR"))
    program_name = env.subst("$PROGNAME")
    zip_file = build_dir / f"{program_name}.zip"
    ota_file = build_dir / f"{program_name}.ota"
    payload = read_dfu_payload(zip_file)
    version = firmware_version_from_source(
        Path(env.subst("$PROJECT_DIR")) / "src" / "main.cpp",
        Path(env.subst("$PROJECT_DIR")) / "include/boards/rak4631_wismesh_eth.h",
    )
    metadata = create_package_bytes(payload, ota_file, version)
    print(
        f"Generated {ota_file} ({metadata['package_length']} bytes, "
        f"payload SHA-256 {metadata['payload_sha256']})"
    )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.zip", generate_ota)  # noqa: F821
