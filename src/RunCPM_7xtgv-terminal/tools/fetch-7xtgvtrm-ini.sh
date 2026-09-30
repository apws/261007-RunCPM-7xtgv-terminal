#!/bin/sh
# Read A0:7XTGVTRM.INI, archive the preceding local mirror, then replace it.
# This helper never writes either the live configuration or the archive to CP/M.
set -eu

tools_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_root=$(CDPATH= cd -- "$tools_dir/.." && pwd)
repo_root=$(CDPATH= cd -- "$source_root/../.." && pwd)
mirror_dir="$repo_root/cpmdrive/A0 - CP-M core"
mirror_file="$mirror_dir/7XTGVTRM.INI"
history_dir="$repo_root/cpmdrive/.7xtgvtrm-ini-history"
temp_dir=$(mktemp -d "${TMPDIR:-/tmp}/7xtgvtrm-ini.XXXXXX")
mirror_stopped=false

cleanup() {
    rm -rf "$temp_dir"
    if [ "$mirror_stopped" = true ]; then
        "$tools_dir/kon.sh" --port "$port" --target "$XTGV_TARGET" >/dev/null 2>&1 &
    fi
}
trap cleanup EXIT HUP INT TERM

# Keep the target/port choice in one place with the regular mirror tools.
. "$tools_dir/terminal.conf"
while [ "$#" -gt 0 ]; do
    case "$1" in
        --target|--port)
            if [ "$#" -lt 2 ] || [ -z "$2" ]; then
                echo "Missing value for $1" >&2
                exit 2
            fi
            case "$1" in
                --target) XTGV_TARGET=$2; export XTGV_TARGET ;;
                --port) XTGV_PORT=$2; export XTGV_PORT ;;
            esac
            shift 2
            ;;
        *) echo "Usage: $0 [--target TARGET] [--port PORT]" >&2; exit 2 ;;
    esac
done
port=$(python3 "$tools_dir/target_config.py" --print-port)

tmux kill-session -t 7xtgv-cpm 2>/dev/null || true
mirror_stopped=true

python3 "$tools_dir/cpm-transfer/transfer.py" --port "$port" --user 0 \
    get 7XTGVTRM.INI --directory "$temp_dir"

if [ ! -s "$temp_dir/7XTGVTRM.INI" ]; then
    echo "Fetch failed: device returned no 7XTGVTRM.INI" >&2
    exit 1
fi

mkdir -p "$history_dir"
if [ -f "$mirror_file" ]; then
    stamp=$(TZ=Europe/Prague date +%m%d%H%M)
    archive="$history_dir/$stamp.INI"
    if [ -e "$archive" ]; then
        echo "Refusing to overwrite existing local snapshot: $archive" >&2
        exit 1
    fi
    cp -p "$mirror_file" "$archive"
    echo "Archived local mirror: $archive"
fi

mv "$temp_dir/7XTGVTRM.INI" "$mirror_file"
echo "Fetched device A0:7XTGVTRM.INI -> $mirror_file"
