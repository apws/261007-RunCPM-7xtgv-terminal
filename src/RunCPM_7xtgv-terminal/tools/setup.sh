#!/usr/bin/env bash
# Explicit Arch/CachyOS setup. Never build, flash, change groups or run as root.
set -euo pipefail
xtgv_dry=0
xtgv_yes=0
for xtgv_arg in "$@"; do
    case "$xtgv_arg" in
        --dry-run) xtgv_dry=1 ;;
        --yes) xtgv_yes=1 ;;
        --help|-h)
            printf '%s\n' 'Usage: setup.sh [--dry-run] [--yes]' \
                'Arch/CachyOS only. Installs system prerequisites with sudo pacman,' \
                'then Arduino CLI (if absent), pinned cores and libraries as your user.' \
                'No build, flash, group changes or desktop installation.'
            exit 0 ;;
        *) printf 'Unknown argument: %s\n' "$xtgv_arg" >&2; exit 2 ;;
    esac
done
if ((EUID == 0)); then
    printf '%s\n' 'Run setup as your normal user, not through sudo.' >&2; exit 1
fi
source /etc/os-release
if [[ "$ID" != arch && " ${ID_LIKE:-} " != *' arch '* ]]; then
    printf '%s\n' 'Only Arch Linux and Arch-derived distributions are supported.' >&2; exit 1
fi
printf '%s\n' 'Setup: Arch system prerequisites + Arduino dependencies for all six modules.' \
    'System: git python curl tar ca-certificates tmux util-linux udisks2 sudo.' \
    'Arduino: existing CLI, or checksummed CLI 1.5.0 in ~/.local/bin.' \
    'Cores: esp32 3.1.0, rp2040 5.4.4; libraries: GFX 1.6.4, SdFat 2.3.0.' \
    'No system upgrade, firmware build/flash, or serial permission changes.'
if ((xtgv_dry)); then exit 0; fi
if ((!xtgv_yes)); then
    read -r -p 'Install these components? [y/N] ' xtgv_answer
    [[ "$xtgv_answer" == y || "$xtgv_answer" == Y ]] || exit 0
fi
command -v sudo >/dev/null || {
    printf '%s\n' 'sudo is missing; ask your administrator to install it first.' >&2; exit 1;
}
sudo pacman -S --needed git python curl tar ca-certificates tmux util-linux udisks2 sudo
xtgv_cli=${ARDUINO_CLI:-}
if [[ -z "$xtgv_cli" ]]; then
    xtgv_cli=$(command -v arduino-cli || true)
    if [[ -z "$xtgv_cli" && -x "$HOME/.local/bin/arduino-cli" ]]; then
        xtgv_cli="$HOME/.local/bin/arduino-cli"
    fi
fi
if [[ -z "$xtgv_cli" ]]; then
    case "$(uname -m)" in
        x86_64) xtgv_arch=Linux_64bit ;;
        aarch64) xtgv_arch=Linux_ARM64 ;;
        *) printf '%s\n' 'Unsupported CLI download architecture; set ARDUINO_CLI.' >&2; exit 1 ;;
    esac
    xtgv_temp=$(mktemp -d)
    printf 'CLI download workspace (retained): %s\n' "$xtgv_temp"
    xtgv_url=https://github.com/arduino/arduino-cli/releases/download/v1.5.0
    xtgv_archive="arduino-cli_1.5.0_${xtgv_arch}.tar.gz"
    curl --fail --location --retry 2 "$xtgv_url/$xtgv_archive" -o "$xtgv_temp/$xtgv_archive"
    curl --fail --location --retry 2 "$xtgv_url/1.5.0-checksums.txt" -o "$xtgv_temp/checksums.txt"
    (cd "$xtgv_temp" && sha256sum --check --ignore-missing checksums.txt)
    tar -xzf "$xtgv_temp/$xtgv_archive" -C "$xtgv_temp" arduino-cli
    mkdir -p "$HOME/.local/bin"
    install -m 755 "$xtgv_temp/arduino-cli" "$HOME/.local/bin/arduino-cli"
    xtgv_cli="$HOME/.local/bin/arduino-cli"
fi
xtgv_options=()
if [[ -n "${XTGV_ARDUINO_CONFIG:-}" ]]; then
    xtgv_options=(--config-file "$XTGV_ARDUINO_CONFIG")
fi
xtgv_arduino() { "$xtgv_cli" "${xtgv_options[@]}" "$@"; }
xtgv_arduino version
xtgv_arduino core update-index --additional-urls \
    https://espressif.github.io/arduino-esp32/package_esp32_index.json,https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
xtgv_arduino core install esp32:esp32@3.1.0 --additional-urls \
    https://espressif.github.io/arduino-esp32/package_esp32_index.json
xtgv_arduino core install rp2040:rp2040@5.4.4 --additional-urls \
    https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
xtgv_arduino lib update-index
xtgv_arduino lib install 'GFX Library for Arduino@1.6.4' 'SdFat@2.3.0'
printf '%s\n' 'Setup complete. Run your module rebuild.sh, then reflash.sh separately.' \
    'USB permissions may still need administrator configuration; no groups were changed.' \
    'Konsole/Kitty are optional; without them the mirror uses the current terminal.'
