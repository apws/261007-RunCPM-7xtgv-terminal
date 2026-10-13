#!/usr/bin/env bash
# Experimental browser runtime; never builds or writes the source card.
set -euo pipefail
xtgv_here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec python3 "$xtgv_here/shortcut.py" local "$@"
