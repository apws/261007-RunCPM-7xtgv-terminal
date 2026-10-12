#!/usr/bin/env bash
# Experimental browser runtime; never builds or writes the source card.
set -euo pipefail
xtgv_here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
xtgv_root=$(cd -- "$xtgv_here/../../.." && pwd)
if [[ "${1:-}" != "--native-mirror" ]]; then
    python3 "$xtgv_here/browser.py" "$@" 2>&1 | tee "$xtgv_here/desktop-local.log"
    exit "${PIPESTATUS[0]}"
fi
shift
xtgv_python="$xtgv_root/build/RunCPM_7xtgv-terminal/desktop-web/_deps/venv/bin/python"
[[ -x "$xtgv_python" ]] || { echo 'Run tools-web/setup.sh first.' >&2; exit 1; }
"$xtgv_python" "$xtgv_here/launch.py" "$@" 2>&1 | tee "$xtgv_here/desktop-local.log"
