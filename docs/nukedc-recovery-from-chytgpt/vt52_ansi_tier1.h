// vt52_ansi_tier1.h
// Tier-1 terminal interpreter: VT52 + ANSI SGR basics (colors/attrs) for 7xtgv-style text grids.
//
// Designed for your RP2350 dual-core setup:
//   - core0: RunCPM produces a byte stream
//   - core1: feeds bytes into this interpreter, which updates a character grid backend
//
// Supports:
//   VT52: ESC A/B/C/D/H/Y/J/K/E/I/F/G/Z
//   ANSI: CSI ... m (SGR subset), CSI H/f (cursor pos), CSI J/K (erase), CSI A/B/C/D (cursor step)
//
// Notes:
//   - VT52 has no native colors. Colors come from ANSI SGR (VT100+/xterm style).
//   - If you only need VT52, you can ignore CSI/SGR (still safe).
//   - This file includes an optional 7xtgv backend implementation (define VTT_BACKEND_7XTGV).
//
// License: MIT (do whatever, keep this header).

#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// -------------------------------
// Backend interface
// -------------------------------
typedef struct vtt_backend_t {
  void* user;

  // Screen geometry
  uint16_t (*cols)(void* user);
  uint16_t (*rows)(void* user);

  // Cursor
  void (*set_cursor)(void* user, uint16_t x, uint16_t y);
  void (*get_cursor)(void* user, uint16_t* x, uint16_t* y);

  // Drawing
  void (*putch)(void* user, uint8_t ch);      // draw at cursor, advance, wrap/scroll as backend decides

  // Erase ops (relative to cursor)
  void (*erase_eol)(void* user);              // erase from cursor to end of line
  void (*erase_eos)(void* user);              // erase from cursor to end of screen
  void (*clear_screen)(void* user);           // clear + home

  // Optional: scroll up within full screen (used for VT52 ESC I if backend wants it)
  void (*scroll_up)(void* user, uint16_t lines);

  // Attribute setters (called when SGR changes)
  // You can map these to your 7xtgv ink/paper/font model.
  void (*set_attr)(void* user,
                   uint8_t bold, uint8_t underline, uint8_t inverse,
                   uint8_t fg8, uint8_t bg8); // fg8/bg8 are 0..7 (ANSI basic colors)
} vtt_backend_t;

// -------------------------------
// Interpreter state
// -------------------------------
typedef enum vtt_state_e {
  VTT_ST_NORMAL = 0,
  VTT_ST_ESC,
  VTT_ST_VT52_Y_ROW,
  VTT_ST_VT52_Y_COL,
  VTT_ST_CSI,          // ESC [
} vtt_state_t;

typedef struct vtt_t {
  vtt_backend_t be;

  vtt_state_t st;

  // VT52 cursor-addressing temp
  uint8_t vt52_row;
  uint8_t vt52_col;

  // CSI parser
  int16_t csi_params[8];
  uint8_t csi_nparams;
  int16_t csi_cur;

  // Attributes (ANSI SGR)
  uint8_t bold;
  uint8_t underline;
  uint8_t inverse;
  uint8_t fg8;  // 0..7, 7=white default
  uint8_t bg8;  // 0..7, 0=black default

  // Mode flags
  uint8_t vt52_graphics; // ESC F/G (optional; you can ignore in backend)
} vtt_t;

void vtt_init(vtt_t* t, const vtt_backend_t* backend);
void vtt_reset(vtt_t* t);                    // resets parser + attributes (doesn't necessarily clear screen)
void vtt_feed(vtt_t* t, const uint8_t* data, size_t n);
void vtt_feed1(vtt_t* t, uint8_t b);

// -------------------------------
// Optional backend: 7xtgv grid
// Define VTT_BACKEND_7XTGV and provide the external symbols/macros below.
//
// Required from your 7xtgv code:
//   - extern uint8_t paperRAM[], charRAM[], attrRAM[];
//   - TXT_COLS, TXT_ROWS macros (uint16_t-ish)
//   - setCell(cx, cy, paperGlyph, inkGlyph, glyphset2, paper2, ink4) inline or function
//
// This backend implements:
//   - putch with wrap+scroll
//   - erase_eol/erase_eos/clear_screen
//   - scroll_up
//   - set_attr mapping ANSI fg/bg/bold/inverse -> 7xtgv (paper2/ink4) via tables
//
#ifdef VTT_BACKEND_7XTGV

// Provide these from your build (they already exist in 7xtgv_demo-cheers.h).
#ifndef TXT_COLS
#error "VTT_BACKEND_7XTGV requires TXT_COLS"
#endif
#ifndef TXT_ROWS
#error "VTT_BACKEND_7XTGV requires TXT_ROWS"
#endif

// You can override these mappings to match your theme.
// Defaults are conservative: map ANSI 8 colors into 7xtgv's 0..15 ink, 0..3 paper.
//
// - fg_map[0..7] : ANSI basic fg -> 7xtgv ink4 (0..15)
// - bg_map[0..7] : ANSI basic bg -> 7xtgv paper2 (0..3)
extern const uint8_t vtt7_default_fg_map[8];
extern const uint8_t vtt7_default_bg_map[8];

// Create a backend instance pointing at your screen RAM.
// - glyphset2: 0..3 (font bank)
// - paper_glyph: usually 0x20 (space inversion fill in your design)
// Returns a fully-populated backend you can pass to vtt_init.
vtt_backend_t vtt_make_backend_7xtgv(uint8_t glyphset2, uint8_t paper_glyph,
                                     const uint8_t fg_map[8], const uint8_t bg_map[8]);

#endif // VTT_BACKEND_7XTGV

#ifdef __cplusplus
}
#endif
