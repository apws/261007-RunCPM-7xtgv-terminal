# Linux RunCPM POSIX target and Konsole handoff

Recorded 260919. This is the headless desktop test target discussed before
experimenting with RunCPM disk-access buffering. It is not a port of the 7xtgv
TFT terminal.

## What is in place

- `src/linux_RunCPM/` builds a native Linux `RunCPM` using the current project's
  Z80, BDOS, disk and internal CCP headers. Its POSIX entry point and filesystem
  adapter derive from official RunCPM commit
  `79396be665d6ec263a2728b20b177f4149647ed8`; local changes are described
  in [its README](../src/linux_RunCPM/README.md).
- The binary is `build/linux_RunCPM/RunCPM`. `make -C src/linux_RunCPM smoke`
  uses a disposable private filesystem and checks `DIR`, `VTDRV` write/read,
  read-only verification, and `EXIT`.
- Petr prepared `build/linux_RunCPM/card-snapshot-260919/` and a separate
  writable `build/linux_RunCPM/card-work/`. The new
  [Konsole launcher](../src/linux_RunCPM/tools/kon.sh) and
  [current-terminal launcher](../src/linux_RunCPM/tools/run.sh) point only at
  `card-work`. The snapshot and device card are not runtime targets.
- The Konsole launcher installs a separate 80x24 profile with ordinary key
  mappings and mouse tracking/alternate scrolling disabled. It does not use
  the device mirror's 7xtgv key table or serial connection.

From the repository root, build and launch with:

```sh
make -C src/linux_RunCPM
src/linux_RunCPM/tools/kon.sh
```

`EXIT` leaves RunCPM. The separate `make -C src/linux_RunCPM run` target uses
`build/linux_RunCPM/data/` by default, **not** `card-work`.

## Verification and observed baseline

- `sh -n` passed for both launcher scripts, and `git diff --check` passed.
- The native disposable-filesystem smoke test passed. Petr also launched the
  Konsole session and ran `VTDRV` against `card-work` successfully.
- Petr observed an estimated unthrottled Z80 rate of **2843 MHz**. A 32 KiB
  `VTDRV` transfer reported **962 us / 33264.03 KiB/s** for write and
  **680 us / 47058.82 KiB/s** for read.
- Those sub-millisecond, 32 KiB results are dominated by the host environment
  and likely Linux filesystem cache. They do not measure durable storage or
  predict SD-card throughput. The CPU figure is an execution-rate estimate,
  not a physical host CPU clock.
- No device firmware was rebuilt or flashed, and no device-side INI/history
  file was changed for this target. The Linux source tree is not yet committed.

## Boundary and next experiment

Linux uses its POSIX filesystem adapter and Konsole's ANSI parser, not the
Arduino SD adapter or 7xtgv renderer. Consequently `7XTGVTRM.INI`, terminal
profiles/history, zoom, panning, custom keys and private graphics CSI do not
apply. Ordinary ANSI/VT100 CP/M apps are the relevant overlap. App-specific
terminal geometry or VT52/ADM-3A input/output may require separate setup.

Both current filesystem adapters open and close a host file for each 128-byte
CP/M record transfer. A 32 KiB `VTDRV` pass therefore exercises at least 256
such record operations. A useful next host experiment is to count opens,
closes and seeks, repeat a larger transfer for stable before/after timings,
and verify file bytes and failure behavior in disposable trees. An equivalent
POSIX optimization would validate the *policy*, but not the speedup of the
Arduino `File` implementation. The device-side `VTDRV` before/after run remains
the performance verdict for that implementation.
