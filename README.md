# 7xtgv

7xtgv is an RP2350 Touch LCD 2 project combining RunCPM with a compact
VT100/VT52 terminal. CP/M runs on one core while the other handles USB input,
terminal interpretation, the 320x240 TFT, and an 80x24 PC terminal mirror.

The current main firmware is
[`src/7xtgv-vt100-runcpm`](src/7xtgv-vt100-runcpm/README.md). It provides a
52x24 panned TFT viewport, ANSI and VT52 compatibility, the QL-derived 6x10
font, the Mixworx color palette, and console file-transfer helpers.

## Repository layout

| Path | Contents |
| --- | --- |
| [`src/`](src/) | Current firmware and earlier demo/source projects |
| [`build/`](build/) | Generated Arduino build output; ignored by Git |
| [`cpmdrive/`](cpmdrive/README.md) | Documented mirror of CP/M drive A user areas |
| [`cpm-languages/`](cpm-languages/SOURCES.md) | Language-package sources and provenance |
| [`docs/`](docs/) | Recovery notes, reference material, and archived experiments |
| `backups/` | Local generated backups; ignored by Git |

## Build

The main sketch uses the pinned Arduino profile in `sketch.yaml`:

```sh
cd src/7xtgv-vt100-runcpm
arduino-cli compile --profile rp2350-touch-lcd-2 \
  --output-dir ../../build/build-vt100-runcpm .
```

## Test

Run the host-side checks from the main project directory:

```sh
cd src/7xtgv-vt100-runcpm
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

For the PC keyboard and full 80-column terminal mirror:

```sh
python3 tools/terminal.py
```

Press `Ctrl+]`, then `q`, to leave the terminal without rebooting CP/M. See
the [main project documentation](src/7xtgv-vt100-runcpm/README.md),
[validation record](src/7xtgv-vt100-runcpm/VALIDATION.md), and
[CP/M user-area map](cpmdrive/README.md) for details.
