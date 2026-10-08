# Repository guidance

## Scope and working style

- Keep replies compact; report concrete changes, checks and remaining limits.
  Flag conflicts between a request and the implementation before acting.
- Prefer small checkpoints and focused host tests. Agree the scope of lengthy
  research or reverse engineering first; external analysis can be brought back
  for repository-grounded verification. Do not prescribe named assistants,
  model aliases or a particular developer's workstation configuration.
- Native 7xtgv is the primary objective. The implemented 7xtgv-terminal,
  RunCPM integration and SDL desktop are development/test vehicles; do not
  present planned native capabilities as shipped terminal features.
- Preserve unrelated work. Commit/push, hardware writes and card deployment
  require their own authorization; a successful test does not grant it.
  Suggest a commit checkpoint after a completed feature without blocking work.

## Layout and current evidence

- Active firmware and desktop: `src/RunCPM_7xtgv-terminal/`.
  Historical demos and bring-up tools: `src/legacy/`.
  Generated artifacts: `build/`. Maintained manuals: `guides/`.
  Chronological research/plans/checkpoints: `docs/`. Working card: `cpmcard/`.
- Unless qualified otherwise, `tools/` below means
  `src/RunCPM_7xtgv-terminal/tools/`, not a repository-root directory.
- Start with current guides and code. Search relevant dated documents newest
  first, then follow their PLAN/DONE/CHAT links. Check older filename conventions:
  a suffix is not always an HHMM timestamp. A newer proposal is not evidence of
  implementation; distinguish source, built artifact and hardware acceptance.
  Do not load the entire chronological archive for a narrow question.
- For desktop work, use
  `docs/261006-0935 PLAN Desktop runtime using existing board HALs.adoc`
  together with subsequent DONE records, not that plan in isolation.
- Keep detailed history in linked documents, not this file.

## Architecture and validation

- Preserve Z80 behavior; measure before retaining emulator optimizations.
- Before editing RunCPM-derived CCP, BDOS/BIOS, CPU, disk, console, globals or
  common abstraction, identify the file/seam and obtain direction. A terminal
  or board request alone does not authorize core changes. Inspect the pinned
  recovered baseline and relevant upstream changes, including CP/M 3, before
  porting; do not claim upstream parity from successful CP/M 2.2 tests.
  Follow upstream conventions for portable changes and keep project behavior
  behind narrow searchable hooks. See `guides/technical-reference-guide.adoc` and
  `docs/260928-23 RunCPM core delta and upstream seams.adoc`.
- Keep each Arduino board HAL complete and readable: one uniformly sectioned
  header, ordinary functions and intentional board-local storage adapters.
  Avoid per-feature fragments and code-body macros. For ports, also read
  `docs/260908-18 runcpm-port-addendum.adoc` and
  `docs/261001-1322 HAL challenge and readable board consolidation.adoc`.
- `src/RunCPM_linux/` is a POSIX CP/M comparator used by guest-program tests;
  it does not exercise the terminal parser/renderer. Terminal fixes need host
  terminal tests and/or the real-board SDL desktop, with physical tests where
  needed. Desktop success cannot establish electrical, SPI or panel timing.
- Prefix CP/M tests/fixtures with `VT`; retain the unified `VTESTS` menu and
  independently callable tests. Provide short human checklists for longer
  interactive/physical checks rather than taking over a live session.

## Builds, upload and desktop lifecycle

- Batch firmware edits; run focused host tests, but do not rebuild/stage/flash
  after every change. An explicit `rebuild` authorizes a target-aware build,
  not an upload or guest-file deployment. State when rebuild is still pending.
  Do not build firmware unless requested or needed for an authorized test.
- Use `tools/targets/DISPLAY_ID -- target/` wrappers:
  `rebuild.sh` builds only; `reflash.sh` uploads only; `mirror.sh` attaches.
  They use shared Python tooling; preserve quiet status and adjacent logs.
  Missing prerequisites should be reported; installation uses explicit
  `tools/setup.sh`, not a hidden installer in rebuild/reflash.
