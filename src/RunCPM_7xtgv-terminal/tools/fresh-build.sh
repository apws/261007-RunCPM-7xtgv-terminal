#!/usr/bin/env bash
# Dependency-isolated Linux test; no host package installation or device access.
set -euo pipefail

xtgv_tools=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
xtgv_repo=$(cd -- "$xtgv_tools/../../.." && pwd)
xtgv_target=esp32c6-lcd-1-47
xtgv_source=https://github.com/apws/260906-7xtgv.git
xtgv_ref=
xtgv_snapshot=0
xtgv_cli_version=1.5.0
xtgv_existing_cli=

usage() {
    printf '%s\n' 'Usage: fresh-build.sh [--target TARGET|all] [--source GIT_URL_OR_PATH]' \
        '       [--ref COMMIT_OR_TAG] [--snapshot] [--use-cli PATH] [--cli-version VERSION]' \
        'Default: download checksummed Arduino CLI 1.5.0, clone GitHub, install baseline' \
        'cores/libraries in empty directories, build C6 only. No flash, no sudo.' \
        '--snapshot tests current non-ignored working files, NOT a published clone.' \
        '--use-cli reuses only that executable, never its existing package/cache dirs.' \
        'Needs Linux, bash, git, python3, curl, tar, sha256sum and CA certificates.' \
        'Artifacts/logs remain in a new temporary directory; nothing is auto-deleted.'
}
while (($#)); do
    case "$1" in
        --target) xtgv_target=$2; shift 2 ;;
        --source) xtgv_source=$2; shift 2 ;;
        --ref) xtgv_ref=$2; shift 2 ;;
        --snapshot) xtgv_snapshot=1; shift ;;
        --cli-version) xtgv_cli_version=$2; shift 2 ;;
        --use-cli) xtgv_existing_cli=$2; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) usage; exit 2 ;;
    esac
done
if [[ ! "$xtgv_cli_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf '%s\n' 'CLI version must be a numeric release version.' >&2; exit 2
fi
if ((xtgv_snapshot)) && [[ -n "$xtgv_ref" ]]; then
    printf '%s\n' '--snapshot and --ref are mutually exclusive.' >&2; exit 2
fi
for xtgv_command in git python3 curl tar sha256sum; do
    command -v "$xtgv_command" >/dev/null || {
        printf 'Missing host prerequisite: %s\n' "$xtgv_command" >&2; exit 1;
    }
done
xtgv_run=$(mktemp -d "${TMPDIR:-/tmp}/xtgv-fresh-build-XXXXXX")
printf 'Isolated run (retained): %s\n' "$xtgv_run"
exec > >(tee "$xtgv_run/run.log") 2>&1
trap 'xtgv_status=$?; printf "\nRun exit status: %s; logs: %s\n" "$xtgv_status" "$xtgv_run"' EXIT

# Environment overrides otherwise take precedence over our explicit config.
for xtgv_variable in ${!ARDUINO_@}; do unset "$xtgv_variable"; done
unset XTGV_PORT XTGV_TARGET XTGV_ARDUINO_CONFIG
export XDG_CACHE_HOME="$xtgv_run/cache"
export TMPDIR="$xtgv_run/tmp"
mkdir -p "$TMPDIR" "$xtgv_run/bin" "$xtgv_run/arduino-data" \
    "$xtgv_run/downloads" "$xtgv_run/sketchbook" "$XDG_CACHE_HOME"
uname -a
python3 --version
git --version

if [[ -n "$xtgv_existing_cli" ]]; then
    xtgv_cli=$(realpath -- "$xtgv_existing_cli")
    [[ -x "$xtgv_cli" ]] || { printf '%s\n' 'CLI is not executable.' >&2; exit 1; }
else
    case "$(uname -m)" in
        x86_64) xtgv_arch=Linux_64bit ;;
        aarch64) xtgv_arch=Linux_ARM64 ;;
        *) printf '%s\n' 'Unsupported download architecture; use --use-cli.' >&2; exit 1 ;;
    esac
    xtgv_archive="arduino-cli_${xtgv_cli_version}_${xtgv_arch}.tar.gz"
    xtgv_url="https://github.com/arduino/arduino-cli/releases/download/v${xtgv_cli_version}"
    curl --fail --location --retry 2 "$xtgv_url/$xtgv_archive" -o "$xtgv_run/$xtgv_archive"
    curl --fail --location --retry 2 "$xtgv_url/${xtgv_cli_version}-checksums.txt" \
        -o "$xtgv_run/checksums.txt"
    (cd "$xtgv_run" && sha256sum --check --ignore-missing checksums.txt)
    tar -xzf "$xtgv_run/$xtgv_archive" -C "$xtgv_run/bin" arduino-cli
    xtgv_cli="$xtgv_run/bin/arduino-cli"
