// vt52_ansi_tier1.c
#include "vt52_ansi_tier1.h"

#include <string.h>

static inline void be_set_attr(vtt_t* t) {
  if (t->be.set_attr) {
    t->be.set_attr(t->be.user, t->bold, t->underline, t->inverse, t->fg8, t->bg8);
  }
}

void vtt_init(vtt_t* t, const vtt_backend_t* backend) {
  if (!t || !backend) return;
  memset(t, 0, sizeof(*t));
  t->be = *backend;
  vtt_reset(t);
}

void vtt_reset(vtt_t* t) {
  if (!t) return;
  t->st = VTT_ST_NORMAL;
  t->vt52_row = 0;
  t->vt52_col = 0;

  t->csi_nparams = 0;
  t->csi_cur = -1;
  for (unsigned i = 0; i < 8; i++) t->csi_params[i] = -1;

  // Reasonable defaults
  t->bold = 0;
  t->underline = 0;
  t->inverse = 0;
  t->fg8 = 7; // white
  t->bg8 = 0; // black
  t->vt52_graphics = 0;
  be_set_attr(t);
}

static inline void be_home(vtt_t* t) {
  if (!t->be.set_cursor) return;
  t->be.set_cursor(t->be.user, 0, 0);
}

static inline void be_step(vtt_t* t, int dx, int dy) {
  if (!t->be.get_cursor || !t->be.set_cursor) return;
  uint16_t x=0,y=0;
  t->be.get_cursor(t->be.user, &x, &y);
  int nx = (int)x + dx;
  int ny = (int)y + dy;
  uint16_t cols = t->be.cols ? t->be.cols(t->be.user) : 0;
  uint16_t rows = t->be.rows ? t->be.rows(t->be.user) : 0;
  if (cols) { if (nx < 0) nx = 0; if (nx >= (int)cols) nx = (int)cols - 1; }
  if (rows) { if (ny < 0) ny = 0; if (ny >= (int)rows) ny = (int)rows - 1; }
  t->be.set_cursor(t->be.user, (uint16_t)nx, (uint16_t)ny);
}

static inline void be_pos(vtt_t* t, uint16_t x, uint16_t y) {
  if (!t->be.set_cursor) return;
  uint16_t cols = t->be.cols ? t->be.cols(t->be.user) : 0;
  uint16_t rows = t->be.rows ? t->be.rows(t->be.user) : 0;
  if (cols && x >= cols) x = cols - 1;
  if (rows && y >= rows) y = rows - 1;
  t->be.set_cursor(t->be.user, x, y);
}

// --- CSI helpers ---
static inline void csi_reset(vtt_t* t) {
  t->csi_nparams = 0;
  t->csi_cur = -1;
  for (unsigned i = 0; i < 8; i++) t->csi_params[i] = -1;
}

static inline void csi_push(vtt_t* t) {
  if (t->csi_nparams >= 8) return;
  t->csi_params[t->csi_nparams++] = t->csi_cur;
  t->csi_cur = -1;
}

static inline int16_t csi_get(const vtt_t* t, unsigned idx, int16_t defv) {
  if (idx >= t->csi_nparams) return defv;
  int16_t v = t->csi_params[idx];
  return (v < 0) ? defv : v;
}

static void csi_apply_sgr(vtt_t* t) {
  // If no params: same as 0
  if (t->csi_nparams == 0) {
    t->bold = 0; t->underline = 0; t->inverse = 0; t->fg8 = 7; t->bg8 = 0;
    be_set_attr(t);
    return;
  }

  for (unsigned i = 0; i < t->csi_nparams; i++) {
    int16_t p = t->csi_params[i];
    if (p < 0) p = 0;

    if (p == 0) {
      t->bold = 0; t->underline = 0; t->inverse = 0; t->fg8 = 7; t->bg8 = 0;
    } else if (p == 1) {
      t->bold = 1;
    } else if (p == 4) {
      t->underline = 1;
    } else if (p == 7) {
      t->inverse = 1;
    } else if (p == 22) { // normal intensity
      t->bold = 0;
    } else if (p == 24) { // underline off
      t->underline = 0;
    } else if (p == 27) { // inverse off
      t->inverse = 0;
    } else if (p >= 30 && p <= 37) {
      t->fg8 = (uint8_t)(p - 30);
    } else if (p == 39) {
      t->fg8 = 7;
    } else if (p >= 40 && p <= 47) {
      t->bg8 = (uint8_t)(p - 40);
    } else if (p == 49) {
      t->bg8 = 0;
    }
    // Ignore everything else (xterm extensions, etc.)
  }
  be_set_attr(t);
}

