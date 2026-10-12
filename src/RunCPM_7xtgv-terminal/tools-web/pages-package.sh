#!/usr/bin/env bash
# Static publishing artifact only; no deployment or working-card writes.
set -euo pipefail
xtgv_here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec python3 "$xtgv_here/pages.py" package "$@"
