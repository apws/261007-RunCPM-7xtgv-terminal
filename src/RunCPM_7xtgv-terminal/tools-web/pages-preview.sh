#!/usr/bin/env bash
# Local Pages-like hosting without COOP/COEP response headers.
set -euo pipefail
xtgv_here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec python3 "$xtgv_here/pages.py" preview "$@"
