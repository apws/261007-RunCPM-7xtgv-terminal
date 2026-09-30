# Repository guidance

- Keep replies compact; use bullets for status and test results.
- If Petr's request or assumption conflicts with the current code, configuration,
  or supported behavior, flag the specific mismatch immediately, before editing
  or executing it. Explain briefly; do not silently reinterpret the request.
- Prioritize low resource use over speed; use small checkpoints and ask Petr
  for useful visual/hardware checks or optional web investigation. For RunCPM
  ports, read `docs/260908-18 runcpm-port-addendum.adoc`; "flat HAL" means target headers,
  not a broad refactor.
- Preserve the project layout: source under `src/`, generated output under
  `build/`, documentation under `docs/`, and the CP/M mirror under `cpmdrive/`.
- Prefix CP/M test programs and fixtures with `VT`; keep the unified `VTESTS`
  menu and its independently callable tests.
- Preserve Z80 behavior when changing RunCPM. Measure before keeping emulator
  optimizations, and record relevant results in `docs/`.
- Before editing any RunCPM-derived core file (CCP, BDOS/BIOS, CPU, disk,
  console, globals or common abstraction), tell Petr which file and seam need
  changing and why; ask for direction before making that core edit. Do not
  silently treat a terminal or board task as permission to extend the core.
  First inspect the pinned recovered source and current Mockba upstream,
  including CP/M 3 changes, and follow the upstream file's coding and naming
  conventions where a portable change is intended. Keep 7xtgv-specific
  behavior behind narrow, documented hooks when practical. The audit and
  open questions are in `docs/260928-23 RunCPM core delta and upstream seams.adoc`.
- Native `src/RunCPM_linux` does not run the 7xtgv terminal/parser/renderer.
  Use its ordinary ANSI terminal for application-output comparisons, not as
  proof that 7xtgv display behavior works; verify terminal fixes in the host
  terminal tests and, when needed, on the real device.
- For hardware work, check serial-port ownership first, avoid unnecessary
  reboots, and release the port after testing. ESP32 flashing recreates
  `/dev/ttyACM0`; restore the temporary user ACL afterward with approved sudo.
  Use the shared tmux terminal for concurrent viewing—never open two serial
  readers.
- If a flash, transfer, or serial session becomes confused (stale protocol
  text, disappearing/re-enumerating port, or no mirror), do not keep sending
  diagnostic/recovery keystrokes into CP/M. Stop the tmux owner, perform one
  simple supported target reset (DTR/RTS where available), or ask Petr for a
  power-cycle when it is not. Wait for one stable serial node, then start one
  clean mirror attachment. Do not loop across ports or launchers; a clean boot
  is preferable to preserving a potentially contaminated session.
- When Petr asks for device-side transfer, flashing, or direct CP/M testing,
  that authorizes stopping the shared `7xtgv-cpm` tmux terminal. Stop it,
  perform the work, and relaunch `kon.sh` or `kit.sh` without asking again.
  A relaunch is complete only after the user-visible Konsole/Kitty attachment
  is started; do not leave only a detached tmux serial bridge.
  Briefly report that active CP/M program state was lost.
- Petr's chat instruction `reflash` means upload the connected target's staged
  `new` candidate, falling back to `current` if none exists, then restore the
  user-visible mirror; do not treat it as build-only preparation. A literal
  `reflash` typed inside CP/M is not a host instruction unless an explicit
  future opt-in command-monitor is enabled.
  This chat instruction authorizes the complete in-scope device and mirror
  workflow without additional conversational permission questions: check port
  ownership, stop the shared mirror, use the configured target uploader, handle
  its reset and any required serial-port ACL, relaunch Konsole/Kitty, and verify
  the booted build. Obtain any execution-environment approval still required
  for privileged or outside-sandbox commands. `reflash` alone does not authorize
  a rebuild, image promotion, target change, CP/M file overwrite, or unrelated
  device tests.
  For a registered RP2350 already in manual BOOTSEL, absence of its normal
  serial port is expected. Match the BOOTSEL disk's unique chip ID to the
  recorded target, check its RP2350 UF2 identity, mount it, stop the mirror
  once, copy that target's staged `new` UF2, wait for the same chip ID to
  reappear as serial, restore the port ACL if needed, and relaunch the visible
  mirror. Treat this as one `reflash` workflow; ask only for required system
  approval, an unrecognized/mismatched chip, or a real failure.
  The opt-in monitor protocol is now reserved as guest output `CSI > 64 v`
  (`ESC[>64v`), emitted by `A0:REFLASH.COM`; it must only invoke an explicitly
  configured target-specific host uploader. The common host dispatcher is
  `src/RunCPM_7xtgv-terminal/tools/reflash.py`; select its explicit
  `XTGV_TARGET` in `tools/terminal.conf` rather than hard-coding an uploader.
