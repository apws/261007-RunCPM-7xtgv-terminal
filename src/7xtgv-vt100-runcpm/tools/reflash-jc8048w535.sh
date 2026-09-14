#!/bin/sh
# Host action for A0:REFLASH on the currently attached JC8048W535 ESP32-S3.
# terminal.py has already released /dev/ttyACM0 before invoking this script.
set -eu

tools_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(CDPATH= cd -- "$tools_dir/.." && pwd)
arduino_cli=${ARDUINO_CLI:-/home/falken/.local/bin/arduino-cli}

exec "$arduino_cli" upload --profile jc8048w535 --port /dev/ttyACM0 "$project_dir"
