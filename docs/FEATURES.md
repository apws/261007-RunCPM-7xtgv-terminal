# 7xtgv feature ledger

Last reviewed: 260922 against commit `e3dc01e` plus the current working tree.

This is the central index of implemented features, signed work, designs and
small ideas.  It does not replace the dated source documents.  Those remain
the historical record of Petr/Lyra consultation, Petr/Ari design discussion,
implementation decisions, tests and later corrections.

## Status codes

| Code | Meaning |
| --- | --- |
| **B** | Brainstorm or useful idea; no settled design. |
| **D** | Designed or investigated; not necessarily approved for implementation. |
| **P** | Signed/approved implementation plan; implementation is still pending. |
| **I** | Implemented in the source tree and covered by host tests. |
| **H** | Implemented and exercised on real target hardware. |
| **X** | Deliberately deferred, rejected, retired or superseded. |
| **R** | Reference, test evidence or operational knowledge rather than a feature. |

`partial` means that the linked document contains a larger idea than the part
already delivered.  Hardware status describes the feature, not firmware-image
promotion: a running `new` candidate may be hardware-tested while deliberately
remaining unpromoted.

Entries use the timestamp of their principal source document.  Older files
without a minute timestamp keep their existing identifier; this ledger does
not invent false chronology.

## Current gate

| Feature | State | Exact position |
| --- | --- | --- |
| `260922-13` key-first INI chord names | **H**, deeper test | Firmware `260922-1515` and converted `A0:7XTGVTRM.INI` boot with `Terminal profile .....OK`; TP301/Turbo editing was smoke-tested.  Candidate remains `new` until Petr requests promotion. |

<!-- feature-registry:begin -->

Generated from `docs/features.tsv` by `src/feature_registry.py`; do not edit these tables directly.

## RunCPM, targets and host tooling