- The hardware `rebuild` path uses `tools/reflash.py --build-only`.
  It stages verified output as `build/<project>/<target>/new` with a fresh
  `YYMMDD-HHMM` stamp; compilation does not establish hardware acceptance.
  Keep hardware-proven `current` untouched. Never reuse another target's
  artifacts or promote an ad-hoc build. `/tmp` is transient test space.
- A hardware `reflash` request uploads that target's staged `new`, falling
  back to `current` only if no candidate exists, then restores the visible
  mirror. It authorizes the necessary in-scope port/reset/mirror workflow,
  not rebuilding, promotion, target changes or CP/M file replacement.
  Verify the booted Build/VTINFO when accessible; report unverified boot.
- Stable promotion/archive recovery is separate and explicit. For targets
  supported by the low-level uploader, inspect `--promote-new`, `--current`
  and `--restore` before use: these workflows can upload as well as change
  artifact state. Do not assume those options support RP2350.
  Preserve dated archives; do not mass-rename historical build directories.
- ESP32 upload uses the target guard in `tools/reflash.py`; never bypass a
  reported mismatch. Old/unresponsive firmware needs explicit physical-target
  confirmation before one-shot `--allow-unidentified-target TARGET`.
  Never persist that override or silently retry with it.
- RP2350 BOOTSEL is integrated in `tools/target_reflash.py` through
  `tools/bootsel_upload.py`; the low-level `reflash.py` is not its uploader.
  Use the selected target's UF2 validation and physical chip/disk checks.
  An eligible disk already present does not require Enter; missing devices
  prompt interactively. Multiple/mismatched devices are not a reason to bypass
  checks. ROM presence cannot identify the attached display board: verify the
  intended module, using a known chip mapping or explicit confirmation when
  uncertain. Reconnect the mirror to the same chip, not an arbitrary serial node.
- Guest `A0:REFLASH.COM` emits `CSI >64v`; the mirror may handle it only
  through an explicitly configured target-specific uploader. Ordinary guest
  text is not a host command. Keep unsupported workflows unsupported.
- Desktop shortcuts use `tools/target_desktop.py` and
  `desktop/desktop.py`; never flash an MCU. Rebuild and launch are separate.
  Guest input belongs to the mirror, SDL is output-only. Closing the owning
  mirror or SDL requests session shutdown; guest REBOOT restarts the desktop.
  Do not stop an active session or its card writer without task authorization.

## Hardware access and transfers

- Resolve the selected target/port; never assume `/dev/ttyACM0`.
  Check ownership and use one serial reader. Use the shared tmux mirror for
  concurrent viewing; never open a second reader to diagnose an occupied port.
- A device transfer, flash or direct CP/M test authorizes temporarily stopping
  the shared `7xtgv-cpm` mirror and restoring its visible attachment afterward.
  Report application-state loss if a reset/upload caused it; a mere mirror
  reconnect does not by itself mean CP/M restarted.
- Before every direct `tools/cpm-transfer/transfer.py` operation, stop the
  shared mirror unconditionally. Restore the visible Konsole/Kitty attachment
  (`kon.sh`/`kit.sh`) or explicitly selected current-terminal mirror, including
  after failure. A detached tmux session alone is not a visible GUI restoration.
- Serial nodes may disappear/re-enumerate. Check permissions on the actual
  returned node; use configured group access, or an approved temporary ACL
  only when needed. Never grant privileges or change groups without approval.
  If sandbox discovery reports a missing device, repeat narrowly with required
  execution approval before diagnosing hardware failure.
- If serial/protocol state becomes confused, stop sending diagnostic keys.
  Stop the owner, use one supported reset or request a power-cycle, wait for
  one stable device and attach once. Do not loop across ports or launchers.
- Automated guest commands use profile/history bank #9; preserve banks #0-#8.
  Use explicit RAW controls for exact-byte transfers. VTPUT/VTGET must capture
  the original profile via `CSI >14v`, then restore profile and RAW state in
  cleanup without executing `on_select` or changing DU. The transfer Link
  implements this; report restoration failures.
- Send agent-authored CP/M commands in uppercase for attribution, except
  deliberate lowercase-only interpreters such as BBCLOWER. Preserve configured
  keyboard layout, punctuation, modifiers and CapsLock behavior; do not impose
  a workstation-specific layout or assume host key behavior is a firmware bug.

