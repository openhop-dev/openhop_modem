#!/bin/bash
# Build every supported environment and stage the same complete asset set used
# by GitHub Actions. Run from anywhere; paths resolve relative to this script.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
args=(--variant all)
if [ -n "${PIO:-}" ]; then
    args+=(--pio "$PIO")
fi

exec python3 "$SCRIPT_DIR/tools/build_firmware_assets.py" "${args[@]}"