static void csi_apply(vtt_t* t, uint8_t final) {
  // Ensure last param is captured if we were in digits
  if (t->csi_cur >= 0 || t->csi_nparams > 0) {
    // If last char before final was ';' then csi_cur=-1; that is ok: push -1 meaning "default"
    if (t->csi_nparams == 0 || t->csi_params[t->csi_nparams-1] != t->csi_cur) {
      // Only push if we haven't already pushed after ';'
      // Heuristic: if final came right after digits, csi_cur>=0 and we haven't pushed.
      if (t->csi_cur >= 0) csi_push(t);
    }
  }

  switch (final) {
    case 'm': // SGR
      csi_apply_sgr(t);
      break;

    case 'H': // CUP
    case 'f': // HVP
    {
      // ANSI is 1-based: row;col
      int16_t row1 = csi_get(t, 0, 1);
      int16_t col1 = csi_get(t, 1, 1);
      if (row1 < 1) row1 = 1;
      if (col1 < 1) col1 = 1;
      be_pos(t, (uint16_t)(col1 - 1), (uint16_t)(row1 - 1));
      break;
    }

    case 'A': // CUU
      be_step(t, 0, -(int)csi_get(t, 0, 1));
      break;
    case 'B': // CUD
      be_step(t, 0, (int)csi_get(t, 0, 1));
      break;
    case 'C': // CUF
      be_step(t, (int)csi_get(t, 0, 1), 0);
      break;
    case 'D': // CUB
      be_step(t, -(int)csi_get(t, 0, 1), 0);
      break;

    case 'J': // ED
      if (!t->be.erase_eos) break;
      // 0=from cursor to end, 2=entire screen
      if (csi_get(t, 0, 0) == 2) {
        if (t->be.clear_screen) t->be.clear_screen(t->be.user);
      } else {
        t->be.erase_eos(t->be.user);
      }
      break;

    case 'K': // EL
      if (!t->be.erase_eol) break;
      // 0=to end of line, 2=entire line
      if (csi_get(t, 0, 0) == 2) {
        // emulate whole-line erase by going to col 0, erase_eol, restore
        if (t->be.get_cursor && t->be.set_cursor) {
          uint16_t x=0,y=0; t->be.get_cursor(t->be.user,&x,&y);
          t->be.set_cursor(t->be.user,0,y);
          t->be.erase_eol(t->be.user);
          t->be.set_cursor(t->be.user,x,y);
        } else {
          t->be.erase_eol(t->be.user);
        }
      } else {
        t->be.erase_eol(t->be.user);
      }
      break;

    default:
      break;
  }
}

void vtt_feed(vtt_t* t, const uint8_t* data, size_t n) {
  if (!t || !data) return;
  for (size_t i = 0; i < n; i++) vtt_feed1(t, data[i]);
}

