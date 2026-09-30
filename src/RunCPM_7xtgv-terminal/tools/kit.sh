#!/bin/sh
set -eu

tools_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

exec kitty \
    --config "$tools_dir/kitty.conf" \
    "$tools_dir/cpm-session.sh" "$@"
