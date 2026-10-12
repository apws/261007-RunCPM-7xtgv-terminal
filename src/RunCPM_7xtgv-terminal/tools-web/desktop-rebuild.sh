#!/usr/bin/env bash
# Experimental web proof only; no MCU build or card changes.
set -euo pipefail
xtgv_here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
xtgv_root=$(cd -- "$xtgv_here/../../.." && pwd)
xtgv_build="$xtgv_root/build/RunCPM_7xtgv-terminal/desktop-web"
[[ -f "$xtgv_build/_deps/emsdk/emsdk_env.sh" ]] || { echo 'Run tools-web/setup.sh first.' >&2; exit 1; }
source "$xtgv_build/_deps/emsdk/emsdk_env.sh" >/dev/null 2>&1
xtgv_gfx=${XTGV_GFX_SOURCE:-"$HOME/Arduino/libraries/GFX_Library_for_Arduino"}
mkdir -p "$xtgv_build/shared"
printf '%s\n' 'Building experimental web ES3_35 proof ...'
emcmake cmake -S "$xtgv_here/../desktop/web" -B "$xtgv_build/shared" -G "Unix Makefiles" \
    -DXTGV_GFX_SOURCE="$xtgv_gfx" > "$xtgv_build/shared/configure.log" 2>&1 || { cat "$xtgv_build/shared/configure.log" >&2; exit 1; }
cmake --build "$xtgv_build/shared" -j 2 2>&1 | tee "$xtgv_build/shared/build.log"
python3 "$xtgv_here/build_assets.py"
printf '%s\n' 'Web proof built. Launch with tools-web/desktop-local.sh.'
