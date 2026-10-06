#!/usr/bin/env bash
# Launch the terminal mirror without building or uploading firmware.
set -euo pipefail
xtgv_tools=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
command -v python3 >/dev/null || {
    printf '%s\n' 'Missing python3. Run tools/setup.sh.' >&2
    exit 1
}
exec python3 "$xtgv_tools/target_mirror.py" "$@" --target jc8048w535
