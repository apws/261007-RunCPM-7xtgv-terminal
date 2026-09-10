# ESP32-S3 RunCPM and terminal integration

The JC8048W535 profile uses two explicitly pinned application tasks:

- core 0: RunCPM, Z80 execution and SD_MMC ownership
- core 1: Arduino `setup()`/`loop()`, terminal parsing, TFT rendering and USB

The existing bounded byte queues remain the console boundary. Neither task
accesses the other task's peripheral. Affinity locks each application task to
one core; it does not reserve that entire core. ESP-IDF system tasks and
interrupts may briefly preempt it, and a core can run other ready tasks while
its application task is blocked. FreeRTOS cannot migrate either pinned
application task to the other core.

The startup no longer runs a destructive Z80 estimate before CP/M. Instead,
`VTCPU.COM` is a normal CP/M command which runs the same fixed instruction loop
in the live interpreter. Its active pass measures the normal two-task system.
Its quiet pass asks the terminal task to acknowledge a pause, measures the Z80
loop, then waits for the terminal task to acknowledge its resume. CP/M output
stays in the 16 KiB queue during the pause. Normal CP/M execution always keeps
both tasks concurrent.

The guest command uses private I/O port `7Bh` to reach a small native timing
service. The service only takes timestamps, controls the terminal handshake and
formats the result; all measured instructions still execute in the Z80
interpreter. Calling the old `Z80estimateClock()` recursively from a running
guest would reset the emulated CPU and RAM, so it is intentionally avoided.

## Arduino and task creation

Arduino-ESP32 3.1.0 starts `loopTask` from `app_main()` with priority 1 and the
core selected by `ARDUINO_RUNNING_CORE`. The project FQBN explicitly sets
`LoopCore=1`; therefore `setup()`, `loop()`, terminal rendering and native USB
run on core 1. This is one Arduino runtime with an additional ordinary
FreeRTOS task, rather than two independently initialized Arduino runtimes.

After display initialization, `setup()` calls `xTaskCreatePinnedToCore()` for
RunCPM with priority 1, core ID 0 and a 16 KiB stack. ESP-IDF specifies this
stack argument in bytes, unlike upstream FreeRTOS ports that commonly express
it in words. The initial 16 KiB value is conservative; its high-water mark is
still to be measured on hardware before reducing it.

Both application tasks have priority 1. Because they have disjoint affinity,
they normally run concurrently. Higher-priority ESP-IDF driver and USB tasks
can preempt them. A blocked virtual-console read uses `delay(1)`, which becomes
a one-tick FreeRTOS delay rather than a busy wait.

## Display and memory

The terminal uses the panel's native 6x10 cell size for an 80x32 screen. Its
tested landscape setup keeps the AXS15231B controller at native 320x480 and
rotates an Arduino_GFX canvas to 480x320. Dirty cells are composed in PSRAM and
the full canvas is flushed at no more than about 30 frames per second. Direct
controller landscape output remains a later display-driver optimization.

The ESP32-S3 has 512 KiB of on-chip SRAM, but it is divided among executable
IRAM, data RAM, ESP-IDF/system use and heap; the Arduino linker reports a
320 KiB ceiling specifically for static sketch data. This build occupies
141,448 bytes of that region. The selected SDK also configures a 16 KiB
instruction cache and a 32 KiB data cache. External flash and PSRAM accesses
pass through these caches, so PSRAM is larger but not equivalent to direct
on-chip SRAM.

The RGB565 canvas is 480 x 320 x 2 = 307,200 bytes. The selected OPI-PSRAM SDK
configuration exposes external RAM to ordinary `malloc()`. For allocations
larger than 4 KiB, the allocator prefers PSRAM when the requested capabilities
allow it; this is a preference with fallback, not a rule that every large
allocation must be external. Thus Arduino_Canvas should place its 307,200-byte
buffer in PSRAM. Static queues and terminal planes remain internal, and normal
FreeRTOS task stacks require internal memory in this configuration.

## Idle task and watchdog

Arduino-ESP32 3.1.0 subscribes the core 0 idle task to the task watchdog.
RunCPM intentionally saturates that core. A small priority-2 service task on
core 0 wakes every 250 ms, suspends the priority-1 RunCPM task for one 1 ms
tick, then resumes it. This guarantees an idle window without changing the Z80
instruction loop. The service task sleeps at all other times.

