# pyright: reportMissingImports=false, reportUndefinedVariable=false
"""Record P4's resolved SDK and OTA initializer for the asset contract."""
from pathlib import Path
import sys

Import("env")  # noqa: F821 - PlatformIO/SCons
sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "tools"))
from p4_firmware import P4_PROFILES, capture_build_info  # noqa: E402

profile = P4_PROFILES[env["PIOENV"]]
board = env.BoardConfig()
if board.get("build.chip_variant") != profile["chip_variant"]:
    raise ValueError("P4 profile selected the wrong precompiled chip variant")

platform = env.PioPlatform()
framework = Path(platform.get_package_dir("framework-arduinoespressif32"))
libraries = Path(platform.get_package_dir("framework-arduinoespressif32-libs"))
flash_size = str(board.get("upload.flash_size"))
if not flash_size.endswith("MB"):
    raise ValueError(f"Unsupported P4 flash size: {flash_size}")
definitions = [item[0] if isinstance(item, (list, tuple)) else item
               for item in env.get("CPPDEFINES", [])]
settings = {
    "board": env["BOARD"],
    "cpu_hz": int(str(board.get("build.f_cpu")).rstrip("Ll")),
    "flash_hz": int(str(board.get("build.f_flash")).rstrip("Ll")),
    "flash_mode": board.get("build.flash_mode"),
    "flash_size_bytes": int(flash_size[:-2]) * 1024 * 1024,
    "psram_hz": int(str(board.get("build.f_psram")).rstrip("Ll")),
    "psram_enabled": "BOARD_HAS_PSRAM" in definitions,
}
capture_build_info(env["PIOENV"], env.GetProjectOption("platform"), settings,
                   framework, libraries, Path(env.subst("$BUILD_DIR")))
