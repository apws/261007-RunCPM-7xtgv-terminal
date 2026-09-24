#!/bin/sh
# Host action for A0:REFLASH on the currently attached JC8048W535 ESP32-S3.
# Compatibility entry point; the target data/environment selects the port.
set -eu

tools_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$tools_dir/reflash.py" --target jc8048w535 "$@"
