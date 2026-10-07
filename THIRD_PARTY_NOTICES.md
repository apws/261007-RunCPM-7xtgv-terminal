# Third-party notices and review inventory

Inventory scanned on 261007; original-terminal MIT decision recorded on 261008.
See [LICENSE.md](LICENSE.md) for the scope of the project's own MIT grant.
Third-party code and assets retain their existing terms. These notices neither
relicense them nor certify every imported package for redistribution.

In a mixed file, preserve upstream credits and terms as well as the project's
notice. The native specification is excluded from the terminal grant.

## Source and assets present in this repository

| Part | Evidence and identified terms | Scope / remaining work |
| --- | --- | --- |
| RunCPM-derived emulator, BDOS/BIOS, console, disk and CCP code | [MIT notice, copyright 2017 Mockba the Borg](src/RunCPM_7xtgv-terminal/LICENSE.RunCPM) | Preserve the complete upstream notice alongside the project's MIT notice for original additions. Neither notice relicenses unrelated third-party parts of the subtree. |
| POSIX comparator in `src/RunCPM_linux/` | Its [README](src/RunCPM_linux/README.md) identifies upstream POSIX files and shared active core headers; `main.c` retains Marcelo Dantas's 2016 copyright credit | A local [RunCPM MIT notice](src/RunCPM_linux/LICENSE.RunCPM) accompanies this tree too. Retain file-level credits. |
| Archived RunCPM in `src/legacy/RunCPM_7xtgv/` | Separate preserved [MIT notice](src/legacy/RunCPM_7xtgv/LICENSE.RunCPM) | Archival status does not remove attribution requirements or license unrelated font/demo data. |
| EP4_10 panel initialization table | Apache-2.0 marker beside the adapted table in [jc8012p4a1c.h](src/RunCPM_7xtgv-terminal/hardware/arduino_7xtgv/boards/jc8012p4a1c.h); also in both legacy demos' `7xtgv-jc8012p4a1c-newpanel.h` | Comment attributes the table to Espressif's NewPanel example. Recover the exact source/version and applicable notices; do not infer that the entire mixed HAL is Apache-2.0. |
| Legacy JD9365 driver | [Driver source](src/legacy/jc8012p4a1c-display-test/src/esp_lcd_jd9365.c) and adjacent header: Apache-2.0, copyright 2024 Espressif Systems (Shanghai) CO LTD | Preserve credits and modification notices. The [full Apache license](LICENSES/Apache-2.0.txt) is included; exact upstream version and any applicable NOTICE still need verification. |
| Root browser stylesheet | [CSS](asciidoctorjs-crx-mixworx.css) explicitly marks normalize.css v2.1.2 as MIT; also contains framework-derived styling and local modifications | Full upstream stylesheet provenance and notices still need reconstruction. Asciidoctor itself has an [MIT notice](https://github.com/asciidoctor/asciidoctor/blob/main/LICENSE), but that alone does not prove the origin/license of every CSS block. |
| Original tools, browser JS helper, guest helpers, tests and adapters | Original terminal material is now MIT under [LICENSE-7XTGV-TERMINAL](LICENSE-7XTGV-TERMINAL) | Adapted third-party snippets retain their terms; an API name such as Arduino, SdFat or SDL is not provenance evidence. |
| Font, palette and local demo data | Original terminal assets are covered by the project's MIT grant | The grant covers only rights held by the licensor. Confirm any underlying imported glyph/image provenance; the name "QL" alone establishes neither origin nor permission. |
| Guides, dated documents, diagrams, screenshots, PDFs and demo imagery | Mixed authored material, quotations/references and captured/imported content | A software license does not automatically clear these assets. Record ownership, source and permission separately where needed. Screenshots and historical documents may contain third-party material. |

The [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0), section 4,
requires a license copy, preservation of relevant notices and identification
of modified files; applicable upstream NOTICE content must also be retained.
An SPDX label or this summary is not a replacement for those materials.

## Build and runtime dependencies, normally installed separately

Version pins come from [sketch.yaml](src/RunCPM_7xtgv-terminal/sketch.yaml).
The [desktop CMake configuration](src/RunCPM_7xtgv-terminal/desktop/CMakeLists.txt)
shows which external graphics sources and SDL interface it uses.

| Dependency | Evidence reviewed | Distribution boundary |
| --- | --- | --- |
| SdFat 2.3.0 | Installed `SdFat/LICENSE.md`: MIT, copyright 2011..2020 Bill Greiman | Hardware build dependency, not vendored source here. Desktop uses the project's host-file adapter, not the SdFat implementation. Retain the actual package notice when distributing incorporated code. |
| GFX Library for Arduino 1.6.4 | Installed `license.txt`: two-clause BSD notice, copyright 2012 Adafruit Industries. `Arduino_GFX.cpp` also credits Adafruit-GFX and LovyanGFX origins | Used by hardware and desktop. Audit compiled source/subcomponents and any vendor patches; the top-level notice is not a complete dependency audit. Desktop compiles real `Arduino_G.cpp`, `Arduino_GFX.cpp` and `canvas/Arduino_Canvas.cpp`. |
| Arduino-ESP32 3.1.0 | Installed core `Arduino.h` includes LGPL-2.1-or-later terms; platform bundles additional SDK components | Audit the actual linked core, ESP-IDF, FreeRTOS and other libraries for each target. Do not label the complete platform with a single license. |
| Arduino-Pico 5.4.4 | Installed top-level `LICENSE`: LGPL-2.1 text; bundled Pico SDK top-level notice: BSD-3-Clause; FreeRTOS-Kernel: MIT | Individual file and nested-package terms still apply. Installed components are not necessarily all linked into every firmware. |
| SDL desktop presentation | [SDL2 license](https://github.com/libsdl-org/SDL/blob/SDL2/LICENSE.txt): zlib. This workstation provides the SDL2 interface through `sdl2-compat`, whose installed notice is also zlib | Record the actual linked implementation/version for a shipped desktop binary, including SDL3 when provided through compatibility libraries. Do not assume every host uses identical packages. |
| Host/build tools | Arduino CLI, compilers, CMake, Python/pyserial, terminal/tmux tools and documentation renderers are separately installed | Using a tool is distinct from bundling its code or runtime. Inventory any redistributed tool executables, generated runtime code, libraries and license exceptions in the actual release package. |

Before shipping binaries, review the exact link/dependency set, required
notices and any source/relinking obligations under applicable library licenses.
A successful build does not establish distribution compliance. This is a
technical inventory, not legal advice or a completed compliance assessment.

## Versioned CP/M card: separate package review required

The [working card](cpmcard/README.adoc) contains 338 tracked files at the scan
checkpoint, including configured applications, sources, binaries, backups,
disk images and local configuration/history. Inclusion in Git is not a new
license grant. Catalog names, age, an embedded credit or availability from a
download site do not establish redistribution permission for the exact copy.

The following is a review queue, **not a cleared-software list**. Filename
groupings identify candidates; they do not prove binary version or ownership.

| Location / examples | Separate review needed |
| --- | --- |
| `A/0`, `A/4`: `ASM`, `DDT`, `ED`, `LOAD`, `PIP`, `STAT`, `SUBMIT`, `MAC`, `ZSID` and other system utilities | Identify the exact Digital Research or other distribution and applicable grant. RunCPM's emulator license does not license guest utilities. |
| `A/0`, `A/7`: `MBASIC.COM`, `M522*`, `MBAS529*`, `MBAS530*`; `A/C`: Altair BASIC images | Microsoft/MITS and variant/patch provenance and exact redistribution terms remain unresolved here. |
| `A/0`, `A/2`: `TURBO.COM`, `TINST.COM` and related package files | Borland Turbo Pascal distribution/version and terms, including configured/patched copies. |
| `A/1`: `BBCBASIC.COM`, `BBCLOWER.COM`, package README and examples | BBC BASIC Z80 package/version terms and local patch provenance. The included test-package README supplies usage instructions, not a complete license determination. |
| `A/4`: `Z80ASM.COM`, `Z80ASM.PDF`, other assemblers; copies in `A/0` | Identify each assembler/manual separately, including SLR material; do not apply one package's terms to the whole area. |
| `A/B`, `A/E`, `A/F`: `Nc.com`, `Nu.com`, dBASE programs, FAND programs, databases and backups | Establish package identity, rightsholder and permission. Review data/privacy separately from executable licensing. |
| `A/6`: Mandelbrot/Dhrystone programs, `ZEXALL`, `ZEXDOC`, `8085EXER` and other demos/tests | Match each source/binary to its original author, version and license, including any copyleft/source obligations. No permission inferred from benchmark status. |
| `A/C`: Pico Z80 / z80pack-related examples and binary images | README names Tiny BASIC, Microchess, tests and other examples. These are multiple works, not automatically covered by an emulator's license. |
| `A/D`: `.dsk` collection | Included README identifies CP/M 2.2/3, MP/M II, FIG Forth, UCSD p-System and application images. Inventory the contents and grants per image/package; the container file is not a license boundary. |
| Project-authored `VT*`, `CARDMARK`, graphics helpers, scripts, catalogs and configuration | Original terminal material is MIT. Separate it from imported namesakes and variants; preserved third-party code retains its own terms. |
| Archives, snapshots and other assets | Inspect contained material too. The listed contents of `A/0/7XTGVTRM.ZIP` are a single historical INI; this does not classify other future archives. |

Unresolved does not mean proven infringement; it means this scan cannot attest
to the necessary permission. The card is already versioned, so these questions
apply to the current source distribution, not only a future downloadable image.

## Remaining release work

1. Original terminal material is MIT. Keep the native specification outside
   that grant and confirm rights in adapted fonts/artwork and other imports.
2. Preserve RunCPM notices; collect missing full third-party license texts and
   applicable NOTICE files, with exact upstream versions and modification notes.
3. Resolve the card review queue with package sources, versions/hashes and
   evidence of permission. Do not treat an emulator license as a guest license.
4. Produce separate manifests for source, each firmware target, desktop binaries
   and any card image. Record actual bundled/link-time dependencies, not merely
   everything installed on a developer's machine.
5. Review required attribution/source/relinking materials before distribution;
   get qualified legal review for unresolved licensing questions if needed.

See the [Technical Reference](guides/technical-reference.adoc#_licensing_and_attribution_inventory)
and [261007 scan checkpoint](docs/261007-2344%20DONE%20Component%20licensing%20inventory%20and%20open%20decisions.adoc).

## Preserved license texts

These copies supplement, not replace, source-level notices. Installed dependency
copies below record the inspected packages; they are not a complete license
bundle for every possible compiled binary.

* RunCPM: notices alongside the active, POSIX and archived RunCPM trees, linked above.
* Apache display code: [Apache-2.0](LICENSES/Apache-2.0.txt), retrieved from the
  [Apache Software Foundation](https://www.apache.org/licenses/LICENSE-2.0.txt).
* Arduino_GFX 1.6.4: [inspected package notice](LICENSES/Arduino_GFX-1.6.4.txt).
* SdFat 2.3.0: [inspected package notice](LICENSES/SdFat-2.3.0.txt).
* This workstation's SDL2 compatibility runtime: [sdl2-compat notice](LICENSES/sdl2-compat.txt).
