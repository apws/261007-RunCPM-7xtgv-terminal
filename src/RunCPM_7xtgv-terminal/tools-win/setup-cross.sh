#!/usr/bin/env bash
# Optional Linux-to-Windows build prerequisite setup; never called by rebuild.
set -euo pipefail
case "${1:---dry-run}" in
    --dry-run) xtgv_install=0 ;;
    --install) xtgv_install=1 ;;
    *) printf '%s\n' 'Usage: setup-cross.sh [--dry-run|--install]'; exit 2 ;;
esac
source /etc/os-release
case " $ID ${ID_LIKE:-} " in
    *' arch '*) xtgv_command=(sudo pacman -S --needed mingw-w64-gcc) ;;
    *) printf '%s\n' 'This optional cross-toolchain setup currently supports Arch derivatives only.' >&2; exit 1 ;;
esac
printf '%s\n' 'Install the MinGW-w64 cross compiler and its CRT/headers/binutils dependencies.' \
    'No system upgrade, Arduino setup, build, launch or card operation.'
printf 'Command:'; printf ' %q' "${xtgv_command[@]}"; printf '\n'
if ((xtgv_install)); then "${xtgv_command[@]}"; fi
