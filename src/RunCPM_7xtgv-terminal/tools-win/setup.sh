#!/usr/bin/env bash
# Explicit Windows/MSYS2 prerequisites; never invoked by rebuild.
set -euo pipefail
xtgv_tools=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
xtgv_packages=(git curl ca-certificates python tmux mintty
    mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake
    mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-SDL2)
case "${1:-}" in
    --dry-run)
        printf '%s\n' 'Windows MSYS2 UCRT64 setup (preview only):'
        printf 'pacman -S --needed'; printf ' %q' "${xtgv_packages[@]}"; printf '\n'
        printf '%s\n' 'Then download and verify Arduino_GFX 1.6.4 into build/.../desktop-win/_deps.'
        exit 0 ;;
    '') ;;
    *) printf '%s\n' 'Usage: setup.sh [--dry-run]' >&2; exit 2 ;;
esac
if [[ ${MSYSTEM:-} != UCRT64 || ! -x /usr/bin/pacman.exe ]]; then
    printf '%s\n' 'Run setup.cmd on Windows, or setup.sh inside MSYS2 UCRT64.' >&2
    exit 1
fi
printf '%s\n' 'First finish the MSYS2 system update described in README.adoc.' \
    'This installs desktop tools and pinned GFX only; no MCU cores or card changes.'
pacman -S --needed "${xtgv_packages[@]}"
/usr/bin/python3 "$xtgv_tools/windows_build.py" setup-gfx
printf '%s\n' 'Setup complete. Choose targets/DISPLAY_ID -- target/desktop-rebuild.cmd.' \
    'Current Windows build uses scripted input; interactive mirror launch is pending.'
