# RunCPM 7xtgv-terminal — licensing

**The original 7xtgv-terminal software is licensed under the MIT License,
for both MCU/display hardware and the desktop emulator.**

Copyright (c) 2026 Petr Antos (@apws/@7alken).
The complete terms are in [LICENSE-7XTGV-TERMINAL](LICENSE-7XTGV-TERMINAL).
This scope was selected by the maintainer on 261008.

## What the project's MIT license covers

Subject to the exclusions below, the grant covers original project material
in the present terminal implementation:

* Terminal rendering/parser, history/profiles, board HAL integrations and
  runtime code, including the desktop emulator, adapters and shims.
* Project-authored additions to the RunCPM integration and POSIX comparator.
* Host tooling, build/launch scripts, tests and project-authored CP/M helpers.
* Original terminal fonts, palette/demo data, configuration and catalogs,
  to the extent the licensor holds the relevant rights.
* Original terminal documentation and related assets, including the guides,
  README, browser helpers and terminal-development records.
* Original terminal/demo material retained under `src/legacy/`.

The grant permits use, modification and redistribution, including commercial
use, subject to the MIT terms. Preserve the copyright and license notice.
When copying a covered component out of this repository, include its relevant
license texts and notices too.

## What it does not relicense

* **The native 7xtgv specification and native-engine design documents in
  `docs/`**, including their PDF copies, are not covered by this terminal MIT
  grant. Principal examples are listed below. No new license for those
  documents or a future separate native-engine implementation is selected here.
  This exclusion does not retract MIT permissions for code already included
  in the covered terminal implementation merely because it is native-inspired.
* **Third-party material** retains its own licenses and notices. RunCPM remains
  MIT under its original notice; Apache/BSD/LGPL and other components retain
  their applicable terms. Local modifications do not erase upstream rights.
* **Imported CP/M applications, manuals, artwork, quotations and
  other third-party assets** are not relicensed by their inclusion in the
  repository. The complete `cpmcard/` tree is not a blanket-MIT software package.

The native-specification exclusion includes, but is not limited to:

* [Original Tiled GUI Video proposal](docs/260906-21%20250524%20!!!!%20mixworx%207xtgv%20-%20TILED-GUI-VIDEO-256.adoc).
* [Native v0.1 specification and implementation plan](docs/260910-13%207xtgv%20v0.1%20native%20specification%20and%20implementation%20plan.adoc).
* [Native SEMI mode and image-demo design](docs/260910-13%207xtgv%20native%20SEMI%20mode%20and%20image%20demo.adoc).

In mixed documents, this exclusion applies to the native specification/design
material, not to separately identifiable documentation of the MIT terminal.
Existing third-party rights and permissions remain unaffected.

## Third-party notices

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for components, preserved
license texts, dependencies and unresolved imported-package questions.
Keep `LICENSE.RunCPM` in each RunCPM-derived tree. A source checkout, firmware
binary, desktop package and card image may need different accompanying notices
and source materials; the terminal MIT grant does not replace those obligations.
