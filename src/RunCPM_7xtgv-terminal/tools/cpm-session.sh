#!/bin/sh
set -eu

tools_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
session=${XTGV_SESSION:-7xtgv-cpm}
case "$session" in
    *[!a-zA-Z0-9_-]*|"") echo "Invalid terminal session name" >&2; exit 1 ;;
esac

if [ -r "$tools_dir/terminal.conf" ]; then
    . "$tools_dir/terminal.conf"
fi

# File-manager and nested GUI launches sometimes inherit TERM=dumb.
case "${TERM-}" in
    ""|dumb) TERM=xterm-256color; export TERM ;;
esac

case "${1-}" in
    --stop)
        exec tmux kill-session -t "$session"
        ;;
    --monitor)
        exec tmux attach-session -r -t "$session"
        ;;
esac

if [ "$#" -eq 0 ]; then
    # Resolve through the same data as the Python tools, after the existing
    # session shortcut below (which must not probe a newly disconnected port).
    resolve_default_port=1
else
    resolve_default_port=0
fi

# An existing shared session already owns the serial port.  A new Konsole
# window is only a tmux attachment, so it must not request an ACL/sudo or try
# to inspect the port before attaching.
if tmux has-session -t "$session" 2>/dev/null; then
    exec tmux attach-session -t "$session"
fi

if [ "$resolve_default_port" -eq 1 ]; then
    port=$(python3 "$tools_dir/target_config.py" --print-port)
    set -- --port "$port"
fi

if [ "${1-}" = "--port" ] && [ -n "${2-}" ]; then
    attempts=0
    while [ ! -e "$2" ] && [ "$attempts" -lt 50 ]; do
        sleep 0.1
        attempts=$((attempts + 1))
    done
fi

configured_for_port_group=0
if [ "${1-}" = "--port" ] && [ -n "${2-}" ] && [ -e "$2" ]; then
    # by-id paths are symlinks owned by root; permissions belong to the device.
    port_group=$(stat -L -c '%G' "$2" 2>/dev/null || true)
    case " $(id -nG "$USER" 2>/dev/null || true) " in
        *" $port_group "*) configured_for_port_group=1 ;;
    esac
fi

# Recreated USB nodes may lose the user's ACL after a reboot or reflash.
# Ask once here so kit.sh/kon.sh work without a separate setup command.
if [ "${1-}" = "--port" ] && [ -n "${2-}" ] && [ -e "$2" ] \
    && [ "$configured_for_port_group" -eq 0 ] \
    && { [ ! -r "$2" ] || [ ! -w "$2" ]; }; then
    if command -v sudo >/dev/null 2>&1; then
        sudo setfacl -m "u:${USER}:rw" "$2" || true
    fi
fi

# A freshly rebooted Linux session may recreate the USB node as root:uucp.
# Keep the terminal visible with the remedy instead of closing immediately.
case "${1-}" in
    --port)
        port=${2-}
        if [ -n "$port" ] && [ -e "$port" ] \
            && [ "$configured_for_port_group" -eq 0 ] \
            && { [ ! -r "$port" ] || [ ! -w "$port" ]; }; then
            echo "Cannot access $port (serial permissions)."
            echo "Run once: sudo setfacl -m u:$USER:rw $port"
            echo "Or add $USER to the device group (usually uucp/dialout), then log in again."
            printf 'Press Enter to close... '
            read dummy
            exit 1
        fi
        ;;
esac

created=0
if ! tmux has-session -t "$session" 2>/dev/null; then
    # terminal.py validates its initial geometry. GUI launchers may open a
    # narrow window, so give the detached shared session a stable baseline.
    tmux new-session -d -x 80 -y 24 -s "$session" -- \
        python3 "$tools_dir/terminal.py" "$@"
    sleep 0.2
    if ! tmux has-session -t "$session" 2>/dev/null; then
        echo "7xtgv terminal failed to start."
        echo "Run $tools_dir/terminal.py with the same target/port to see the error."
        printf 'Press Enter to close... '
        read dummy
        exit 1
    fi
    # HOME is mapped to Ctrl+B for RunCPM, so tmux must not consume it.
    tmux set-option -t "$session" prefix None
    created=1
fi

# The normal Kitty/Konsole window owns the session it created.  Do not leave
# terminal.py holding the serial port after that window is closed.  A separate
# --monitor attachment intentionally does not take this path.
if [ "$created" -eq 1 ]; then
    trap 'tmux kill-session -t "$session" 2>/dev/null || true' EXIT HUP INT TERM
fi
tmux attach-session -t "$session"
