#!/usr/bin/env bash
# Serve the standalone browser over HTTPS on the trusted LAN for phone access.
set -euo pipefail
xtgv_here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec "$xtgv_here/desktop-local.sh" --lan --https --no-open --port 36341 \
    --target jc8048w535 --window-scale 1 "$@"