The relevant SDK settings are `CONFIG_FREERTOS_HZ=1000`,
`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=1`,
`CONFIG_SPIRAM_USE_MALLOC=1`, and
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`. Arduino's own `loopTask` watchdog
subscription starts disabled. A plain `taskYIELD()` would allow equal-priority
tasks but would not guarantee time for the lower-priority idle task.

An instruction-count delay was tested before selecting this policy. A static
counter reduced the startup estimate to 10 MHz (1,324 ms); a stack-local
countdown still produced only 11 MHz (1,274 ms). Both touched the hottest path
on every guest opcode and were rejected. Arduino's `disableCore0WDT()` was also
tested, but this core version's idle hook continued calling
`esp_task_wdt_reset()` after removal and flooded native USB with “task not
found”; that policy was rejected. The periodic scheduler window leaves the Z80
interpreter and watchdog subscription intact. A later application-level RunCPM
heartbeat could provide more specific liveness monitoring.

## Current validation

- S3 integrated build: pass, 566,000 bytes flash and 141,448 bytes static RAM
- RP2350 regression build: pass
- host terminal, keyboard, bounds/fuzz, queue and Python tests: pass
- S3 display, CP/M boot, USB input and sustained compute test: pass on hardware
- `VTCPU.COM` before IRAM: 10.79 MHz active, 17.02 MHz quiet
- `VTCPU.COM` with `Z80run()` in IRAM: 17.21 MHz active and 17.21 MHz quiet

The 6 September headless milestone reported about 19 MHz. The quiet integrated
result recovers most of that rate without resetting CP/M. The remaining gap can
include the FreeRTOS idle window, system interrupts and shared external-memory
traffic.

Both LX7 cores access the S3's common external-memory cache subsystem through
separate core buses. This build selects a 16 KiB, 8-way, 32-byte-line
instruction cache and a 32 KiB, 8-way, 32-byte-line data cache. Task affinity
therefore cannot prevent terminal code from displacing interpreter instructions
or framebuffer traffic from competing for the flash/PSRAM path.

The S3 profile now marks only `Z80run()` with `IRAM_ATTR`. Its linked IRAM body
is `0x7082` bytes (28,802 bytes), and current total IRAM use is `0x18DEB`
(101,867 bytes). Static internal data SRAM is 141,448 bytes. The linker calls
this region `.dram0`; in project prose, “DRAM”
is reserved for actual dynamic-memory technology such as PSRAM.
Three before/after runs were stable: active execution improved from 10.79 to
17.21 MHz, while quiet execution improved from 17.02 to 17.21 MHz. The equal
active and quiet results show that removing the dispatcher from the shared
flash instruction cache eliminated essentially all terminal-related slowdown
in this benchmark. The attribute is S3-specific; the RP2350 build remains
unchanged and compiles successfully.

The native BIOS/BDOS dispatchers remain in cached flash. Their measured size,
call-frequency implications and the shared policy for a future VMEX port are
recorded in [Interpreter porting and internal-SRAM policy](260909-13 interpreter-porting-memory.md).

### Global `-O3` trial

A compile-only `-O3` variant was flashed and tested on 2026-09-09. It passed
CP/M boot, SD mounting, `VTCPU`, `VTCPU8X`, `VTDRV8X` verification and the full
automatic `VTESTS.BAS` sequence. Compared with the current global `-Os` build:

| Test | `-Os` | global `-O3` |
| --- | ---: | ---: |
| `VTCPU` active | 17.30 MHz | 19.30 MHz |
| `VTCPU` quiet | 17.31 MHz | 19.34 MHz |
| `VTCPU8X` active/quiet | 0.65 / 0.66 Mloops/s | 0.76 / 0.76 Mloops/s |
| `VTDRV8X` write (A0) | 7.52 KiB/s (`-Os` control) | 7.32 KiB/s |
| `VTDRV8X` read (A0) | 12.65 KiB/s (`-Os` control) | 12.45 KiB/s |

Global `-O3` therefore improves interpreted compute by about 11.6%. The
earlier larger storage difference came from comparing A6 with A0; standardized
A0 measurements show only a small change. Global `-O2` reached 19.38 MHz and
7.46/12.61 KiB/s, while the targeted `Z80run()`-only O2 build reached
19.01 MHz and 7.53/12.64 KiB/s. A separate experiment moved `_Bios()` and
`_Bdos()` into IRAM; it reached 19.03/19.02 MHz and 7.55/12.74 KiB/s, so the
extra roughly 3 KiB of IRAM was removed again.

All drive comparisons above were rerun from A0. A6 remains useful for testing
its own directory layout, but its results must not be mixed into the A0 control
series.

Build with:

```sh
arduino-cli compile --profile jc8048w535 \
  --output-dir ../../build/build-vt100-runcpm-s3-dual .
```

Uploading recreates `/dev/ttyACM0` and removes its temporary per-user ACL. The
working order is: close the serial session, upload, run
`sudo setfacl -m u:"$USER":rw /dev/ttyACM0`, then start the shared terminal.
Never run the transfer/test helper and a separate picocom/terminal reader at
the same time; two readers split the byte stream instead of both receiving a
copy.
