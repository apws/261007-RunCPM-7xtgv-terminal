#!/bin/sh
set -eu

tools_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$tools_dir/../../.." && pwd)
binary="$repo_root/build/RunCPM_linux/RunCPM"
card_work="$repo_root/build/RunCPM_linux/card-work"

if [ ! -x "$binary" ]; then
    printf 'Linux RunCPM is not built. Run: make -C %s/src/RunCPM_linux\n' "$repo_root" >&2
    exit 1
fi
if [ ! -d "$card_work/A/0" ]; then
    printf 'Writable CP/M tree is missing: %s/A/0\n' "$card_work" >&2
    exit 1
fi

cd "$card_work"
exec "$binary"
