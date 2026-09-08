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
`VTMHZ.COM` is a normal CP/M command which runs the same fixed instruction loop
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
141,432 bytes of that region. The selected SDK also configures a 16 KiB
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

- S3 integrated build: pass, 564,740 bytes flash and 141,440 bytes static RAM
- RP2350 regression build: pass
- host terminal, keyboard, bounds/fuzz, queue and Python tests: pass
- S3 display, CP/M boot, USB input and sustained compute test: pass on hardware
- callable `VTMHZ.COM`: pass on hardware; 10.79 MHz active, 17.02 MHz quiet

The 6 September headless milestone reported about 19 MHz. The quiet integrated
result recovers most of that rate without resetting CP/M. The remaining gap can
include the FreeRTOS idle window, system interrupts and shared external-memory
traffic.

The linked `Z80run()` dispatcher occupies `0x6FAA` bytes (28,586 bytes) and is
currently in flash-mapped `.text`. Both LX7 cores access the S3's common
external-memory cache subsystem through separate core buses. This build selects
a 16 KiB, 8-way, 32-byte-line instruction cache and a 32 KiB, 8-way,
32-byte-line data cache. Therefore task affinity prevents migration but cannot
prevent terminal code from displacing interpreter instructions or framebuffer
traffic from competing for the flash/PSRAM path. A later controlled experiment
may place only `Z80run()` in internal IRAM and compare `VTMHZ`; its 28 KiB size
looks feasible, but memory placement and the RP2350 regression must be checked
before keeping that change.

Build with:

```sh
arduino-cli compile --profile jc8048w535 \
  --output-dir ../../build/build-vt100-runcpm-s3-dual .
```