| Introduced | Feature | State | Summary and remaining boundary | Documents |
| --- | --- | --- | --- | --- |
| 260906 | Recovered Arduino/7xtgv workspace and demos | **H** | Recovery workspace; recovered material is evidence, not a normative current design. | [260906-12 RECOVERY.md](260906-12%20RECOVERY.md) |
| 260907 | Z80/RunCPM optimization pass | **X**, until-measured-need | Optimization notes and interpreter memory study. Existing broad guesses are not authorization for intrusive caching. | [260907-23 runcpm-optimization-plan.md](260907-23%20runcpm-optimization-plan.md), [260909-13 interpreter-porting-memory.md](260909-13%20interpreter-porting-memory.md) |
| 260908 | ESP32-S3 headless RunCPM baseline | **H** | Headless validation. | [260908-22 runcpm-s3-headless-validation.md](260908-22%20runcpm-s3-headless-validation.md) |
| 260908 | JC8012P4A1C 10-inch display demo | **H**, terminal-port-pending | Port notes. Display proof exists; complete current terminal/RunCPM integration remains future work. | [260908-09 jc8012p4a1c-runcpm-port-notes.md](260908-09%20jc8012p4a1c-runcpm-port-notes.md) |
| 260908 | JC8048W550 RGB-panel RunCPM target and SD SPI | **H** | Vendor baseline, SD validation, and later hardware checkpoints. | [260908-17 jc8048w550-vendor-test.md](260908-17%20jc8048w550-vendor-test.md), [260914-17 jc8048w550 SD SPI validation.md](260914-17%20jc8048w550%20SD%20SPI%20validation.md) |
| 260908 | RP2350 two-core RunCPM + terminal | **H** | Multiplatform plan, port addendum, and firmware validation records. | [260909-07 runcpm-multiplatform-plan.md](260909-07%20runcpm-multiplatform-plan.md), [260908-18 runcpm-port-addendum.md](260908-18%20runcpm-port-addendum.md) |
| 260909 | ESP32-S3 pinned RunCPM/terminal tasks and IRAM dispatcher | **H** | Dual-task implementation. | [260909-13 runcpm-s3-dual-task.md](260909-13%20runcpm-s3-dual-task.md) |
| 260909 | Selected-file virtual drive/cache | **B** | Interpreter memory study; intentionally not part of ordinary RunCPM semantics yet. | [260909-13 interpreter-porting-memory.md](260909-13%20interpreter-porting-memory.md) |
| 260911 | RunCPM upstream comparison, licensing split and PR candidates | **D** | Upstream/licensing plan. | [260911-19 RunCPM upstream comparison and licensing plan.md](260911-19%20RunCPM%20upstream%20comparison%20and%20licensing%20plan.md) |
| 260912 | JC8048W535 rotated PSRAM framebuffer workaround | **H** | Rotation/framebuffer implementation. | [260912-12 JC8048W535 software rotation and framebuffer implementation.md](260912-12%20JC8048W535%20software%20rotation%20and%20framebuffer%20implementation.md) |
| 260913 | Device-drive hash manifest synchronization | **D** | Manifest option; explicitly not implemented. | [260913-22 RunCPM device-drive hash manifest sync option.md](260913-22%20RunCPM%20device-drive%20hash%20manifest%20sync%20option.md) |
| 260914 | Windows/SDL2 desktop 7xtgv emulator | **D** | Desktop emulator plan: framebuffer shim, deterministic replay, headless hashes, keyboard and static Windows package all remain pending. | [260914-00 7xtgv Windows SDL2 desktop emulator plan.md](260914-00%207xtgv%20Windows%20SDL2%20desktop%20emulator%20plan.md) |
| 260919 | Linux/POSIX RunCPM comparison environment | **H**, as-comparison-tool | Linux RunCPM handoff. It does not execute the 7xtgv terminal/renderer. | [260919-01 Linux RunCPM POSIX target and Konsole handoff.md](260919-01%20Linux%20RunCPM%20POSIX%20target%20and%20Konsole%20handoff.md) |
| 260919 | Terminal/RunCPM/HAL source separation | **I** | Source-boundary audit. Native 7xtgv remains intentionally separate and not yet implemented. | [260919-14 terminal RunCPM source boundaries and rendering flow.md](260919-14%20terminal%20RunCPM%20source%20boundaries%20and%20rendering%20flow.md) |
| - | CP/M interpreted-screen file transfer | **H** | tools/cpm-transfer/transfer.py, VTGET and VTPUT; one-way mirror policy is in AGENTS.md. |  |
| - | Cross-platform desktop packaging through Wine/Windows builds | **B** | A possible later consequence of the SDL2 plan, not a current deliverable. |  |
| - | Internal CCP prompt styles DD, DH, D (CCPP) | **I** | Implemented in src/7xtgv-vt100-runcpm/ccp.h; still a local RunCPM extension and a possible upstream-sized change. |  |
| - | RunCPM SD/FAT open-file cache, read buffer and lazy-write experiments | **D** | Discussed in the interpreter/storage study; require device benchmarks and power-loss tests before retention. |  |
| - | Target-aware new/current build, reflash and recovery workflow | **H** | Defined operationally in AGENTS.md and implemented by tools/reflash.py; promotion remains explicit. |  |
| - | XMODEM interaction from the mirror UI | **I**, limited-hardware-use | Documented in the terminal manual; keep distinct from the interpreted-screen transfer protocol. | [README.md](260912-23%207xtgv%20terminal%20manual/README.md) |

## Terminal output, display and graphics

