# Petr's addendum: scope and economical collaboration

Recorded 2026-09-08. This addendum takes precedence over broader interpretations
of the [port plan](260909-07 runcpm-multiplatform-plan.md).

## User priorities

- Resource use and subscription cost matter more than finishing quickly. Work
  may take several days. Do not turn this plan into a rushing autonomous goal
  or a long exhaustive research/build campaign.
- When Petr can resolve uncertainty cheaply, prefer asking him: visual tests,
  board/button/socket inspection, trying a vendor demo, or web investigation.
  Explain the specific question and what observation/link would resolve it.
- Read existing local source and supplied resources first. Before optional web
  research, ask whether Petr would rather investigate and bring back results.
- Use small checkpoints, focused checks and concise replies. Avoid repeated
  builds, speculative optimizations and unnecessary model/tool/delegation use.
  Ask before substantially expanding a difficult investigation.

## Clarification of "flat HAL"

Petr meant a single target header directly under `hal/`, rather than nested
target folders. He did not request a redesigned portability framework, new
library architecture, or a rewrite of RunCPM's existing abstractions.

Keep RunCPM as-is as far as practical. Reuse its portability mechanisms and
only isolate board-specific differences needed for the new targets. Small
implementation helpers are acceptable when a header alone would be awkward.

The existing virtual serial connection is the intended integration boundary.
Keep RunCPM and terminal/7xtgv independent, with their existing core ownership
and queue semantics. No tighter integration is wanted.

## Assistant notes: Arduino and FreeRTOS

The installed Arduino-ESP32 3.1.0 `cores/esp32/main.cpp` creates `loopTask`
using `xTaskCreateUniversal`; it invokes Arduino `setup()` and `loop()` inside
that task. Thus Arduino on both S3 and P4 normally runs above ESP-IDF FreeRTOS.
Our RP2350 Arduino-Pico profile explicitly selects `os=none`: the existing
`setup1()`/`loop1()` approach does not use FreeRTOS.

Arduino hides most RTOS details for ordinary sketch code. Our two-core port
needs a small explicit adapter for task creation/affinity, stack allocation and
cooperative waiting. The unchanged fast interpreter can run for a long time
without blocking, so idle-task/watchdog scheduling must be checked; merely
calling `taskYIELD()` is not necessarily sufficient for lower-priority tasks.
Preserve watchdog protection and add only the smallest justified cooperation
hook if measurements show it is necessary.

This is a bounded integration concern, not a reason to rewrite the interpreter
around RTOS primitives. Drivers, interrupts and background tasks still share
CPU/memory resources even with one application task pinned to each core.

SD support is likely routine once vendor wiring/examples are checked. Do not
assume absent hardware from an unsuccessful text search through ZIP/PDF files,
or build a custom in-memory filesystem preemptively. SdFat-specific directory
metadata/rename APIs mean switching to Arduino SD_MMC may need a small existing
abstraction-layer adaptation, beyond changing initialization pins.

No firmware changes or hardware tests were performed when saving these notes.

## Follow-up: two Arduino-style tasks are straightforward

Inspection of installed Arduino-ESP32 3.1.0 confirms `loopTask()` calls
`setup()` once, then repeatedly calls `loop()`. `xTaskCreateUniversal()` uses
`xTaskCreatePinnedToCore()` when a valid core is selected on a multicore build.
A small project adapter can therefore call a second setup function once and a
second loop function repeatedly, pinned to the opposite core. Arduino APIs
remain usable there; this does not require a second Arduino initialization or
an independent runtime. Shared peripherals still require single ownership.

Arduino-Pico 5.4.4 provides that convention automatically: weak `setup1()` and
`loop1()` symbols trigger `multicore_launch_core1()` (or its separate-stack
variant); `main1()` invokes the callbacks. Our `core1_separate_stack=true`
selects its separate 8 KiB stack. This is a convenience we can reproduce with
a small ESP32 task wrapper, not a major architectural obstacle.

The scheduling distinction remains: Arduino-ESP32's `yield()` invokes
`vPortYield()`, whereas `delay(ms)` invokes `vTaskDelay()` using the configured
tick period. A yield is not guaranteed to let a lower-priority idle task run,
and a millisecond delay can round to zero ticks. Check the actual RTOS tick and
watchdog configuration before choosing an occasional blocking interval.
Our RunCPM currently runs its command/interpreter loop inside `setup1()`, so
adding a wait only to `loop1()` would not solve compute-only starvation.

Petr's requested order is now explicit: prove headless RunCPM first on S3/LX7
and P4/RISC-V, with storage and serial working, before combining either with
the dual-task TFT terminal. Finish the headless proofs as small checkpoints;
then resume display integration in 3.5-inch, 5-inch, 10-inch order. Keep the
existing virtual serial boundary available for the later integration.
