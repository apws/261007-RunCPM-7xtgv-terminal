# Add `##` RAW/bypass mode

Implementation status (2026-09-10): implemented in the terminal runtime and
host transfer helper; host/unit tests and RP2350/ESP32-S3 builds pass. The
ESP32-S3 target was flashed and verified with the mirrored TFT path: toggle-in
showed `[## RAW / #9]`, RAW `DIR` executed, explicit off restored `#9`, and Up
did not recall the RAW command. Physical keyboard chords and comparative
`vtput/vtget` timing remain to be checked.

Private host controls are `CSI > 15 v` (toggle), `CSI > 16 v` (explicit on),
and `CSI > 17 v` (explicit off). These controls, current-mode display
`CSI > 14 v`, and numbered bank selection remain reserved inside RAW mode;
all other input bytes bypass the keyboard/history path unchanged.

Implement a temporary terminal-local `##` mode for debugging and fast host/Codex transfers.

## Semantics

- `#0..#9` remain persistent application banks.
- `##` is **not another bank**; it is a temporary RAW overlay.
- Entering `##` remembers the currently selected bank.
- Leaving `##` restores that bank.
- Selecting `#0..#9` while RAW is active exits RAW and selects that bank.

## RAW input path

While `##` is active:

- bypass app-bank input mappings;
- bypass application-specific input adapters/state machines;
- disable history capture and history editing/interception;
- do not transform injected `vtput` data;
- otherwise keep the normal CP/M console/output/display path unchanged.

Goal: provide the simplest and fastest possible input path for exact-byte testing and `vtput/vtget` transfers.

## Indicator

Extend the existing temporary bank indicator:

- `#+Space` and/or existing `Alt+Space` indicator shows current mode briefly.
- Normal example: `[#3 BBCBASIC]`
- RAW example: `[## RAW / #3]`

No permanent status bar is required.

## Codex/test usage

Keep `#9` as the normal dedicated TEST/CODEX bank.

Typical comparison:

```text
#9          normal controlled test profile
#9 -> ##    raw/untransformed test path
## -> exit  returns to #9
```

Codex should use `#9` rather than disturbing the user's active application bank, except when explicitly testing bank-switch behavior.

## Testing

Keep testing compact:

1. Verify `##` enters/exits and restores the previous bank.
2. Verify indicator correctly reports RAW and underlying bank.
3. Verify history is untouched while RAW traffic is sent.
4. Compare a short `vtput/vtget` transfer in `#9` vs `#9 + ##`.
5. Verify exact test bytes arrive unchanged in RAW mode.

Do not build a large test suite now. Once the implementation is substantially complete, ask Petr for any useful interactive keyboard/CP/M tests that cannot be verified automatically.