| Introduced | Feature | State | Summary and remaining boundary | Documents |
| --- | --- | --- | --- | --- |
| 260910 | Full VT100 text-terminal compatibility target | **I**, partial | The original terminal-extension roadmap is broader than the documented current subset. Do not call the terminal fully VT100-certified merely because the implemented applications work. | [260910-01 terminal-extension-plans.md](260910-01%20terminal-extension-plans.md) |
| 260912 | Bird's-eye reduced/summary views below 1x | **D** | The earlier viewport/bird's-eye specification remains a separate later renderer. Current zoom deliberately stops at real 1x glyphs. | [260912-10_7xtgv_terminal_viewport_birdseye_xterm256_spec.md](260912-10_7xtgv_terminal_viewport_birdseye_xterm256_spec.md) |
| 260912 | Generic .X256 image converter/viewer | **D** | CP/M image-demo plan. Basic format, converter and viewer are not implemented. | [260912-20_7xtgv_xterm-256_cpm_image_demo_plan.md](260912-20_7xtgv_xterm-256_cpm_image_demo_plan.md) |
| 260912 | Mandelbrot BROT-BS / BROT-PS compatibility | **H** | Initial plan and GCLS implementation record. | [260912-01_7xtgv_xterm256_brot_plan.md](260912-01_7xtgv_xterm256_brot_plan.md) |
| 260912 | xterm-256 indexed-color plot transaction (GCLS) | **H** | Implementation plan/status; absolute physical origin, zoom, gutter restoration and non-clearing arming behavior were hardware-tested. | [260912-12 7xtgv ANSI xterm256 GCLS implementation plan.md](260912-12%207xtgv%20ANSI%20xterm256%20GCLS%20implementation%20plan.md) |
| 260913 | Backlight HAL, private CSI, keyboard control and persistence | **H** | Original plan and signed plan. | [260913-16 7xtgv backlight brightness control plan.md](260913-16%207xtgv%20backlight%20brightness%20control%20plan.md), [260914-11 7xtgv signed backlight and CSI keyboard implementation plan.md](260914-11%207xtgv%20signed%20backlight%20and%20CSI%20keyboard%20implementation%20plan.md) |
| 260914 | Integer terminal zoom 1x-4x and runtime geometry | **H** | Generic design, signed plan, and hardware corrections. | [260914-23_7xtgv_generic_zoom_in_display_geometry_plan.md](260914-23_7xtgv_generic_zoom_in_display_geometry_plan.md), [260915-01 7xtgv signed zoom-in implementation plan.md](260915-01%207xtgv%20signed%20zoom-in%20implementation%20plan.md), [260915-11 Zoom hardware test corrections.md](260915-11%20Zoom%20hardware%20test%20corrections.md) |
| 260917 | Default grey ink, darker/bold companion inks and pale papers | **H** | Legacy implementation review records the ANSI/VGA comparison and project-specific palette decision. | [260917-10 terminal legacy compatibility implementation review.md](260917-10%20terminal%20legacy%20compatibility%20implementation%20review.md) |
| 260917 | Output modes ANSI and VT52, including runtime DECANM switching | **H** | Signed compatibility review, checkpoint, and device smoke. | [260917-10 terminal legacy compatibility implementation review.md](260917-10%20terminal%20legacy%20compatibility%20implementation%20review.md), [260917-18 terminal legacy compatibility implementation checkpoint.md](260917-18%20terminal%20legacy%20compatibility%20implementation%20checkpoint.md), [260917-21 ES3_5 legacy device smoke and DIR fix.md](260917-21%20ES3_5%20legacy%20device%20smoke%20and%20DIR%20fix.md) |
| 260918 | ANSI insert/delete line (CSI n L/M) | **H** | Signed plan, TP301 diagnosis. TP3's configured Ctrl+L i status-row loss was an application command-prefix collision, not the terminal implementation. | [260918-22 terminal insert-delete lines plan.md](260918-22%20terminal%20insert-delete%20lines%20plan.md), [260919-09 TP301 insert-line prefix diagnosis.md](260919-09%20TP301%20insert-line%20prefix%20diagnosis.md) |
| 260918 | DEC Special Graphics with G0/G1 and SI/SO | **H** | Designed in DEC graphics notes, now implemented and host-tested; exercised by the visual terminal suite. | [260918-13 terminal profiles DEC graphics and native 7xtgv design notes.md](260918-13%20terminal%20profiles%20DEC%20graphics%20and%20native%207xtgv%20design%20notes.md) |
| 260918 | Palette/theme loader (VTPAL, PAPER/green/amber) | **D**, brainstorm | Palette command note and attribute/theme notes. | [260918-17 vtpal_com command to load one of basic 10 pallete sets fromt vtpal_ini file.txt](260918-17%20vtpal_com%20command%20to%20load%20one%20of%20basic%2010%20pallete%20sets%20fromt%20vtpal_ini%20file.txt), [260918-23 TP301 input profiles and ANSI attribute marker notes.md](260918-23%20TP301%20input%20profiles%20and%20ANSI%20attribute%20marker%20notes.md) |
| 260921 | Optional boot_ansi=on\|off startup styling | **D** | Explicitly excluded from the INI diagnostics plan; no implementation exists. | [260921-01 terminal INI parse diagnostics implementation plan.md](260921-01%20terminal%20INI%20parse%20diagnostics%20implementation%20plan.md) |
| - | 256-glyph/codepage output and Czech characters | **D** | Requires a new glyph/codepage design and an explicit printable-0x9B versus C1 policy. Console8 alone does not provide it. |  |
| - | Additional Hazeltine/Soroc/Televideo/etc. decoders | **X** | Explicit non-goal; use declarative application installation or add only evidence-driven personalities. |  |
| - | ADM-3A/Kaypro output decoder | **X**, deferred | Historical design source exists, but the signed review defers it until a concrete application requires it. |  |
| - | ANSI C1 0x9B CSI under Console8 | **H** | Implemented with VT52 address-byte protection; FAND as a whole is not yet accepted. |  |
| - | Buffered ANSI/VT100-style terminal and PC mirror | **H** | Current behavior is summarized in the firmware README and manual. | [README.md](../src/7xtgv-vt100-runcpm/README.md), [README.md](260912-23%207xtgv%20terminal%20manual/README.md) |
| - | Debug-only deterministic RGB screen capture | **B** | Mentioned in the firmware README; must use diagnostic framing rather than CP/M console bytes. |  |
| - | DEC double-width/double-height per-line modes | **X**, brainstorm | Kept separate in the DEC notes; not implemented until a real application justifies the screen-model cost. |  |
| - | Display publish-rate/dirty-region tuning | **D** | Later phases of the zoom/display plan; current correctness-first redraw remains authoritative. |  |
| - | General compact graphics protocol beyond xterm-SGR samples | **B** | GCLS is deliberately a narrow compatibility mechanism, not the future native graphics API. |  |
| - | Optional .X256 RLE, dithering and benchmark mode | **B** | Later phases of the image-demo plan; do not pull into the minimum viewer silently. |  |
| - | Optional CRT scanline visual effect | **B** | Kept independent from semantic terminal attributes and native 7xtgv. |  |
| - | Optional text-only VT340 and WYSE color personalities | **X**, brainstorm | Preserved from the terminal-extension roadmap; require original specifications and a concrete application before implementation. |  |
| - | Per-display and per-profile logical geometry overrides | **H** | Implemented through [display:HAL] and zoomN_terminal; unused physical area remains background rather than gutter. |  |
| - | Persistent zoom setting | **H** | Lazy INI rewrite shares the settings transaction with backlight persistence. |  |
| - | Private backlight query/reply | **B** | Optional future extension from the backlight plan; no query is implemented. |  |
| - | QL 6x10 font and 4-paper/16-ink 7xtgv palette | **H** | Current terminal uses terminal cell/sidecar arrays, not native four-plane 7xtgv memory. |  |
| - | Smooth sub-cell/pixel scrolling | **X**, brainstorm | Kept outside the current cell terminal and native v0.1 format; future compositor/controller work only. |  |
| - | Viewport edge jumps, grey hidden-content gutter and plot-safe redraw | **H** | Hardware-tested during zoom/GCLS work. Automatic cursor-following pan remains deliberately absent. |  |
| - | VT52.COM / VT100.COM transient mode helpers | **H** | Included in the compatibility checkpoint and device smoke. |  |