void vtt_feed1(vtt_t* t, uint8_t b) {
  if (!t) return;

  switch (t->st) {
    case VTT_ST_NORMAL:
      if (b == 0x1B) { // ESC
        t->st = VTT_ST_ESC;
        return;
      }
      // Basic control chars
      if (b == 0x07) { /* BEL: optional */ return; }
      if (b == 0x08) { be_step(t, -1, 0); return; } // BS
      if (b == 0x09) { // TAB: 8-col stops
        if (t->be.get_cursor && t->be.set_cursor && t->be.cols) {
          uint16_t x=0,y=0; t->be.get_cursor(t->be.user,&x,&y);
          uint16_t nx = (uint16_t)((x + 8u) & ~7u);
          uint16_t cols = t->be.cols(t->be.user);
          if (nx >= cols) nx = cols ? (cols - 1) : 0;
          t->be.set_cursor(t->be.user,nx,y);
        }
        return;
      }
      if (b == 0x0A || b == 0x0B) { // LF/VT
        be_step(t, 0, 1);
        return;
      }
      if (b == 0x0D) { // CR
        if (t->be.get_cursor && t->be.set_cursor) {
          uint16_t x=0,y=0; t->be.get_cursor(t->be.user,&x,&y);
          t->be.set_cursor(t->be.user,0,y);
        }
        return;
      }

      // Printable
      if (t->be.putch) t->be.putch(t->be.user, b);
      return;

    case VTT_ST_ESC:
      // VT52 + ANSI lead-ins
      if (b == '[') { // CSI
        csi_reset(t);
        t->st = VTT_ST_CSI;
        return;
      }
      // VT52 sequences (ESC <cmd>)
      switch (b) {
        case 'A': be_step(t, 0, -1); break;
        case 'B': be_step(t, 0,  1); break;
        case 'C': be_step(t,  1, 0); break;
        case 'D': be_step(t, -1, 0); break;
        case 'H': be_home(t); break;
        case 'Y': t->st = VTT_ST_VT52_Y_ROW; return;
        case 'J':
          if (t->be.erase_eos) t->be.erase_eos(t->be.user);
          break;
        case 'K':
          if (t->be.erase_eol) t->be.erase_eol(t->be.user);
          break;
        case 'E':
          if (t->be.clear_screen) t->be.clear_screen(t->be.user);
          break;
        case 'I': // reverse line feed / scroll up
          if (t->be.scroll_up) t->be.scroll_up(t->be.user, 1);
          break;
        case 'F': t->vt52_graphics = 1; break;
        case 'G': t->vt52_graphics = 0; break;
        case 'Z':
          // Identify (VT52 expects response ESC / Z). The "response" is up to your transport.
          // Here we do nothing; you can catch this in your outer layer if you want to answer.
          break;
        default:
          break;
      }
      t->st = VTT_ST_NORMAL;
      return;

    case VTT_ST_VT52_Y_ROW:
      // VT52 row is byte-0x20 (0-based)
      if (b >= 0x20) t->vt52_row = (uint8_t)(b - 0x20);
      else t->vt52_row = 0;
      t->st = VTT_ST_VT52_Y_COL;
      return;

    case VTT_ST_VT52_Y_COL:
      if (b >= 0x20) t->vt52_col = (uint8_t)(b - 0x20);
      else t->vt52_col = 0;
      be_pos(t, t->vt52_col, t->vt52_row);
      t->st = VTT_ST_NORMAL;
      return;

    case VTT_ST_CSI:
      // parse: digits and ';' until final byte
      if (b >= '0' && b <= '9') {
        int16_t d = (int16_t)(b - '0');
        if (t->csi_cur < 0) t->csi_cur = d;
        else t->csi_cur = (int16_t)(t->csi_cur * 10 + d);
        return;
      }
      if (b == ';') {
        csi_push(t);
        return;
      }
      // final
      // If we had digits but no ';', push the last one
      if (t->csi_cur >= 0) csi_push(t);
      csi_apply(t, b);
      t->st = VTT_ST_NORMAL;
      return;
  }
}

// -------------------------------
// 7xtgv backend
// -------------------------------
#ifdef VTT_BACKEND_7XTGV

// Externals expected from your 7xtgv code:
extern uint8_t paperRAM[TXT_ROWS * TXT_COLS];
extern uint8_t charRAM[TXT_ROWS * TXT_COLS];
extern uint8_t attrRAM[TXT_ROWS * TXT_COLS];