- Before **every** direct `tools/cpm-transfer/transfer.py` operation, first
  stop the shared `7xtgv-cpm` tmux mirror unconditionally, then perform the
  transfer, and always relaunch `tools/kon.sh` or `tools/kit.sh` afterwards.
  Do not first attempt a transfer against the live port merely to discover its
  lock owner.
- Treat the physical USB-serial device as outside-sandbox state: the sandbox
  may hide `/dev/ttyUSB*` and `/dev/ttyACM*` even while the device is healthy.
  If a normal serial-node check or transfer says the configured port is
  absent, repeat that narrowly scoped discovery/transfer with escalation
  before diagnosing the cable or device. This does not itself require sudo.
  Every device flash or direct serial operation must finish by relaunching the
  user-visible `kon.sh`/`kit.sh` mirror, even if the upload itself succeeded.
- Keep target firmware artifacts under `build/<project>/<target>/current` only
  after a verified host build. Archive replaced stable artifacts alongside it
  as `build/<project>/<target>/YYMMDD-HHMM <note>`; do not mass-rename
  historical experiment folders merely to impose this newer chronology. A
  deliberate host build first stages and verifies its own target artifact as
  `new`, then uploads that candidate for hardware test while leaving `current`
  untouched. Device-side `REFLASH` normally uploads `new`, falling back to
  `current` only if no candidate exists. Promote a confirmed candidate with
  `tools/reflash.py --promote-new`; recover explicitly with `--current` or by
  promoting an archive with `--restore '<archive>'` (which quarantines `new`).
  `/tmp` build paths are transient validation space, never the intended
  deployed state. Never let one target reuse another target's build directory
  or manually promote an ad-hoc Arduino build: use the target-aware reflash
  path so HAL build properties are present.
- Every target-aware firmware rebuild must inject a fresh `YYMMDD-HHMM` boot
  stamp (as `tools/reflash.py` now does). Never treat the sketch's cached
  `__DATE__`/`__TIME__` as proof of which image was flashed; check the staged
  artifact and uploader log when diagnosing a device-side `REFLASH`.
- Petr performs longer interactive/device tests.  After a relevant build,
  provide a short exact test checklist and wait for his report instead of
  consuming time driving the hardware test.  The shared tmux screen may be
  inspected afterward; add an opt-in input/output recording hook when useful
  for diagnosing a reported interaction.
- Batch ordinary firmware edits by default: run small host tests when useful,
  but do not compile, replace a target's `new` candidate, archive a candidate,
  or touch hardware after each isolated change. Petr's explicit chat command
  `rebuild` authorizes one target-aware host build/stage for all pending edits;
  use `tools/reflash.py --build-only` so `rebuild` never flashes implicitly.
  When `rebuild` also covers changed CP/M mirror files, deploy those files to
  the device separately and report exactly which were updated; the firmware
  reflash remains an independent user request. `reflash` uploads the already
  staged candidate. State when a
  change remains pending a rebuild. Build immediately only when Petr expressly
  asks for it or a hardware test is needed to make safe progress.
- Use the common `tools/reflash.py` target guard for all supported uploads,
  including device-triggered REFLASH; do not bypass it with the old W535 script
  or direct Arduino CLI. A reported target mismatch is a stop condition. Old or
  nonresponding firmware needs explicit user confirmation of the physical target
  before a one-shot `--allow-unidentified-target TARGET`; never silently retry
  with that option or persist it in the mirror configuration. RP2350 BOOTSEL
  remains a separate manual workflow until integrated into the guarded dispatcher.
- Before sending automated commands through the shared CP/M terminal, select
  terminal profile/history bank `#9`; use explicit `## RAW` on/off controls for
  transfers or exact-byte tests. Keep the user's active application history in
  banks `#0` through `#8` untouched.