## Input, profiles, history and smart-terminal work

| Introduced | Feature | State | Summary and remaining boundary | Documents |
| --- | --- | --- | --- | --- |
| 260910 | ## RAW temporary bypass | **H** | RAW-mode design/test. | [260910-15 7xtgv `##` RAW Input Mode.md](260910-15%207xtgv%20%60%23%23%60%20RAW%20Input%20Mode.md) |
| 260910 | MBASIC EDIT n adapter stage A | **H**, partial | MBASIC editing. Local current-line movement is implemented; broader resynchronization remains open. | [260910-13 mbasic-terminal-editing.md](260910-13%20mbasic-terminal-editing.md) |
| 260910 | Per-application syntax coloring over legacy editors | **D** | Multi-application plan. Depends on a stable smart-render boundary; no production highlighter exists. | [260910-17 7xtgv_CPM_terminal_app_profiles_MBASIC_BBCBASIC_TP301_plan.md](260910-17%207xtgv_CPM_terminal_app_profiles_MBASIC_BBCBASIC_TP301_plan.md) |
| 260910 | Ten application/history profiles and explicit selection | **H** | Application profiles; bank #9 remains reserved for automated work. | [260910-01 terminal-application-profiles.md](260910-01%20terminal-application-profiles.md) |
| 260911 | DDT first-class history profile | **H**, partial | Basic profile exists; optional DDT subcontexts from the DDT/history plan do not. | [260911_7xtgv_terminal_new_additions_DDT_history_plan.md](260911_7xtgv_terminal_new_additions_DDT_history_plan.md) |
| 260911 | TP301/TINST full-screen editing profile | **H** | TP reference and later TP301 test records. | [260911-14 Turbo Pascal 3.01 keyboard and terminal installation reference.md](260911-14%20Turbo%20Pascal%203.01%20keyboard%20and%20terminal%20installation%20reference.md) |
| 260915 | BBC BASIC and BBCLOWER profile/history behavior | **H**, partial | BBC handoff and BBCLOWER smoke. Separate .TOK loading is pending. | [260915-19 BBC BASIC and app history handoff.md](260915-19%20BBC%20BASIC%20and%20app%20history%20handoff.md), [260917-23 BBCLOWER terminal smoke.md](260917-23%20BBCLOWER%20terminal%20smoke.md) |
| 260915 | Persistent, checksummed per-bank history | **H** | Current manual/README and history handoff. | [260915-19 BBC BASIC and app history handoff.md](260915-19%20BBC%20BASIC%20and%20app%20history%20handoff.md) |
| 260915 | Separate persistent completion/keyword lists | **D** | History tokens and editing surfaces. Preserve history and completion selection as separate state. | [260915-21 history tokens and editing surfaces.md](260915-21%20history%20tokens%20and%20editing%20surfaces.md) |
| 260917 | Remove numeric-bank behavior coupling | **D** | Coupling audit: editable names are safe, but compiled defaults, CCP bank 0 and a few application transports still depend on numeric identity. | [260917-20 profile bank coupling review note.md](260917-20%20profile%20bank%20coupling%20review%20note.md) |
| 260919 | Historical physical-position key IDs | **I**, design-superseded | The initial sparse-key draft led to the signed scope. Its modifier-first spelling is obsolete, but its XKB/Czech position evidence remains useful. | [260919-19 terminal keyboard physical keys and sparse overrides plan.md](260919-19%20terminal%20keyboard%20physical%20keys%20and%20sparse%20overrides%20plan.md) |
| 260919 | Readable \\^X control-byte notation | **H** | Designed in keymap/control notation and exercised with TP3 mappings. | [260919-13 7xtgv keymap personalities and control notation.md](260919-13%207xtgv%20keymap%20personalities%20and%20control%20notation.md) |
| 260919 | Safe on_select only at a live CCP prompt | **H** | TP301/profile-switching record; in-app switching preserves the current screen line and shows only the reversible label. | [260919-11 TP301 editor TINST and profile switching.md](260919-11%20TP301%20editor%20TINST%20and%20profile%20switching.md) |
| 260920 | Configurable numkeys character-alias fallback | **X**, for-current-konsole | Alias addendum. Exact private physical events solved the current layout; retain the alias idea only for a future host unable to distinguish keys. | [260920-00 terminal numkeys aliases and key-event plan.md](260920-00%20terminal%20numkeys%20aliases%20and%20key-event%20plan.md) |
| 260920 | Full physical-key global/bank overrides | **H** | Signed scope and implementation. | [260920-15 terminal keyboard override signed implementation scope.md](260920-15%20terminal%20keyboard%20override%20signed%20implementation%20scope.md), [260920-16 terminal keyboard override implementation.md](260920-16%20terminal%20keyboard%20override%20implementation.md) |
| 260920 | Main-row digit identity and keypad decimal normalization | **H** | Corrected key plan; Czech layout behavior was verified. | [260920-01 main-row and keypad identity plan.md](260920-01%20main-row%20and%20keypad%20identity%20plan.md) |
| 260922 | Key-first chord names (insert_ctrl_shift_alt) | **H**, deeper-test | Signed plan, implementation. Old modifier-first names are intentionally rejected. | [260922-13 key-first INI chord names signed implementation plan.md](260922-13%20key-first%20INI%20chord%20names%20signed%20implementation%20plan.md), [260922-14 key-first INI chord implementation.md](260922-14%20key-first%20INI%20chord%20implementation.md) |
| 260922 | Private one-byte 0x80..0xFF 7xtgv core-key layout | **D** | TP3 proof and design. Arbitrary high-byte sends work, but the complete default physical-key encoding is not implemented. | [260922-00 7xtgv private 8-bit core key codes and TP3 proof.md](260922-00%207xtgv%20private%208-bit%20core%20key%20codes%20and%20TP3%20proof.md) |
| - | Application-aware overlays/selections/semantic regions | **B** | Later smart-terminal layer; do not encode into guest text or current ANSI parser. |  |
| - | Ctrl/Shift/Alt combinations, F1-F12, special keys and modified letters | **H** | Sparse overrides are implemented; plain and Shift-only letters remain deliberately invalid bindings. |  |
| - | Future local/embedded keyboard scanner | **B** | It should emit logical physical-key events into the same override path; AltGr remains distinct from project Alt. |  |
| - | History work-log browser and metadata | **B** | Keep optional until actual workflows require timestamps/origin/context UI. |  |
| - | Independent mode= output and input= presets | **H** | ANSI, VT52 and WordStar input presets are implemented independently of output language. |  |
| - | Input-origin tagging (human/paste/automation) | **D** | Described in the DDT and application-profile plans; current reserved bank #9 is the simpler operational boundary. |  |
| - | MBASIC external edit-mode recognition/resynchronization | **D** | Later stage in the MBASIC and multi-application plans. |  |
| - | Per-profile local compose/transmit-line mode | **B** | Useful only for confirmed line-oriented programs; not a global behavior. |  |
| - | Pinned/favourite history entries | **B** | Possible lightweight alternative to a macro language. |  |
| - | Prefix-filtered history search | **B** | Attractive optional Termite-style behavior; not implemented. |  |
| - | Profile count expansion beyond 0..9 | **B** | a..z was considered; RAM, INI and UI implications require a separate decision. |  |
| - | Screen-line tracking and filtered history-token completion | **H**, transitional | Existing history can supply filtered tokens in line/screen editors. It is intentionally not the final keyword-store design. |  |
| - | Separate typing delay and paste pacing | **B** | Recorded in the DDT/history plan; no generic end-to-end paste protocol exists. |  |
| - | Several explicitly switchable completion lists per profile | **D** | Refined in the TP301/profile-switching record; broader than a single history/keywords toggle. |  |
| - | XITU-T/custom keyboard mapping | **B** | Mentioned in the signed backlight/key plan; requires actual hardware/protocol before implementation. |  |