// Your 7xtgv helper (already in demo header):
// static inline void setCell(int cx, int cy, uint8_t paperGlyph, uint8_t inkGlyph,
//                            uint8_t glyphset2, uint8_t paper2, uint8_t ink4);

#ifndef VTT7_HAVE_SETCELL
// If your translation unit sees setCell as static inline, include this file in the same TU
// or define VTT7_HAVE_SETCELL and provide a non-static prototype.
#endif

typedef struct vtt7_ctx_t {
  uint16_t cx, cy;
  uint8_t glyphset2;
  uint8_t paper_glyph;   // usually 0x20
  uint8_t ink4;          // 0..15
  uint8_t paper2;        // 0..3
  uint8_t bold;
  uint8_t underline;     // currently unused by cell format
  uint8_t inverse;

  const uint8_t* fg_map;
  const uint8_t* bg_map;
} vtt7_ctx_t;

const uint8_t vtt7_default_fg_map[8] = {
  /*black*/ 1, /*red*/ 2, /*green*/ 4, /*yellow*/ 14,
  /*blue*/ 6, /*magenta*/ 12, /*cyan*/ 10, /*white*/ 0
};
// Background is only 2-bit in your demo (0..3). Map 8 ANSI bgs into 4 papers.
const uint8_t vtt7_default_bg_map[8] = {
  /*black*/ 0, /*red*/ 1, /*green*/ 2, /*yellow*/ 0,
  /*blue*/ 3, /*magenta*/ 1, /*cyan*/ 2, /*white*/ 0
};

static uint16_t vtt7_cols(void* user) { (void)user; return (uint16_t)TXT_COLS; }
static uint16_t vtt7_rows(void* user) { (void)user; return (uint16_t)TXT_ROWS; }

static void vtt7_set_cursor(void* user, uint16_t x, uint16_t y) {
  vtt7_ctx_t* c = (vtt7_ctx_t*)user;
  if (x >= (uint16_t)TXT_COLS) x = (uint16_t)TXT_COLS - 1;
  if (y >= (uint16_t)TXT_ROWS) y = (uint16_t)TXT_ROWS - 1;
  c->cx = x; c->cy = y;
}
static void vtt7_get_cursor(void* user, uint16_t* x, uint16_t* y) {
  vtt7_ctx_t* c = (vtt7_ctx_t*)user;
  if (x) *x = c->cx;
  if (y) *y = c->cy;
}

static inline void vtt7_write_cell(vtt7_ctx_t* c, uint16_t x, uint16_t y, uint8_t ch) {
  // underline not supported in this 7xtgv demo format; you can map underline->different font later.
  uint8_t paper2 = c->paper2;
  uint8_t ink4   = c->ink4;

  if (c->inverse) {
    uint8_t tmp = ink4;
    ink4 = (uint8_t)((paper2 & 3u) ? 0 : 0); // simplistic: keep ink as-is; paper is separate in your model
    paper2 = (uint8_t)(c->paper2);          // keep paper; your rendering already inverts via paper glyph fill
    (void)tmp;
  }

  // Bold: optionally map to "bold" variant by ORing 1 (if your palette is arranged that way).
  // Feel free to change this mapping; it's intentionally conservative.
  if (c->bold) {
    if ((ink4 & 1u) == 0 && ink4 != 0) ink4 = (uint8_t)(ink4 | 1u);
  }

  // Note: paper glyph is kept as space (inversion fill trick), ink glyph is ch.
  // setCell is expected visible in the same compilation unit as your demo header.
  extern void vtt7_setCell_bridge(int cx, int cy, uint8_t paperGlyph, uint8_t inkGlyph,
                                 uint8_t glyphset2, uint8_t paper2, uint8_t ink4);
  vtt7_setCell_bridge((int)x, (int)y, c->paper_glyph, ch, c->glyphset2, paper2, ink4);
}

