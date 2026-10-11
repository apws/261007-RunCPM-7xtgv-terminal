#!/bin/sh
set -eu

tools_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
konsole_dir=${XDG_DATA_HOME:-"$HOME/.local/share"}/konsole

# Konsole discovers profiles and key tables only through its data directories.
install -Dm644 "$tools_dir/7xtgv-terminal.profile" \
    "$konsole_dir/7xtgv-terminal.profile"
install -Dm644 "$tools_dir/7xtgv-terminal.keytab" \
    "$konsole_dir/7xtgv-terminal.keytab"

exec konsole \
    --separate \
    --profile "7xtgv-terminal" \
    -p "KeyBindings=7xtgv-terminal" \
    -e "$tools_dir/cpm-session.sh" "$@"