## Configuration, diagnostics and operational safety

| Introduced | Feature | State | Summary and remaining boundary | Documents |
| --- | --- | --- | --- | --- |
| 260910 | Editable A0:7XTGVTRM.INI with compiled fallbacks | **H** | Application profiles and the terminal manual. | [260910-01 terminal-application-profiles.md](260910-01%20terminal-application-profiles.md) |
| 260921 | INI parse error count, first ten source lines and .ERR file | **H** | Signed diagnostics plan; confirmed clean on the key-first device boot. | [260921-01 terminal INI parse diagnostics implementation plan.md](260921-01%20terminal%20INI%20parse%20diagnostics%20implementation%20plan.md) |
| - | Device-side config fetch with timestamped local backup | **H** | tools/fetch-7xtgvtrm-ini.sh; backups are host-local and never deployed. |  |
| - | Lazy atomic persistence of backlight and zoom | **H** | Saved through the SD-owning core; SYNC/reboot/reflash waits for settings and history. |  |
| - | Mirror reconnect and host mouse-mode sanitation | **H** | Mouse tracking and alternate-scroll modes are reset; only recognized mouse packets are defensively filtered, not generic CSI keyboard input. |  |
| - | Raw terminal output capture/replay for application diagnosis | **D** | Useful for FAND and terminal disputes; a generalized replay harness is not yet present. |  |
| - | Split global/profile/history/keyword files | **B** | Discussed after TE's line limit. No file-layout decision is signed; separate keyword storage is the only settled direction. |  |
| - | Staged VTCONF.COM validator/editor using .NEW | **B** | Possible later convenience; simple syntax plus useful line reporting remains preferred first. |  |