- For VTPUT/VTGET, first read the original selected profile via Alt+Space's
  `CSI >14v` notice. Use #9 temporarily, then restore that profile and original
  RAW state in cleanup, including failed transfers. Never leave #9 selected or
  run the restored profile's `on_select` command/change DU as a side effect.
  The shared transfer Link handles this for both mirror and CLI; restoration
  failures must be reported, not silently treated as success.
- Send agent-authored CP/M commands in uppercase. Petr normally types lowercase,
  so uppercase history entries remain visibly attributable even if an automated
  command lands outside reserved bank `#9`; do not discard them merely for being
  agent-generated. Exception: inside `BBCLOWER`, use lowercase commands because
  that deliberately patched interpreter does not accept the uppercase forms.
- Before starting a nonessential web search or broad external research, tell the
  user briefly what is needed and ask whether to search here or let them handle
  it in ChatGPT/Lyra and bring the result back. Proceed immediately when the
  user explicitly requests browsing or current external information.
- For any complex, lengthy, or open-ended investigation—especially binary
  reverse engineering, Ghidra/decompilation work, historical research, or
  broad architecture/design analysis—tell Petr **before beginning** that
  ChatGPT/Lyra is the preferred place to do that analysis, and ask him to
  bring its findings here for repo-grounded verification and implementation.
  Store any resulting chronological project document as AsciiDoc. Do not
  substitute shallow local probes for that deeper
  analysis unless Petr expressly asks to proceed here. If analysis must be
  done here, remind him of `/model`: SOL for analysis/design, TERRA for
  implementation, and LUNA for quick testing. Do not claim a billing or
  infrastructure saving that is not established for his account.
- Keep this file short. Put detailed history, inventories and experiments in
  linked documentation under `docs/`.
- Treat the local repository as the default documentation source of truth;
  commit/push or direct GitHub editing is a separate publishing step and is
  done when Petr requests it or the task explicitly includes it.
- At a completed feature or a switch to unrelated work, flag outstanding
  code/tests/documentation and suggest a small commit checkpoint. Do not let
  reminders block brainstorming; commit and push remain separately authorized.
- Preserve imported/original ChatGPT/Lyra plans as historical source documents.
  A format-only migration to AsciiDoc must preserve their substance. Put later
  repo-grounded discussion and signed implementation decisions in a new
  chronological `YYMMDD-HH ` AsciiDoc design document that links or names its
  source; do not rewrite the earlier plan unless Petr explicitly requests
  consolidation.
- Track non-trivial ideas and delivery state in canonical ASCII
  `docs/features.tsv`. Never edit the marked generated tables in
  `docs/FEATURES.adoc` directly; validate and regenerate them with
  `python3 src/feature_registry.py check` and
  `python3 src/feature_registry.py update-ledger` after changing the TSV. Leave
  unknown milestone dates empty rather than inventing chronology.
- Petr uses a Czech keyboard layout (`XTGV_BANK_KEYS` in `tools/terminal.conf`)
  and XKB `shift:breaks_caps,caps:none`: Shift clears CapsLock. Do not rely on
  CapsLock state or alter ordinary Czech punctuation/Shift input in terminal
  profiles; diagnose such host-input issues outside the CP/M byte bridge first.
- Name project working notes under `docs/` with their filesystem-time prefix
  `YYMMDD-HH ` so extension sorting also preserves their timeline. Canonical
  authored chronological documents use `.adoc`, a level-0 `= Title`, and the
  `:source-highlighter: highlight.js` document attribute. Do not create new
  date-prefixed Markdown or plain-text notes. Put referenced images under
  `docs/images/` and prefix project-owned image filenames with `YYMMDD-HH `;
  a chronological image record is an `.adoc` wrapper that references the
  correspondingly dated image. The generated feature overview is `FEATURES.adoc`.
  Keep stable readmes,
  recovered material, vendor resources, and other non-chronological formats in
  their existing names until a separately approved migration covers them.
- Treat files with uppercase `MIXWORX` in their filename as local/private:
  keep them under the root `.gitignore` rule, and never force-add, commit, or
  push them. Keep all CP/M FAND 1.0 test observations in the `MIXWORX`-tagged
  log under `docs/260912-23 7xtgv terminal manual/`, not in separate test
  reports under `docs/`.
- Before an agent-triggered context compaction during active work, write a
  dated AsciiDoc handoff in `docs/`: current implementation state, relevant
  tests/hardware observations, pending work, and exact recovery/build details.
  Treat it as durable project history, not merely a private chat summary.
