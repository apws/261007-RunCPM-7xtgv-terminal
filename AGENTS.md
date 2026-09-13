# Repository guidance

- Keep replies compact; use bullets for status and test results.
- Prioritize low resource use over speed; use small checkpoints and ask Petr
  for useful visual/hardware checks or optional web investigation. For RunCPM
  ports, read `docs/260908-18 runcpm-port-addendum.md`; "flat HAL" means target headers,
  not a broad refactor.
- Preserve the project layout: source under `src/`, generated output under
  `build/`, documentation under `docs/`, and the CP/M mirror under `cpmdrive/`.
- Prefix CP/M test programs and fixtures with `VT`; keep the unified `VTESTS`
  menu and its independently callable tests.
- Preserve Z80 behavior when changing RunCPM. Measure before keeping emulator
  optimizations, and record relevant results in `docs/`.
- For hardware work, check serial-port ownership first, avoid unnecessary
  reboots, and release the port after testing. ESP32 flashing recreates
  `/dev/ttyACM0`; restore the temporary user ACL afterward with approved sudo.
  Use the shared tmux terminal for concurrent viewing—never open two serial
  readers.
- When Petr asks for device-side transfer, flashing, or direct CP/M testing,
  that authorizes stopping the shared `7xtgv-cpm` tmux terminal. Stop it,
  perform the work, and relaunch `kon.sh` or `kit.sh` without asking again.
  Briefly report that active CP/M program state was lost.
- Petr performs longer interactive/device tests.  After a relevant build,
  provide a short exact test checklist and wait for his report instead of
  consuming time driving the hardware test.  The shared tmux screen may be
  inspected afterward; add an opt-in input/output recording hook when useful
  for diagnosing a reported interaction.
- Before sending automated commands through the shared CP/M terminal, select
  terminal profile/history bank `#9`; use explicit `## RAW` on/off controls for
  transfers or exact-byte tests. Keep the user's active application history in
  banks `#0` through `#8` untouched.
- Before starting a nonessential web search or broad external research, tell the
  user briefly what is needed and ask whether to search here or let them handle
  it in ChatGPT/Lyra and bring the result back. Proceed immediately when the
  user explicitly requests browsing or current external information.
- At the start of a substantial phase, remind Petr of his preferred `/model`
  choice: SOL for plan analysis/design, TERRA for implementation, and LUNA for
  quick testing.
- At the very end of every final reply, and at the end of every escalation or
  permission question, print the currently selected model and reasoning level
  in exactly one final label, for example `Model: GPT-5.6-SOL-high`. Use the exact exposed
  family/variant/effort (`SOL`, `TERRA`, or `LUNA`, and `low`/`medium`/`high`
  when available); if only a family is exposed, print that family and append
  `-level-unavailable` rather than guessing. Do not repeat the label elsewhere
  in the same reply.
- Keep this file short. Put detailed history, inventories and experiments in
  linked documentation under `docs/`.
- Petr uses a Czech keyboard layout (`XTGV_BANK_KEYS` in `tools/terminal.conf`)
  and XKB `shift:breaks_caps,caps:none`: Shift clears CapsLock. Do not rely on
  CapsLock state or alter ordinary Czech punctuation/Shift input in terminal
  profiles; diagnose such host-input issues outside the CP/M byte bridge first.
- Name project working notes under `docs/` with their filesystem-time prefix
  `YYMMDD-HH ` so extension sorting also preserves their timeline.  Keep
  stable readmes, recovered material, and vendor resources in their existing
  names.
- Treat `cpmdrive/` as the source mirror for authored CP/M files. Deploy
  one-way with `tools/cpm-transfer/transfer.py put`; use `--verify` only when
  a full device read-back is worthwhile. Never silently import device files;
  use explicit `get` only when Petr requests it.
- Keep CP/M text source lines at 80 columns or fewer; the installed TE editor
  rejects longer lines. Upload text with `--cpm-text` so it receives CR/LF
  records and a trailing Ctrl-Z.