## Native 7xtgv architecture

| Introduced | Feature | State | Summary and remaining boundary | Documents |
| --- | --- | --- | --- | --- |
| 260906 | Original tiled-GUI/video-256 concept | **B**, reference | Early 7xtgv brainstorm. Later native specifications, not this raw note, govern implementation. | [260906-21 250524 !!!! mixworx 7xtgv - TILED-GUI-VIDEO-256.txt](260906-21%20250524%20%21%21%21%21%20mixworx%207xtgv%20-%20TILED-GUI-VIDEO-256.txt) |
| 260910 | Native SEMI 2x5 subcell mode | **D** | SEMI/image plan; both overlay and indexed-four-color variants remain unimplemented. | [260910-13 7xtgv native SEMI mode and image demo.md](260910-13%207xtgv%20native%20SEMI%20mode%20and%20image%20demo.md) |
| 260910 | Native v0.1 four-plane TEXT/WIND/INST/SEMI specification | **D** | Native specification. It is not the current terminal storage model. | [260910-13 7xtgv v0.1 native specification and implementation plan.md](260910-13%207xtgv%20v0.1%20native%20specification%20and%20implementation%20plan.md) |
| - | ADAPTIVE1024 palette clustering, iteration and dithering | **B** | Advanced optional path only after SAFE14 produces deterministic reference output. |  |
| - | Fade/alpha-to-desktop | **B** | Explicitly separate from backlight brightness and v0.1 core semantics. |  |
| - | HDL/CPLD/FPGA realization | **D**, brainstorm | Native formats are HDL-friendly, but present work is a software reference over Arduino_GFX. |  |
| - | Native 256-glyph font | **D** | Must not silently promote the legacy QL font into the canonical native set. |  |
| - | Native hard desktop/window compositor | **B** | Multiple clipped windows and flattening are future software above cell RAM. |  |
| - | Native INST mode | **D** | Specified as an instruction/glyph class; no complete renderer. |  |
| - | Native software reference renderer over Arduino_GFX | **P**, design | First implement/test independently, then adapt the terminal; do not mutate the spec around terminal shortcuts. |  |
| - | Native TEXT mode | **D** | Character/ATTR/EXTA behavior specified, not implemented as the terminal backend. |  |
| - | Native WIND mode and window glyph classes | **D** | Specified, including HDL-friendly index behavior; no complete renderer. |  |
| - | SAFE14 reference image converter | **D** | Normative/simple converter strategy in the native spec; pending. |  |