- Format every human-facing project date as `YYMMDD` (intentionally no
  century); use `YYMMDD-HH` only where the existing chronological document
  naming convention needs the hour.
- Treat `cpmdrive/` as the source mirror for authored CP/M files. Deploy
  one-way with `tools/cpm-transfer/transfer.py put`. Routine transfers need no
  full read-back (`--verify`) or separate post-transfer serial/tmux/screen probes:
  trust the transfer result and launch the visible mirror, still restoring the
  original profile/RAW state and reporting errors. Petr will report problems.
  In transfer context, `reload` means repeat the last clearly identified file
  transfer/destination, not rebuild/reflash/reboot; ask if its target is unclear.
  Verify further only on request or a concrete transfer failure. Never silently import device files;
  use explicit `get` only when Petr requests it.
- Store guest text in `cpmdrive/` in CP/M format: CRLF line endings and a
  trailing Ctrl-Z. Maintain this format when editing or generating text there;
  ordinary send/receive transfers preserve bytes, without text conversion.
  Never infer text from extension alone: tokenized BASIC, binaries, disk images,
  native checksummed history and historical backups must remain byte-exact.
  Host-only metadata/documentation/build machinery (JSON, README.md, Makefile)
  retain their host format. Do not add 128-byte padding to authored local text.
- Always save maintained MBASIC programs as ASCII with `SAVE "NAME",A`
  (default extension `.BAS`), never ordinary tokenized `SAVE "NAME"`.
  Load normally with `LOAD "NAME"`. Fetch Petr's current device sources when
  requested before editing; do not substitute an older host test fixture.
  Bytes after the first Ctrl-Z in fetched ASCII are CP/M record tail, not source.
  BBC BASIC/BBCLOWER use `*SPOOL NAME.TXT`, `LIST`/`list`, `*SPOOL` to export,
  then `NEW`/`new` and `*EXEC NAME.TXT` to import, not MBASIC's `,A` syntax.
  Clean captured prompt/command lines before maintaining that listing as source;
  preserve keyword case. See `docs/260924-17 BASIC ASCII sources and ink test labels.adoc`.
- `tools/fetch-7xtgvtrm-ini.sh` is the explicit live-config import path. It
  archives the preceding local mirror under the ignored
  `cpmdrive/.7xtgvtrm-ini-history/MMDDHHMM.INI`, then replaces the local
  mirror after a successful `get`; those snapshots are strictly host-local and
  must never be deployed to CP/M.
- Before editing INI for a requested device configuration update, always fetch
  the current device A0:7XTGVTRM.INI first, then edit that fresh local mirror and
  send it back only with transfer approval. This requested workflow authorizes
  the preceding fetch; preserve Petr's unrelated on-device edits. If the fetch
  fails, stop rather than deploying a stale local INI. Pass explicit --target
  and --port to the fetch helper when the attached module differs from host defaults.
- Never overwrite device-side `A0:7XTGVTRM.INI` or `A0:7XTGVTRM.BUF`
  (terminal history) without Petr's explicit approval for that transfer,
  even during a rebuild or bulk CP/M mirror deployment. Approval for one
  file does not authorize overwriting the other.
- Keep CP/M text source lines at 80 columns or fewer; the installed TE editor
  rejects longer lines. Files already in `cpmdrive/` need no upload conversion.
  The CLI `--cpm-text` option is only for deliberate imports of external host
  text; it is not the normal transfer path for the CP/M mirror.
- Keep CP/M assembler dialects explicit. Project-owned Intel 8080 sources for
  Digital Research `ASM` use `.ASM`; project-owned Z80-mnemonic sources for SLR
  `Z80ASM` use `.Z80`. Never assemble a `.Z80` file with `ASM`. With bare CP/M
  filenames, `ASM` defaults to `.ASM` and emits `.HEX`/`.PRN` for `LOAD`, while
  SLR `Z80ASM` defaults to `.Z80` and directly emits `.COM`. Preserve historical
  MAC/M80/vendor filenames and their existing build scripts rather than
  renaming them merely by dialect.
- For new CP/M assembly helpers, save every register whose value remains live
  across BDOS/BIOS calls. Verify the local RunCPM implementation when possible:
  `_Bdos()` in `src/RunCPM_7xtgv-terminal/cpm.h` explicitly resets `HL` on entry
  and also changes `C`. Do not assume a call preserves a parsing pointer or
  loop counter merely because a different CP/M implementation does.
