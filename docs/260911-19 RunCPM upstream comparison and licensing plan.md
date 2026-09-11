# RunCPM upstream comparison and licensing plan

## Scope and method

This audit compares `src/7xtgv-vt100-runcpm` with a fresh local clone of
[MockbaTheBorg/RunCPM](https://github.com/MockbaTheBorg/RunCPM), fetched on
2026-09-11 at upstream commit `79396be665d6ec263a2728b20b177f4149647ed8`.
The comparison checkout is local-only at `/tmp/runcpm-upstream-compare` and is
not part of this repository.

The recovered 7xtgv project did **not** begin as a Git fork tied to a particular
upstream SHA. During recovery, the RunCPM source was reconstructed from the
available recovered material and matching public RunCPM sources. Therefore the
comparison identifies current provenance and practical upstream candidates; it
cannot prove a single exact historical upstream base for every recovered line.

The upstream `master` snapshot itself contains a newer ESP32-S3 LCD adapter,
committed 2026-08-24. It is for a different Waveshare board and SD-over-SPI
wiring, not the JC8048W535's one-bit SDMMC wiring.

## License finding

Upstream RunCPM is MIT licensed: copyright © 2017 Mockba the Borg. The MIT text
permits modification, distribution, sublicensing and sale, provided the
copyright and permission notice accompany substantial upstream portions.
`src/7xtgv-vt100-runcpm/LICENSE.RunCPM` already preserves that notice.

This permits a combined 7xtgv firmware, including proprietary 7xtgv code,
because MIT is permissive. It does **not** make the upstream RunCPM portions
proprietary: recipients retain the MIT rights to those portions.

The repository currently has no top-level project licence. Therefore its
original material is legally unlicensed by default, while the RunCPM copies are
licensed through their local `LICENSE.RunCPM` files. The QL font header already
states `Copyright 2026 Petr Antos, ALL RIGHTS RESERVED`.

## Current code inventory

| Area | Provenance and role | Licence handling |
|---|---|---|
| `src/7xtgv-vt100-runcpm/{ccp,cpm,cpu*,disk,ram,console,globals,abstraction_arduino}.h` | RunCPM-derived core and Arduino port, with local fixes and target integration. | Keep the MIT notice with every redistributed copy. Local modifications may be separately copyrighted, but must not remove MIT rights from the upstream portions. |
| `src/7xtgv-vt100-runcpm/7xtgv-vt100-runcpm.ino`, `runcpm-config.h`, `hal/` | Local board integration: RP2350 SPI target and JC8048W535 SDMMC target, FreeRTOS task arrangement and startup reporting. | New code; distributed together with a RunCPM-derived sketch, so retain `LICENSE.RunCPM` in this subtree. |
| `terminal*.{cpp,h}`, `history_profiles.*`, `line_history.h`, `byte_queue.h` | New 7xtgv terminal, ANSI parser/rendering, application profiles, history banks, local compose and persistence. `terminal_runtime.cpp` is coupled to RunCPM through virtual serial queues. | Original 7xtgv work. It can have a different licence once physically separated, while the combined firmware still carries the RunCPM MIT notice. |
| `7xtgv-QL-font.h`, `7xtgv-palette.h`, `7xtgv-gfxsetup.h` | 7xtgv-specific font, palette and display layer. | Treat as Petr Antos material; keep its current all-rights-reserved status unless deliberately changed. |
| `tools/`, CP/M transfer utilities, VTBENCH/REBOOT programs, tests, `cpmdrive/` mirror | Predominantly local tooling and test assets; some CP/M packages retain their own provenance. | Add per-package notices before broad redistribution of third-party binaries. |
| `src/7xtgv_RunCPM` | Recovered secondary RunCPM copy. | Also preserve its `LICENSE.RunCPM`; avoid changing it until its role is decided. |

## Upstream difference map

The current upstream comparison reports these local additions absent from
upstream: the 7xtgv sketch, display/font/palette files, terminal and history
implementation, `hal/`, tests, CP/M transport and benchmarks. Upstream files
not carried here are its desktop/Windows/Posix builds, project files and its
broader `hardware/` board directory.

Local modifications to upstream-derived files fall into these groups:

- **Portable Arduino storage safety:** `_diskActivity()` makes an activity LED
  optional, and `_sys_extendfile()` correctly extends files in bounded chunks.
  The latter fixes an upstream subtraction-direction bug and has a local test.
- **RunCPM operating behaviour:** CCP/BDOS/disk/console changes, interrupt
  handoff and emulator configuration that support the current CP/M runtime.
- **ESP32-S3 performance and scheduling:** IRAM placement, pinned RunCPM task,
  narrow idle window, SDMMC adapter and terminal/RunCPM queues.
- **7xtgv host services:** private benchmark ports, terminal benchmark,
  deferred reboot and CP/M test commands.

## Recommended repository licence shape

Do not add a blanket top-level open-source licence yet. It would accidentally
make the 7xtgv-specific material available on terms Petr has not chosen.

When ready, add these three short documents:

1. `LICENSE-7XTGV` — chosen licence for original 7xtgv code and assets. It may
   remain proprietary for now.
2. `THIRD_PARTY_NOTICES.md` — identify RunCPM as MIT, link/copy its full notice,
   list bundled CP/M language/tool packages and their known status.
3. `LICENSE` or `README` scope statement — say that the repository combines
   original 7xtgv material with independently licensed third-party components;
   point readers to the two documents above.

Keep `LICENSE.RunCPM` in each RunCPM-derived tree even after adding a top-level
notice. This avoids ambiguity for a future extracted terminal or a hardware PR.

## Upstream PR candidates

Good small candidates, after isolating them against the current upstream tree:

1. `_sys_extendfile()` correction and its unit test. It is board-neutral and
   independently useful.
2. Optional activity-LED wrapper, if presented without 7xtgv naming and tested
   on a board without `LED`.
3. A clean `hardware/esp32/jc8048w535_sdmmc.h` target adapter, only if it is
   separated from the terminal and 7xtgv display assumptions. Upstream already
   has `hardware/esp32/esp32_s3_lcd.h`, so the PR should explain the distinct
   JC8048W535 SDMMC pins and full target board.

Not suitable for an initial upstream PR: the terminal/history system, private
benchmark/reboot ports, CP/M mirror conventions, task timing policy, QL font,
palette, or 7xtgv display semantics. They are product-specific and would make a
review unnecessarily large.

## Desktop terminal extraction path

The ANSI parser/cell buffer and history/profile model are largely reusable;
`terminal_runtime.cpp` is the coupling layer. A later `src/7xtgv-terminal`
component can expose a byte-stream API and separate:

- portable terminal core: parser, cells, history banks, profiles and persistence;
- render back ends: Arduino_GFX framebuffer and desktop framebuffer/window;
- transports: RunCPM virtual serial, POSIX serial, TCP or stdin/stdout;
- host keyboard adapters: the existing Python bridge and future native desktop
  front ends.

That would let the terminal serve real CP/M hardware without changing its ANSI
or history model. It should be a deliberate refactor after the terminal protocol
and 7xtgv specification stabilise, not a prerequisite for the current firmware.

## Next decisions

- Choose the intended licence for original 7xtgv code/assets.
- Decide whether `src/7xtgv_RunCPM` is retained as a historical reference or
  removed from the active source tree later.
- Prepare the isolated `_sys_extendfile()` upstream patch first, with its test,
  before considering board support.
