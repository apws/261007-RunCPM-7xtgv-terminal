# 7xtgv

7xtgv combines RunCPM with a compact ANSI/VT100- and VT52-style terminal on
several RP2350 and ESP32-S3 display modules. The device TFT shows a zoomable,
pannable viewport; the PC mirror shows the complete logical CP/M screen.

The current main firmware is
[`src/RunCPM_7xtgv-terminal`](src/RunCPM_7xtgv-terminal/README.md). Start with the
[User Guide](docs/260912-23%207xtgv%20terminal%20manual/user-guide.adoc); use
the [Reference Guide](docs/260912-23%207xtgv%20terminal%20manual/reference-guide.adoc)
for exact commands/settings and the
[Technical Reference](docs/260912-23%207xtgv%20terminal%20manual/technical-reference.adoc)
for the RunCPM fork, HAL, protocol and license boundaries.

## Repository layout

| Path | Contents |
| --- | --- |
| [`src/`](src/) | Current firmware and earlier demo/source projects |
| [`build/`](build/) | Generated Arduino build output; ignored by Git |
| [`cpmdrive/`](cpmdrive/README.md) | Documented mirror of CP/M drive A user areas |
| [`docs/`](docs/) | [Central feature ledger](docs/FEATURES.adoc), recovery notes, reference material, and archived experiments |
| `backups/` | Local generated backups; ignored by Git |
| `private/`, `docs/private/` | Local-only material; ignored by Git and absent from a fresh clone |

## Feature registry

[`docs/features.tsv`](docs/features.tsv) is the canonical ASCII feature store;
[`docs/FEATURES.adoc`](docs/FEATURES.adoc) is its human ledger plus hand-written
status and evidence notes. Validate, query, or regenerate it with:

```sh
python3 src/feature_registry.py check
python3 src/feature_registry.py report --format text \
  --fields group,idea_date,state,title --sort group,idea_date,title
python3 src/feature_registry.py report --format asciidoc \
  --where group=INP --fields idea_date,title,state,qualifier,next_gate
python3 src/feature_registry.py update-ledger
```

Miller can query the same file directly with `mlr --itsv`; it is optional and
is not required for deterministic ledger generation.

## Build

The main sketch uses the pinned Arduino profile in `sketch.yaml`:

```sh
cd src/RunCPM_7xtgv-terminal
arduino-cli compile --profile rp2350-touch-lcd-2 \
  --output-dir ../../build/build-vt100-runcpm .
```

## Test

Run the host-side checks from the main project directory:

```sh
cd src/RunCPM_7xtgv-terminal
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

For the PC keyboard and full 80-column terminal mirror:

```sh
python3 tools/terminal.py
```

Press `Ctrl+]`, then `q`, to leave the terminal without rebooting CP/M. See
the [main project documentation](src/RunCPM_7xtgv-terminal/README.md),
[validation record](src/RunCPM_7xtgv-terminal/VALIDATION.md), and
[CP/M user-area map](cpmdrive/README.md) for details.
