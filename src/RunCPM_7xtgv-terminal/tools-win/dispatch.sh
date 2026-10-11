#!/usr/bin/env bash
set -euo pipefail
xtgv_tools=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
case "${1:-}" in
    setup) shift; exec /usr/bin/bash "$xtgv_tools/setup.sh" "$@" ;;
    build)
        shift
        if [[ ! -x /usr/bin/python3 ]]; then
            printf '%s\n' 'MSYS Python is missing. Run tools-win/setup.cmd first.' >&2
            exit 1
        fi
        exec /usr/bin/python3 "$xtgv_tools/windows_build.py" build "$@" ;;
    launch)
        shift
        exec /usr/bin/python3 "$xtgv_tools/windows_run.py" launch "$@" ;;
    performance)
        shift
        exec /usr/bin/python3 "$xtgv_tools/../desktop/tests/display_performance.py" "$@" ;;
    stage-local)
        shift
        exec /usr/bin/python3 "$xtgv_tools/windows_stage.py" "$@" ;;
    storage-profile)
        shift
        exec /usr/bin/python3 "$xtgv_tools/storage_profile.py" "$@" ;;
    keyboard-probe)
        shift
        exec /usr/bin/python3 "$xtgv_tools/keyboard_probe.py" "$@" ;;
    mirror-proof)
        shift
        exec /usr/bin/python3 "$xtgv_tools/../desktop/tests/mirror_loopback.py" "$@" ;;
    proof)
        shift
        exec /usr/bin/python3 "$xtgv_tools/../desktop/tests/loopback_proof.py" "$@" ;;
    *) printf '%s\n' 'Expected setup, build, launch, proof, mirror-proof, performance, stage-local, storage-profile or keyboard-probe action.' >&2; exit 2 ;;
esac
