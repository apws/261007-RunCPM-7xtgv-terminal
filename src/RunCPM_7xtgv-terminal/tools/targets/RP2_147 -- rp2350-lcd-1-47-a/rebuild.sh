#!/usr/bin/env bash
# Build/stage new firmware only; no device or mirror access.
set -euo pipefail
xtgv_tools=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
command -v python3 >/dev/null || {
    printf '%s\n' 'Missing python3. Run tools/setup.sh on Arch/CachyOS.' >&2
    exit 1
}
exec python3 "$xtgv_tools/target_reflash.py" "$@" --build-only --target rp2350-lcd-1-47-a
