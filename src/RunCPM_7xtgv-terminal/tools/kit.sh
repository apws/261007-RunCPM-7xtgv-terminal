#!/bin/sh
set -eu

tools_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

config=$(python3 "$tools_dir/generate_key_events.py" --host kitty \
    --output-dir "${XDG_CACHE_HOME:-$HOME/.cache}/7xtgv-terminal")

exec kitty \
    --config "$config" \
    "$tools_dir/cpm-session.sh" "$@"
