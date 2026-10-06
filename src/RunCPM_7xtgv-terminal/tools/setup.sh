#!/usr/bin/env bash
# Auto-detected Linux setup. Never build, flash or run as root.
set -euo pipefail
xtgv_dry=0
xtgv_yes=0
xtgv_serial_only=0
for xtgv_arg in "$@"; do
    case "$xtgv_arg" in
        --dry-run) xtgv_dry=1 ;;
        --yes) xtgv_yes=1 ;;
        --serial-only) xtgv_serial_only=1 ;;
        --help|-h)
            printf '%s\n' 'Usage: setup.sh [--dry-run] [--yes] [--serial-only]' \
                'Detects Arch/Debian families or Fedora; installs packages with sudo,' \
                'then Arduino CLI (if absent), pinned cores and libraries as your user.' \
                'Offers USB serial-group access separately; --yes does not approve it.
--serial-only skips package and Arduino installation. No build or flash.'
            exit 0 ;;
        *) printf 'Unknown argument: %s\n' "$xtgv_arg" >&2; exit 2 ;;
    esac
done
if ((EUID == 0)); then
    printf '%s\n' 'Run setup as your normal user, not through sudo.' >&2; exit 1
fi
source /etc/os-release
# Prefer exact ID, then the distro's ordered compatibility families.
xtgv_family=
for xtgv_candidate in "$ID" ${ID_LIKE:-}; do
    case "$xtgv_candidate" in
        arch|debian|fedora) xtgv_family=$xtgv_candidate; break ;;
    esac
done
case "$xtgv_family" in
    arch)
        xtgv_label='Arch and derivatives'; xtgv_manager=pacman
        xtgv_packages=(git python curl tar ca-certificates tmux util-linux udisks2 sudo acl shadow)
        ;;
    debian)
        xtgv_label='Debian and derivatives'; xtgv_manager=apt-get
        xtgv_packages=(git python3 curl tar ca-certificates tmux util-linux udisks2 sudo acl passwd coreutils)
        ;;
    fedora)
        xtgv_label='Fedora family'; xtgv_manager=dnf
        xtgv_packages=(git python3 curl tar ca-certificates tmux util-linux udisks2 sudo acl shadow-utils coreutils)
        ;;
    *) printf 'Unsupported Linux distribution: %s (ID_LIKE=%s).\n' "$ID" "${ID_LIKE:-}" >&2; exit 1 ;;
esac
xtgv_install_packages() {
    case "$xtgv_family" in
        arch) sudo pacman -S --needed "${xtgv_packages[@]}" ;;
        debian)
            sudo apt-get update
            sudo apt-get install -y "${xtgv_packages[@]}"
            ;;
        fedora) sudo dnf install -y "${xtgv_packages[@]}" ;;
    esac
}
printf '%s\n' "Setup: $xtgv_label prerequisites + Arduino dependencies for all six modules." \
    "System: ${xtgv_packages[*]}." \
    'Arduino: existing CLI, or checksummed CLI 1.5.0 in ~/.local/bin.' \
    'Cores: esp32 3.1.0, rp2040 5.4.4; libraries: GFX 1.6.4, SdFat 2.3.0.' \
    'No system upgrade or firmware build/flash. USB group access asks separately.'
if ((xtgv_dry)); then exit 0; fi
xtgv_tools=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if ((xtgv_serial_only)); then
    exec python3 "$xtgv_tools/setup_serial.py"
fi
if ((!xtgv_yes)); then
    xtgv_prompt='Install these components? [y/N] '
    if [[ -t 1 && "${TERM:-}" != dumb && ! -v NO_COLOR ]]; then
        xtgv_prompt=$'\033[31m'"$xtgv_prompt"$'\033[0m'
    fi
    read -r -p "$xtgv_prompt" xtgv_answer
    [[ "$xtgv_answer" == y || "$xtgv_answer" == Y ]] || exit 0
fi
command -v sudo >/dev/null || {
    printf '%s\n' 'sudo is missing; ask your administrator to install it first.' >&2; exit 1;
}
xtgv_install_packages
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
    'USB serial-group access is checked separately below.' \
    'Konsole/Kitty are optional; without them the mirror uses the current terminal.'

if ((!xtgv_yes)); then
    python3 "$xtgv_tools/setup_serial.py"
else
    printf '%s\n' 'For USB permissions, run ./setup.sh --serial-only with the device connected.'
fi