## Development tools and process features

| Introduced | Feature | State | Summary and remaining boundary | Documents |
| --- | --- | --- | --- | --- |
| 260922 | Central Markdown feature ledger | **I** | Human ledger established; its generated feature tables now come from canonical TSV while narrative sections remain hand-authored. | [260922-16 structured feature registry design.md](260922-16%20structured%20feature%20registry%20design.md) |
| 260922 | Feature-registry validation and Markdown generator | **I** | Repository-owned validator and report generator check ASCII schema, IDs, dates and document paths and update only the marked ledger region. | [260922-16 structured feature registry design.md](260922-16%20structured%20feature%20registry%20design.md) |
| 260922 | Git-friendly structured feature registry | **I** | Canonical ASCII TSV contains the complete migrated feature set and supports deterministic selected-field reports. | [260922-16 structured feature registry design.md](260922-16%20structured%20feature%20registry%20design.md) |
| - | CP/M FAND feature-registry demonstration | **B** | Begin with a read-only, ASCII-safe fixed-width export and compare FAND filtering/reports with host output; never write directly back to canonical TSV initially. |  |
| - | Interactive text-table analysis | **B** | LibreOffice Calc is already installed; Miller 6.21 passed non-installing TSV and custom-pipe trials. VisiData remains optional. |  |

## Application and historical compatibility investigations

| Introduced | Feature | State | Summary and remaining boundary | Documents |
| --- | --- | --- | --- | --- |
| 260920 | SHARP MZ-821 / Lamac LEC CP/M lineage | **D**, reference | Investigation and BIOS reference. A dedicated MZ-821 decoder is not approved. | [260921-00 FAND MZ-821 Lamac-LEC CP-M investigation.md](260921-00%20FAND%20MZ-821%20Lamac-LEC%20CP-M%20investigation.md), [260920-22 Sharp MZ-800 Czech CP-M BIOS source reference.md](260920-22%20Sharp%20MZ-800%20Czech%20CP-M%20BIOS%20source%20reference.md) |
| - | CP/M FAND 1.0 ANSI/VT52 behavior | **D**, not-accepted | Keep all live observations in the private MIXWORX FAND log under the manual directory. ANSI 0x9B support exists, but FAND screen and controls are not yet fully verified. |  |
| - | dBASE/WordStar/other configurable CP/M applications | **R** | Use profile and original installer/reference evidence rather than adding speculative terminal families. |  |
| - | FANDEDIT .CHN reverse engineering | **X**, here | Keep the private note, but broad binary/Ghidra analysis belongs in ChatGPT/Lyra before repo-grounded implementation. |  |
| - | TP301 insert-line/status-row anomaly | **R**, resolved | Application command configuration explained the behavior; terminal CSI L/M remains valid. |  |