## Working card and guest sources

- `cpmcard/` is canonical, with RunCPM drive A-P/user 0-F directories
  (e.g. `A/0`, `A/F`); no descriptive-folder fallback. The complete tree is
  eligible for versioning, including INI/BUF, databases, backups, host snapshots
  and the existing image. No per-file seed manifest or content exclusions.
  Review runtime changes before commit/pull; inclusion policy does not authorize
  publishing each new runtime change or overwriting physical-card data.
  See `docs/261007-1645 DONE Version complete cpmcard without content exclusions.adoc`.
- Tests copy fixtures to disposable cards. Installing rebuilt guest binaries
  requires explicit `--install`. Deploy only authorized files with transfer
  `put`; do not silently import device files. Transfer `reload` means repeat
  the last identified transfer/destination, not build/reflash/reboot.
  Routine successful transfers need no full read-back or extra serial probes;
  verify further on request or concrete failure, and restore the visible mirror.
- For a device INI edit, fetch the current A0:7XTGVTRM.INI first using
  `tools/fetch-7xtgvtrm-ini.sh`, with explicit target/port when needed.
  The requested device-edit workflow authorizes that fetch, not an unrequested
  upload. Stop if fetch fails. Preserve unrelated settings.
  Host snapshots under `cpmcard/.7xtgvtrm-ini-history/MMDDHHMM.INI` are versioned
  tooling data and must never be deployed to CP/M.
- Never overwrite device INI or BUF without explicit approval for that file;
  approval for one is not approval for the other. A desktop/local-card-only
  edit does not require fetching an unrelated physical device.
- Maintained guest text uses CRLF and trailing Ctrl-Z, with source lines at
  most 80 columns for TE. Do not infer text from extension: tokenized BASIC,
  binaries, native history and backups must remain byte-exact. Host metadata
  retains host formatting. No extra 128-byte padding or implicit transfer
  conversion; `--cpm-text` is only for deliberate external text imports.
- Save maintained MBASIC source with `SAVE "NAME",A`, not tokenized SAVE.
  Respect requested device sources rather than substituting old fixtures;
  bytes after first Ctrl-Z in ASCII are record tail. BBC BASIC/BBCLOWER export
  via `*SPOOL` and LIST/list, then import via NEW/new and `*EXEC`.
  See `docs/260924-17 BASIC ASCII sources and ink test labels.adoc`.
- Use .ASM for project Intel 8080/DR ASM sources and .Z80 for SLR Z80ASM
  sources; do not feed Z80 mnemonics to ASM. Preserve vendor dialects/names.
  ASM produces HEX/PRN for LOAD; SLR Z80ASM normally produces COM.
  Save registers needed across BDOS/BIOS calls; inspect the local implementation
  rather than assuming preservation (`_Bdos()` modifies HL and C).

## Documentation and privacy

- Maintained guides describe current behavior; chronological records preserve
  their dated evidence. Keep imported discussions/plans substantively intact;
  record later decisions separately unless consolidation is requested.
- Use AsciiDoc for new dated records: `YYMMDD-HHMM CHAT|PLAN|DONE Title.adoc`,
  level-zero title and `:source-highlighter: highlight.js`. Format project
  dates as YYMMDD; preserve old filenames. See `docs/README.adoc`.
  Chronological images go in `docs/images/` with dated names; guide assets
  belong alongside their guides. Render new/structurally changed documents.
- Track nontrivial ideas/status in ASCII `docs/features.tsv`; never hand-edit
  generated ledger tables. Run `python3 src/feature_registry.py check` and
  `python3 src/feature_registry.py update-ledger`. Also update the curated
  `guides/features_timeline.adoc` for implemented features/behavior changes.
  Leave unknown milestone dates empty.
- Preserve a concise dated AsciiDoc handoff before intentional context
  compaction during active work: status, evidence, pending work and exact
  recovery/build references.
- Outside `cpmcard/`, uppercase MIXWORX filenames remain local/private under
  .gitignore; never force-add or publish them. Keep FAND test observations in
  the private MIXWORX-tagged log under `guides/`.
