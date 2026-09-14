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
  A relaunch is complete only after the user-visible Konsole/Kitty attachment
  is started; do not leave only a detached tmux serial bridge.
  Briefly report that active CP/M program state was lost.
- Petr's chat instruction `reflash` means upload the current verified firmware
  for the connected target, then restore the user-visible mirror; do not treat
  it as build-only preparation. A literal `reflash` typed inside CP/M is not a
  host instruction unless an explicit future opt-in command-monitor is enabled.
  The opt-in monitor protocol is now reserved as guest output `CSI > 64 v`
  (`ESC[>64v`), emitted by `A0:REFLASH.COM`; it must only invoke an explicitly
  configured target-specific host uploader. The common host dispatcher is
  `src/7xtgv-vt100-runcpm/tools/reflash.py`; select its explicit
  `XTGV_TARGET` in `tools/terminal.conf` rather than hard-coding an uploader.
- Before **every** direct `tools/cpm-transfer/transfer.py` operation, first
  stop the shared `7xtgv-cpm` tmux mirror unconditionally, then perform the
  transfer, and always relaunch `tools/kon.sh` or `tools/kit.sh` afterwards.
  Do not first attempt a transfer against the live port merely to discover its
  lock owner.
- Petr performs longer interactive/device tests.  After a relevant build,
  provide a short exact test checklist and wait for his report instead of
  consuming time driving the hardware test.  The shared tmux screen may be
  inspected afterward; add an opt-in input/output recording hook when useful
  for diagnosing a reported interaction.
- Before sending automated commands through the shared CP/M terminal, select
  terminal profile/history bank `#9`; use explicit `## RAW` on/off controls for
  transfers or exact-byte tests. Keep the user's active application history in
  banks `#0` through `#8` untouched.
- Send agent-authored CP/M commands in uppercase. Petr normally types lowercase,
  so uppercase history entries remain visibly attributable even if an automated
  command lands outside reserved bank `#9`; do not discard them merely for being
  agent-generated.
- Before starting a nonessential web search or broad external research, tell the
  user briefly what is needed and ask whether to search here or let them handle
  it in ChatGPT/Lyra and bring the result back. Proceed immediately when the
  user explicitly requests browsing or current external information.
- At the start of substantial analysis/planning/design, remind Petr that his
  preferred resource-saving workflow is to complete it in ChatGPT/Lyra and
  bring the resulting Markdown here for repo-grounded implementation. If the
  design must be done here, remind him of `/model`: SOL for analysis/design,
  TERRA for implementation, and LUNA for quick testing. Do not claim a billing
  or infrastructure saving that is not established for his account.
- Keep this file short. Put detailed history, inventories and experiments in
  linked documentation under `docs/`.
- Treat the local repository as the default documentation source of truth;
  commit/push or direct GitHub editing is a separate publishing step and is
  done when Petr requests it or the task explicitly includes it.
- Preserve imported/original ChatGPT/Lyra plans as historical source documents.
  Put later repo-grounded discussion and signed implementation decisions in a
  new chronological `YYMMDD-HH ` design document that links or names its source;
  do not rewrite the earlier plan unless Petr explicitly requests consolidation.
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
- Keep CP/M assembler dialects explicit. Project-owned Intel 8080 sources for
  Digital Research `ASM` use `.ASM`; project-owned Z80-mnemonic sources for SLR
  `Z80ASM` use `.Z80`. Never assemble a `.Z80` file with `ASM`. With bare CP/M
  filenames, `ASM` defaults to `.ASM` and emits `.HEX`/`.PRN` for `LOAD`, while
  SLR `Z80ASM` defaults to `.Z80` and directly emits `.COM`. Preserve historical
  MAC/M80/vendor filenames and their existing build scripts rather than
  renaming them merely by dialect.
