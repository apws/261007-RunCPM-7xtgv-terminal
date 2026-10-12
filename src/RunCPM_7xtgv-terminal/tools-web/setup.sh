#!/usr/bin/env bash
# Explicit, project-local prerequisites for the experimental browser runtime.
set -euo pipefail
xtgv_here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
xtgv_root=$(cd -- "$xtgv_here/../../.." && pwd)
xtgv_deps="$xtgv_root/build/RunCPM_7xtgv-terminal/desktop-web/_deps"
mkdir -p "$xtgv_deps"
if [[ ! -f "$xtgv_deps/emsdk/emsdk" ]]; then
    git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$xtgv_deps/emsdk"
fi
printf '%s\n' 'Installing project-local Emscripten 6.0.12 ...'
"$xtgv_deps/emsdk/emsdk" install 6.0.12
"$xtgv_deps/emsdk/emsdk" activate 6.0.12
python3 -m venv "$xtgv_deps/venv"
"$xtgv_deps/venv/bin/python" -m pip install -r "$xtgv_here/requirements.txt"
printf '%s\n' 'Web proof prerequisites ready. No emulator build or card changes.'