<!-- feature-registry:end -->

## Reference and evidence map

These documents contain facts, measurements or operating guidance rather than
independent feature promises:

- [Target context](260908-18%207xtgv-target-context.md),
  [platform comparison](260909-12%20platform-mcu-comparison.md),
  [JC8048W535 baseline](260908-17%20jc8048w535-baseline.md), and individual
  JC8048W550 target records.
- [RunCPM console keys](260910-01%20runcpm-console-keys.md),
  [terminal keyboard cheatsheet](260912-00%207xtgv%20terminal%20keyboard%20control%20cheatsheet.txt),
  and the [current terminal manual](260912-23%207xtgv%20terminal%20manual/README.md).
- [Turbo Pascal installation reference](260911-14%20Turbo%20Pascal%203.01%20keyboard%20and%20terminal%20installation%20reference.md),
  [BBC/TP test records](260915-19%20BBC%20BASIC%20and%20app%20history%20handoff.md),
  and the FAND private log.
- [VTSTART/VTSTOP and PIP quirks](260912-21%20VTSTART%20VTSTOP%20CP-M%20assembly%20and%20PIP%20quirks.md),
  [drive/user and internal-CCP details](260912-22%20RunCPM%20CP-M%20drive-user%20PIP%20and%20CCP%20quirks.md),
  and the [focused user-area note](260912-22%20RunCPM%20CP-M%20user%20areas%20PIP%20Gn%20and%20CCP%20quirks.md).
  These explain CP/M behavior; they are not separate terminal work items.
- [Reboot startup capture](260911-18%20reboot-serial-startup-capture.md),
  the [260913 handoff](260913-01%20recent%20terminal%20and%20CP-M%20handoff.md),
  [260914 target handoff](260914-12%20terminal%20maintenance%20and%20JC8048W550%20handoff.md),
  dated smoke reports and validation notes are evidence for the implementation
  rows above.
- [Code-change monitoring how-to](code%20changes%20monitoring%20-%20howto/code%20changes%20monitoring%20-%20howto.txt),
  [model-assignment guidance](260911-22%20%21%21%21%20PERF%20COST%20%21%21%21%207xtgv-codex-model-assignment-cheatsheet.md),
  and the [host incident report](260922-11%20host%20SAK%20NTFS%20recovery%20and%20Codex%20CWD%20incident%20report.md)
  are development-process references.
- Recovery dumps, screenshots and PDFs remain immutable supporting artifacts;
  the corresponding Markdown record is the searchable source of status.

Historical compatibility sources are retained even when a later review
supersedes decisions: [the original 260916 note](260916_7xtgv_terminal_legacy_compatibility.md)
and [its later expanded copy](260917%207xtgv%20terminal%20I/O%20profiles%20%20ANSI%2BVT100%2C%20VT52%2C%20ADM-3A%20and%20WordStar%20input.md)
lead to the signed 260917 review rather than directly authorizing code.

Private MIXWORX FAND logs, recovered raw font/mapping dumps, module-vendor text
files, screenshots and generated PDFs are intentionally not made normative
ledger links.  They stay available locally as evidence and may be excluded
from Git publication by project policy.

## Known documentation drift found during this review

- The key-first implementation note still described rebuild, transfer and
  device smoke as pending; it is updated alongside this ledger.
- Several older design documents correctly retain their historical status
  lines even though a later checkpoint implemented part or all of the design.
  The ledger points to the later evidence instead of rewriting those records.
- The firmware README still contains a few capability-limit sentences written
  before configurable function/special keys and XMODEM mirror support.  Treat
  the manual plus this ledger as current until that prose receives a focused
  cleanup; do not infer removal of the implemented features.
- Original modifier-first examples remain in historical design documents with
  supersession notices.  Only key-first names are accepted by current source.

## Maintenance rule

For every non-trivial new idea or implementation:

1. Preserve the dated source discussion/plan/report.
2. Add or update one canonical `docs/features.tsv` record with the strongest
   proven state; never edit the marked generated tables directly.
3. Never advance **I** to **H** solely from host tests or a successful build.
4. Record partial delivery explicitly and keep the unfinished subfeatures.
5. Link later corrections instead of silently rewriting the historical plan.
6. Keep firmware artifact promotion (`new` to `current`) separate from feature
   state and hardware-test evidence.
7. Run `python3 src/feature_registry.py check` and then `update-ledger`.
