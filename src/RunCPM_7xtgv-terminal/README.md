# 7xtgv RunCPM terminal sketch

This is the build/test entry point for the current RunCPM-based terminal.
For operation, use the [User Guide](../../guides/user-guide.adoc);
for exact commands and settings, the [Reference Guide](../../guides/reference-guide.adoc);
for core, HAL, CCP, protocol and licensing boundaries, the
[Technical Reference Guide](../../guides/technical-reference-guide.adoc).
The device's live INI and firmware identity are not inferred from this README;
run `VTINFO` on the attached device.

## Source layout

The Arduino sketch and RunCPM-derived CPU, BDOS, disk and CCP headers are at
the sketch root. Terminal parser, profiles, history, font and renderer are in
`src/terminal/`; one complete definition per board (including storage,
display, backlight and reboot) is in `hardware/arduino_7xtgv/boards/`.
`board.h` selects it directly; the sketch emits its implementation once.
W535 and W550 use direct SdFat32/SPI. The former W535 SD_MMC bridge and
its build option were removed; dated comparisons remain historical evidence.
Hardware starts at requested 20 MHz, then applies the INI `sdfat_spi` value.
Host launch, transfer, target selection and upload
tools are in `tools/`. Exact targets and port/upload metadata are in
`tools/targets/*.json`; compiled display IDs and default geometry are in
the selected board header, exposed by `src/terminal/display_policy.h`. The headless
[Linux RunCPM](../RunCPM_linux/README.md) build shares the CP/M core but not
the Arduino display/storage stack.

The experimental [Linux desktop runtime](desktop/README.adoc) instead runs
this original Arduino sketch and terminal, with a selected real board's
geometry/INI identity, real Arduino_GFX drawing, an output-only SDL window,
the existing mirror over PTY and an explicit host card directory.

## Build and flash boundary

For a new machine, use the [Build Guide](../../guides/build-guide.adoc).
It separates firmware dependencies, native tests, desktop tools and guest COM
assemblers, and records setup gaps rather than claiming clean-clone validation.

From this sketch directory, build a candidate for the *explicit* attached
target; this example is for the 2-inch RP2350 module:

```sh
python3 tools/reflash.py --target rp2350-touch-lcd-2 --build-only
```

A build-only run stages `../../build/RunCPM_7xtgv-terminal/<target>/new/`
without flashing. Select the real target from `tools/targets/` before
building or uploading; never copy a UF2 from one board to another. The
uploader checks the running firmware's compiled target when that query is
supported and blocks a mismatch. Old unidentified firmware requires an
explicit one-shot recovery allowance, not a persistent bypass. RP2350
modules require manual BOOTSEL/UF2 copy on a separately authorized flash.
The Technical Reference Guide describes the identity guard and staging model.
The local `sketch.yaml` and target metadata specify dependencies and the
upload method. Libraries and the RP2350 core are version-pinned; ESP32
currently uses the installed core (working baseline 3.1.0). Dated port
reports are historical evidence.

## Host-side checks

From this sketch directory:

```sh
python3 -m unittest discover -s tests -p 'test_*.py' -v
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -pthread src/terminal/terminal.cpp src/terminal/history_profiles.cpp tests/test_terminal.cpp -o /tmp/7xtgv-terminal-tests
/tmp/7xtgv-terminal-tests
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g -pthread -Itests/stubs src/terminal/terminal.cpp src/terminal/history_profiles.cpp tests/test_runtime.cpp -o /tmp/7xtgv-runtime-tests
/tmp/7xtgv-runtime-tests
```

Other focused C++ tests are in `tests/test_*.cpp`; Python tests cover
target selection, transfer, INI storage, helpers and host input. The
[validation record](VALIDATION.md) distinguishes checks actually run on
firmware/hardware from host-side checks. The
[feature ledger](../../docs/FEATURES.adoc) carries implementation and
hardware-test state.

## Hardware smoke after an authorized flash

Confirm the boot banner and `VTINFO` identity first. Then check the
TFT/mirror pair, `DIR`, a short MBASIC session, temporary zoom and pan,
a profile/history selection, and reconnect snapshot. For file transfer and
INI changes follow the User Guide, including live-INI fetch and 80-column
logical-width rules. The maintained ASCII MBASIC visual tests are in A7;
the older `tests/VTESTS.BAS` is a historical fixture, not the deployed
source. Record board-specific visual results in dated docs or the feature
ledger, not as an undated default in this README.
