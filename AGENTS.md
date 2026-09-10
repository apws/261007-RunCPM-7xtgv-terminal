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
- Before sending automated commands through the shared CP/M terminal, select
  terminal profile/history bank `#9`. Keep the user's active application
  history in banks `#0` through `#8` untouched.
- Before starting a nonessential web search or broad external research, tell the
  user briefly what is needed and ask whether to search here or let them handle
  it in ChatGPT/Lyra and bring the result back. Proceed immediately when the
  user explicitly requests browsing or current external information.
- Keep this file short. Put detailed history, inventories and experiments in
  linked documentation under `docs/`.
- Name project working notes under `docs/` with their filesystem-time prefix
  `YYMMDD-HH ` so extension sorting also preserves their timeline.  Keep
  stable readmes, recovered material, and vendor resources in their existing
  names.