fi
export ARDUINO_CLI="$xtgv_cli"
export XTGV_ARDUINO_CONFIG="$xtgv_run/arduino-cli.yaml"
"$xtgv_cli" config init --dest-file "$XTGV_ARDUINO_CONFIG"
xtgv_config() { "$xtgv_cli" --config-file "$XTGV_ARDUINO_CONFIG" "$@"; }
xtgv_config config set directories.data "$xtgv_run/arduino-data"
xtgv_config config set directories.downloads "$xtgv_run/downloads"
xtgv_config config set directories.user "$xtgv_run/sketchbook"
xtgv_config config set build_cache.path "$xtgv_run/build-cache"
xtgv_config version
xtgv_config config dump

if ((xtgv_snapshot)); then
    printf '%s\n' 'MODE: working-tree snapshot, not clean-clone proof.'
    mkdir "$xtgv_run/repo"
    (cd "$xtgv_repo" && git ls-files --cached --others --exclude-standard -z |
        tar --null -T - -cf -) | tar -xf - -C "$xtgv_run/repo"
    git -C "$xtgv_repo" rev-parse HEAD
    git -C "$xtgv_repo" status --short
else
    git clone --no-hardlinks "$xtgv_source" "$xtgv_run/repo"
    if [[ -n "$xtgv_ref" ]]; then git -C "$xtgv_run/repo" switch --detach "$xtgv_ref"; fi
    git -C "$xtgv_run/repo" rev-parse HEAD
fi
xtgv_project="$xtgv_run/repo/src/RunCPM_7xtgv-terminal"
if [[ "$xtgv_target" == all ]]; then
    xtgv_targets=(jc8048w535 jc8048w550 jc8012p4a1c rp2350-lcd-1-47-a \
        rp2350-touch-lcd-2 esp32c6-lcd-1-47)
else
    xtgv_targets=("$xtgv_target")
fi
xtgv_esp=0; xtgv_rp=0
for xtgv_item in "${xtgv_targets[@]}"; do
    python3 "$xtgv_project/tools/target_config.py" --target "$xtgv_item"
    case "$xtgv_item" in rp2350-*) xtgv_rp=1 ;; *) xtgv_esp=1 ;; esac
done
if ((xtgv_esp)); then
    xtgv_config config add board_manager.additional_urls \
        https://espressif.github.io/arduino-esp32/package_esp32_index.json
fi
if ((xtgv_rp)); then
    xtgv_config config add board_manager.additional_urls \
        https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
fi
xtgv_config core update-index
if ((xtgv_esp)); then xtgv_config core install esp32:esp32@3.1.0; fi
if ((xtgv_rp)); then xtgv_config core install rp2040:rp2040@5.4.4; fi
xtgv_config lib update-index
xtgv_config lib install 'GFX Library for Arduino@1.6.4' 'SdFat@2.3.0'
xtgv_config core list
xtgv_config lib list
for xtgv_item in "${xtgv_targets[@]}"; do
    python3 "$xtgv_project/tools/reflash.py" --target "$xtgv_item" --build-only
done
printf '%s\n' 'Build-only pass. No device, mirror, permissions or SD bootstrap tested.'