// We provide a weak bridge symbol you can implement as a one-liner calling your static inline setCell.
// This avoids linkage problems when setCell is static inline in a header.
__attribute__((weak))
void vtt7_setCell_bridge(int cx, int cy, uint8_t paperGlyph, uint8_t inkGlyph,
                         uint8_t glyphset2, uint8_t paper2, uint8_t ink4)
{
  (void)cx; (void)cy; (void)paperGlyph; (void)inkGlyph; (void)glyphset2; (void)paper2; (void)ink4;
  // Implement this in *your* .ino/.cpp (same TU as setCell) as:
  //   extern "C" void vtt7_setCell_bridge(...) { setCell(cx,cy,paperGlyph,inkGlyph,glyphset2,paper2,ink4); }
}

static void vtt7_scroll_up(void* user, uint16_t lines) {
  vtt7_ctx_t* c = (vtt7_ctx_t*)user;
  if (lines == 0) return;
  if (lines >= (uint16_t)TXT_ROWS) lines = (uint16_t)TXT_ROWS;

  uint16_t cols = (uint16_t)TXT_COLS;
  uint16_t rows = (uint16_t)TXT_ROWS;

  uint16_t move_rows = (uint16_t)(rows - lines);
  if (move_rows) {
    memmove(paperRAM, paperRAM + lines*cols, move_rows*cols);
    memmove(charRAM,  charRAM  + lines*cols, move_rows*cols);
    memmove(attrRAM,  attrRAM  + lines*cols, move_rows*cols);
  }
  // Clear bottom lines
  for (uint16_t y = move_rows; y < rows; y++) {
    for (uint16_t x = 0; x < cols; x++) {
      uint16_t idx = y*cols + x;
      paperRAM[idx] = c->paper_glyph;
      charRAM[idx]  = 0x20;
      attrRAM[idx]  = (uint8_t)(((c->glyphset2 & 3) << 6) | ((c->paper2 & 3) << 4) | (c->ink4 & 0x0F));
    }
  }
  // Keep cursor inside
  if (c->cy >= lines) c->cy = (uint16_t)(c->cy - lines);
  else c->cy = 0;
}

static void vtt7_clear_screen(void* user) {
  vtt7_ctx_t* c = (vtt7_ctx_t*)user;
  uint16_t cols = (uint16_t)TXT_COLS, rows=(uint16_t)TXT_ROWS;
  for (uint32_t i = 0; i < (uint32_t)cols*(uint32_t)rows; i++) {
    paperRAM[i] = c->paper_glyph;
    charRAM[i]  = 0x20;
    attrRAM[i]  = (uint8_t)(((c->glyphset2 & 3) << 6) | ((c->paper2 & 3) << 4) | (c->ink4 & 0x0F));
  }
  c->cx = 0; c->cy = 0;
}

static void vtt7_erase_eol(void* user) {
  vtt7_ctx_t* c = (vtt7_ctx_t*)user;
  uint16_t cols = (uint16_t)TXT_COLS;
  uint16_t x = c->cx, y=c->cy;
  for (; x < cols; x++) {
    uint16_t idx = y*cols + x;
    paperRAM[idx] = c->paper_glyph;
    charRAM[idx]  = 0x20;
    attrRAM[idx]  = (uint8_t)(((c->glyphset2 & 3) << 6) | ((c->paper2 & 3) << 4) | (c->ink4 & 0x0F));
  }
}

static void vtt7_erase_eos(void* user) {
  vtt7_ctx_t* c = (vtt7_ctx_t*)user;
  uint16_t cols = (uint16_t)TXT_COLS, rows=(uint16_t)TXT_ROWS;
  uint16_t x0 = c->cx, y0=c->cy;

  // current line from cursor
  for (uint16_t x = x0; x < cols; x++) {
    uint16_t idx = y0*cols + x;
    paperRAM[idx] = c->paper_glyph;
    charRAM[idx]  = 0x20;
    attrRAM[idx]  = (uint8_t)(((c->glyphset2 & 3) << 6) | ((c->paper2 & 3) << 4) | (c->ink4 & 0x0F));
  }
  // remaining lines
  for (uint16_t y = (uint16_t)(y0 + 1); y < rows; y++) {
    for (uint16_t x = 0; x < cols; x++) {
      uint16_t idx = y*cols + x;
      paperRAM[idx] = c->paper_glyph;
      charRAM[idx]  = 0x20;
      attrRAM[idx]  = (uint8_t)(((c->glyphset2 & 3) << 6) | ((c->paper2 & 3) << 4) | (c->ink4 & 0x0F));
    }
  }
}

