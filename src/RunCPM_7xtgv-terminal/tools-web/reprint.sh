#!/usr/bin/env bash
# Refresh saved mobile guide PDFs separately from application builds.
set -euo pipefail
xtgv_here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec python3 "$xtgv_here/shortcut.py" reprint "$@"
