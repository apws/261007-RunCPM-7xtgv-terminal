#!/usr/bin/env bash
# Desktop only: never build or upload MCU firmware.
set -euo pipefail
xtgv_here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
command -v python3 >/dev/null || { printf '%s\n' 'Missing python3. Run tools/setup.sh.' >&2; exit 1; }
exec python3 "$xtgv_here/../../target_desktop.py" build --log "$xtgv_here/desktop-rebuild.log" "$@" --target rp2350-touch-lcd-2