static void vtt7_putch(void* user, uint8_t ch) {
  vtt7_ctx_t* c = (vtt7_ctx_t*)user;
  uint16_t cols = (uint16_t)TXT_COLS, rows=(uint16_t)TXT_ROWS;

  // Handle CR/LF here too (some callers prefer raw mode)
  if (ch == '\r') { c->cx = 0; return; }
  if (ch == '\n') {
    c->cx = 0;
    c->cy++;
    if (c->cy >= rows) { vtt7_scroll_up(user, 1); c->cy = (uint16_t)(rows - 1); }
    return;
  }

  vtt7_write_cell(c, c->cx, c->cy, ch);

  c->cx++;
  if (c->cx >= cols) {
    c->cx = 0;
    c->cy++;
    if (c->cy >= rows) {
      vtt7_scroll_up(user, 1);
      c->cy = (uint16_t)(rows - 1);
    }
  }
}

static void vtt7_set_attr(void* user, uint8_t bold, uint8_t underline, uint8_t inverse,
                          uint8_t fg8, uint8_t bg8)
{
  vtt7_ctx_t* c = (vtt7_ctx_t*)user;
  c->bold = bold;
  c->underline = underline;
  c->inverse = inverse;

  const uint8_t* fg = c->fg_map ? c->fg_map : vtt7_default_fg_map;
  const uint8_t* bg = c->bg_map ? c->bg_map : vtt7_default_bg_map;

  uint8_t ink4 = fg[fg8 & 7u];
  uint8_t paper2 = bg[bg8 & 7u];

  // Inverse: swap logical fg/bg as best we can with 2-bit paper
  if (inverse) {
    // best-effort: use a contrasting paper for "fg" and a neutral ink
    // You can make this smarter in your theme layer.
    uint8_t p2 = paper2;
    paper2 = (uint8_t)(ink4 & 3u);
    ink4 = (uint8_t)((p2 == 0) ? 0 : 1); // pick grey/black-ish
  }

  c->ink4 = ink4 & 0x0F;
  c->paper2 = paper2 & 3u;
}

vtt_backend_t vtt_make_backend_7xtgv(uint8_t glyphset2, uint8_t paper_glyph,
                                     const uint8_t fg_map[8], const uint8_t bg_map[8])
{
  static vtt7_ctx_t ctx; // one instance (simple). If you need multiple, allocate yourself.
  memset(&ctx, 0, sizeof(ctx));
  ctx.glyphset2 = (uint8_t)(glyphset2 & 3u);
  ctx.paper_glyph = paper_glyph;
  ctx.cx = 0; ctx.cy = 0;
  ctx.fg_map = fg_map ? fg_map : vtt7_default_fg_map;
  ctx.bg_map = bg_map ? bg_map : vtt7_default_bg_map;
  ctx.ink4 = ctx.fg_map[7];
  ctx.paper2 = ctx.bg_map[0];

  vtt_backend_t be;
  memset(&be, 0, sizeof(be));
  be.user = &ctx;
  be.cols = vtt7_cols;
  be.rows = vtt7_rows;
  be.set_cursor = vtt7_set_cursor;
  be.get_cursor = vtt7_get_cursor;
  be.putch = vtt7_putch;
  be.erase_eol = vtt7_erase_eol;
  be.erase_eos = vtt7_erase_eos;
  be.clear_screen = vtt7_clear_screen;
  be.scroll_up = vtt7_scroll_up;
  be.set_attr = vtt7_set_attr;
  return be;
}

#endif // VTT_BACKEND_7XTGV
