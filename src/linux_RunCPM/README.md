# Headless Linux RunCPM for 7xtgv

This is a native POSIX entry point for the **current** RunCPM CPU, BDOS, disk,
and internal CCP headers in `../7xtgv-vt100-runcpm/`. It does not compile the
Arduino sketch, terminal renderer, FreeRTOS tasks, or Arduino SD/SDMMC driver.
It is useful for CP/M application behavior and repeatable filesystem tests;
its disk timings do **not** predict microSD performance on the display boards.

`main.c` and `abstraction_posix.h` began as the [official RunCPM POSIX files](https://github.com/MockbaTheBorg/RunCPM)
at commit `79396be665d6ec263a2728b20b177f4149647ed8` (MIT; see
[`LICENSE.RunCPM`](../7xtgv-vt100-runcpm/LICENSE.RunCPM)). Local changes are
the one-time CCP banner, optional file
operation tracing (`-DXTGV_POSIX_TRACE_FILES`), and the 7xtgv `VTDRV` timer on
private guest I/O port `7Ah`. The POSIX filesystem implementation remains
distinct from our Arduino `abstraction_arduino.h`.

From the repository root:

```sh
make -C src/linux_RunCPM smoke
make -C src/linux_RunCPM run
```

For the writable `card-work` copy, launch an ordinary Konsole ANSI session:

```sh
make -C src/linux_RunCPM
src/linux_RunCPM/tools/kon.sh
```

`tools/run.sh` runs the same copy in an existing terminal. Both launchers use
only `build/linux_RunCPM/card-work`, never `card-snapshot-*` or the mounted
device card. The Konsole launcher installs its own 80x24 profile with stock
keyboard mappings and mouse reporting/alternate scrolling disabled. It does
not reuse the device mirror's custom key table or connect to USB serial.
The profile controls initial window size, not a CP/M application's own screen
geometry; adjust it if a particular program is configured for 132 columns.

The binary and default writable CP/M directory tree live under
`build/linux_RunCPM/`. `run` creates `data/A/0` if needed and runs with `data/`
as its working directory; `EXIT` leaves RunCPM normally. To use a separate
file-tree copy, set `RUN_ROOT=/absolute/path/to/private/copy` on `make run`.
Never point it at a live card mount or `cpmdrive/`: CP/M programs can create,
overwrite, rename, and delete host files. A card snapshot should be copied
read-only while the device is off, then cloned again into a writable test tree.
The host folders must use the usual `A/0`, `A/1`, etc. layout and uppercase
CP/M filenames on Linux. Use `EXIT` to restore normal terminal input; if a
process is forcibly killed, `stty sane` repairs the host terminal.

This POSIX build shares the current Z80, BDOS and CCP core, but uses host
filesystem I/O and Konsole's terminal parser instead of Arduino storage and
the 7xtgv TFT terminal. Consequently its speed and disk timings differ, and
`7XTGVTRM.INI`, its profiles/history, zoom/pan/backlight, custom keys and
private graphics CSI commands do not apply. Standard ANSI/VT100 applications
are the useful overlap; VT52/ADM-3A-only applications and non-ASCII CP/M text
may need their own terminal configuration or later host-side adapters.

`make smoke` uses a fresh temporary `A/0` tree, independent of the default
interactive data and the device. It runs `DIR`, `VTDRV RW`, a separate
read-only `VTDRV`, and `EXIT`, checking that the 32 KiB test file survives and
is unchanged by the read-only pass. `tests/VTDRV.COM` was assembled on 260919
from the current `tools/vtbench/VTDRV.Z80` using the card's SLR `Z80ASM.COM`;
the older `tools/vtbench/VTDRV.COM` does not match that source and is not used.

For later storage optimizations, this build exercises the shared CP/M core
but **not** the Arduino `File` adapter. Exact write failure and cache tests
still need a host adapter for that code, followed by a device benchmark.

## Terminal code boundary

The files are physically together today, but the core split is already useful:
`../7xtgv-vt100-runcpm/src/terminal/terminal.cpp` and
`history_profiles.cpp` are host-buildable C++ models, while the adjacent
`terminal_runtime.cpp` owns the Arduino_GFX drawing, queues, USB input,
FreeRTOS coordination, and display-specific state. An SDL2 desktop terminal
could reuse the former. A substitute Arduino_GFX surface alone would not port
the latter's input, scheduling, and persistence behavior, so that should be a
separate incremental project, not part of this headless RunCPM port.
