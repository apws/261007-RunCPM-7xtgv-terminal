#!/usr/bin/env bash
# Experimental web proof only; no MCU build or card changes.
set -euo pipefail
xtgv_here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec python3 "$xtgv_here/shortcut.py" rebuild "$@"
