#!/usr/bin/env bash
set -euo pipefail
xtgv_here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec bash "$xtgv_here/../../desktop-rebuild.sh" "$@"
