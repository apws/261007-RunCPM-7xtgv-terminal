#!/bin/sh
set -eu

tools_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
konsole_dir=${XDG_DATA_HOME:-"$HOME/.local/share"}/konsole

install -Dm644 "$tools_dir/7xtgv-linux-runcpm.profile" \
    "$konsole_dir/7xtgv-linux-runcpm.profile"

exec konsole \
    --separate \
    --profile "7xtgv-linux-runcpm" \
    -e "$tools_dir/run.sh"
