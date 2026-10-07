//7xtgv specs - Copyright 2026 Petr Antos, ALL RIGHTS RESERVED (will be opened at release)

//Arduino_GFX with board/display specific setup
#include "7xtgv-gfxsetup.h"

// ===========================
// CONFIG
// ===========================

// QL font cell in pixels
#define CELL_W 6
#define CELL_H 10

// Integer pixel-zoom factor for the whole text grid.
// 1 = native 6x10 cells at 52x17.
// 2 = 12x18 cells at 26x8.
// 3/4 also supported (grid dimensions are floored).
#ifndef SCR_ZOOM
#define SCR_ZOOM 2
#endif

#if (SCR_ZOOM < 1) || (SCR_ZOOM > 4)
#error "SCR_ZOOM must be 1..4"
#endif

// Effective pixel dimensions of one rendered cell
#define CELL_WZ (CELL_W * SCR_ZOOM)
#define CELL_HZ (CELL_H * SCR_ZOOM)

// Logical text grid dimensions after zooming
#define TXT_COLS (SCR_COLS / SCR_ZOOM)
#define TXT_ROWS (SCR_ROWS / SCR_ZOOM)


// ===========================
// Compat wrapper (Arduino_GFX only here)
// ===========================
class GfxCompat
{
public:
  explicit GfxCompat(Arduino_GFX* g) : _g(g) {}
  void beginWrite() { _g->startWrite(); }
  void endWrite()   { _g->endWrite(); }
  void blit565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* pixels)
  {
    _g->draw16bitRGBBitmap(x + GFX_OFS_X, y + GFX_OFS_Y, (uint16_t*)pixels, w, h);
  }
  int16_t width() const { return _g->width(); }
  int16_t height() const { return _g->height(); }
private:
  Arduino_GFX* _g;
};

static GfxCompat* gxp = nullptr;


// ===========================
// 7xtgv-ish RAM
// ===========================
static constexpr uint16_t CELL_PIXELS = (uint16_t)CELL_WZ * (uint16_t)CELL_HZ;
static uint16_t cellBuf[CELL_PIXELS];

// Palette LUT (256 entries) for theming/gamma later.
// For now we only populate the few indices we use.
static uint16_t palette256[256];

// Screen RAM
static uint8_t paperRAM[TXT_ROWS * TXT_COLS]; // paper glyph code (0..255)
static uint8_t charRAM[TXT_ROWS * TXT_COLS];  // ink glyph code   (0..255)
static uint8_t attrRAM[TXT_ROWS * TXT_COLS];  // GGPPIIII: glyphset, paper color, ink color


// ===========================
// Color helpers + palette indices
// ===========================
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// Reserve some palette slots for key theme colors.
// (You can later remap these indices however you like.)
enum : uint8_t
{
  PAL_DEEPBLACK = 0,

  // Paper colors (4)
  PAL_PAPER_WHITE  = 1,
  PAL_PAPER_PRED   = 2,
  PAL_PAPER_PGREEN = 3,
  PAL_PAPER_PBLUE  = 4,

  // Ink colors (16)
  PAL_INK_GRAY        = 16,
  PAL_INK_DARKGRAY    = 24,
  PAL_INK_RED         = 17,
  PAL_INK_DARKRED     = 25,
  PAL_INK_GREEN       = 18,
  PAL_INK_DARKGREEN   = 26,
  PAL_INK_BLUE        = 19,
  PAL_INK_DARKBLUE    = 27,
  PAL_INK_SILVER      = 20,
  PAL_INK_DARKSILVER  = 28,
  PAL_INK_CYAN        = 21,
  PAL_INK_DARKCYAN    = 29,
  PAL_INK_MAGENTA     = 22,
  PAL_INK_DARKMAGENTA = 30,
  PAL_INK_YELLOW      = 23,
  PAL_INK_DARKYELLOW  = 31,
};

static void initPalette256()
{
  // Default everything to deep black for now.
  for (int i = 0; i < 256; i++) palette256[i] = 0x0000;

  // Deep black
  palette256[PAL_DEEPBLACK] = 0x0000;
  //meant as default background but will be configurable, may be also BLUE, etc
  //just separate solid color of DESKTOP ... otherwise, GRAM noise ...

  // Paper palette: BRIGHT WHITE + pastel R/G/B
  palette256[PAL_PAPER_WHITE]  = rgb565(255, 255, 255);
  palette256[PAL_PAPER_PRED]   = rgb565(255, 230, 235);
  palette256[PAL_PAPER_PGREEN] = rgb565(205, 255, 205);
  palette256[PAL_PAPER_PBLUE]  = rgb565(240, 240, 255);

  // Ink palette: 8 chromas - 2 luminances + extra grays
  // GRAY / DARKGRAY
  palette256[PAL_INK_GRAY]     = rgb565(100, 100, 100);
  palette256[PAL_INK_DARKGRAY] = rgb565(  0,   0,   0); //we forced here DEEP BLACK for BOLD KEYWORDS
  //NOTE - traditional retro terminals defined BOLD as BRIGHT, this was applied to negative display with dark background
  //     - on WHITE paper, the BOLD is defined as DAKER luminance of color, so in case of GRAY > VERY DARK GREY / BLACK

  // RED / DARKRED
  palette256[PAL_INK_RED]     = rgb565(255,   0,   0);
  palette256[PAL_INK_DARKRED] = rgb565(231,  37,  37);

  // GREEN / DARKGREEN
  palette256[PAL_INK_GREEN]     = rgb565(  0, 242,   0);
  palette256[PAL_INK_DARKGREEN] = rgb565( 21, 200,  47);

  // BLUE / DARKBLUE
  palette256[PAL_INK_BLUE]     = rgb565(  0,   0, 255);
  palette256[PAL_INK_DARKBLUE] = rgb565( 37,  89, 221);

  // SILVER / DARKSILVER (treat silver as lighter gray - total 4 GRAY levels)
  palette256[PAL_INK_SILVER]     = rgb565(230, 230, 230);
  palette256[PAL_INK_DARKSILVER] = rgb565(215, 215, 215);

  // CYAN / DARKCYAN
  palette256[PAL_INK_CYAN]     = rgb565( 76, 242, 242);
  palette256[PAL_INK_DARKCYAN] = rgb565(  0, 184, 194);

  // MAGENTA / DARKMAGENTA
  palette256[PAL_INK_MAGENTA]     = rgb565(255,  80, 255);
  palette256[PAL_INK_DARKMAGENTA] = rgb565(205,  47, 189);

  // YELLOW / DARKYELLOW
  palette256[PAL_INK_YELLOW]     = rgb565(230, 230,  50);
  palette256[PAL_INK_DARKYELLOW] = rgb565(200, 194,   0);

}

static inline uint8_t paperPalIndexFrom2(uint8_t p2)
{
  switch (p2 & 3) {
    default:
    case 0: return PAL_PAPER_WHITE;
    case 1: return PAL_PAPER_PRED;
    case 2: return PAL_PAPER_PGREEN;
    case 3: return PAL_PAPER_PBLUE;
  }
}

static inline uint8_t inkPalIndexFrom4(uint8_t i4)
{
  switch (i4 & 0x0F) {
    default:
    case  0: return PAL_INK_GRAY;
    case  8: return PAL_INK_DARKGRAY;
    case  1: return PAL_INK_RED;
    case  9: return PAL_INK_DARKRED;
    case  2: return PAL_INK_GREEN;
    case  10: return PAL_INK_DARKGREEN;
    case  3: return PAL_INK_BLUE;
    case  11: return PAL_INK_DARKBLUE;
    case  4: return PAL_INK_SILVER;
    case  12: return PAL_INK_DARKSILVER;
    case 5: return PAL_INK_CYAN;
    case 13: return PAL_INK_DARKCYAN;
    case 6: return PAL_INK_MAGENTA;
    case 14: return PAL_INK_DARKMAGENTA;
    case 7: return PAL_INK_YELLOW;
    case 15: return PAL_INK_DARKYELLOW;

  }
}


// ================+==============================
// 7xtgv (QL mixworx mod "*/\afrs") 6x10 font data
// ===============================================
static uint8_t maskForCellW() { return (uint8_t)(0xFFu << (8 - CELL_W)); }
#include "7xtgv-QL-font.h"


// ==============================================
// Cell compositor - prototype, will be optimized
// ==============================================
//
// IMPORTANT - stay in close to HW/CPLD/FPGA feasibility
//
// Composition order:
//   1) fill cellBuf with deep black
//   2) inverted PAPER glyph (from paperRAM, with paper color)
//   3) normal  INK  glyph (from charRAM,  with ink color)
//
// Inversion: if glyph bit=0 -> draw paper color; if glyph bit=1 -> leave as-is (black or later ink)
//
static void composeCell565(uint8_t glyphset2, uint8_t paperGlyph, uint8_t inkGlyph, uint8_t paper2, uint8_t ink4)
{
  const uint16_t deepBlack = palette256[PAL_DEEPBLACK];
  const uint16_t paper565  = palette256[paperPalIndexFrom2(paper2)];
  const uint16_t ink565    = palette256[inkPalIndexFrom4(ink4)];
  const uint8_t gs = (uint8_t)(glyphset2 & 0x03);
  const uint8_t wmask = maskForCellW();

  // 1) deep black
  for (uint16_t i = 0; i < CELL_PIXELS; i++) cellBuf[i] = deepBlack;

  // 2) inverted paper glyph
  // If bit is 0 -> paint paper color
  {
    for (uint8_t y = 0; y < CELL_H; y++) {
      uint8_t bits = (uint8_t)(font1bpp[gs][paperGlyph][y] & wmask);
      for (uint8_t x = 0; x < CELL_W; x++) {
        uint8_t m = (uint8_t)(1u << (7 - x));
        if ((bits & m) == 0) {
          // write zoomed pixel block
          const uint16_t y0 = (uint16_t)y * (uint16_t)SCR_ZOOM;
          const uint16_t x0 = (uint16_t)x * (uint16_t)SCR_ZOOM;
          for (uint8_t zy = 0; zy < SCR_ZOOM; zy++) {
            uint16_t* row = cellBuf + (y0 + zy) * (uint16_t)CELL_WZ + x0;
            for (uint8_t zx = 0; zx < SCR_ZOOM; zx++) row[zx] = paper565;
          }
        }
      }
    }
  }

  // 3) ink glyph (normal)
  // If bit is 1 -> paint ink color
  {
    for (uint8_t y = 0; y < CELL_H; y++) {
      uint8_t bits = (uint8_t)(font1bpp[gs][inkGlyph][y] & wmask);
      for (uint8_t x = 0; x < CELL_W; x++) {
        uint8_t m = (uint8_t)(1u << (7 - x));
        if (bits & m) {
          // write zoomed pixel block
          const uint16_t y0 = (uint16_t)y * (uint16_t)SCR_ZOOM;
          const uint16_t x0 = (uint16_t)x * (uint16_t)SCR_ZOOM;
          for (uint8_t zy = 0; zy < SCR_ZOOM; zy++) {
            uint16_t* row = cellBuf + (y0 + zy) * (uint16_t)CELL_WZ + x0;
            for (uint8_t zx = 0; zx < SCR_ZOOM; zx++) row[zx] = ink565;
          }
        }
      }
    }
  }
}

//TODO MIXWORX - sure, we now render ENTIRE SCREEN always - prototype
static void renderFullScreen(GfxCompat& g) {
  //g.beginWrite();
  for (int cy = 0; cy < TXT_ROWS; cy++) {
    for (int cx = 0; cx < TXT_COLS; cx++) {
      const int idx = cy * TXT_COLS + cx;

      uint8_t attr      = attrRAM[idx];
      uint8_t glyphset2 = (uint8_t)((attr >> 6) & 0x03);
      uint8_t paper2    = (uint8_t)((attr >> 4) & 0x03);
      uint8_t ink4      = (uint8_t)(attr & 0x0F);

      uint8_t paperGlyph = paperRAM[idx]; // IMPORTANT: paper is glyph index now
      uint8_t inkGlyph   = charRAM[idx];

      composeCell565(glyphset2, paperGlyph, inkGlyph, paper2, ink4);

      int16_t px = (int16_t)(cx * CELL_WZ);
      int16_t py = (int16_t)(cy * CELL_HZ);
      g.blit565(px, py, CELL_WZ, CELL_HZ, cellBuf);
    }
  }
  //g.endWrite();
}


// ===========================
// Demo setup content
// ===========================
static inline void setCell(int cx, int cy, uint8_t paperGlyph, uint8_t inkGlyph, uint8_t glyphset2, uint8_t paper2, uint8_t ink4)
{
  if ((unsigned)cx >= TXT_COLS || (unsigned)cy >= TXT_ROWS) return;
  int idx = cy * TXT_COLS + cx;
  paperRAM[idx] = paperGlyph;
  charRAM[idx]  = inkGlyph;
  attrRAM[idx]  = (uint8_t)(((glyphset2 & 3) << 6) | ((paper2 & 3) << 4) | (ink4 & 0x0F));
}

static void initScreenRAM()
{
  // Default: paper glyph = space, ink glyph = space, attributes some defaults
  for (int i = 0; i < TXT_ROWS * TXT_COLS; i++)
  {
    paperRAM[i] = 0x20;  // paper glyph (inverted) - SPACE paints paper color //TBD specs in progress, still
    charRAM[i]  = 0x20;  // ink glyph - SPACE paints nothing
    attrRAM[i]  = 0;     // ink GRAY, paper WHITE, gs 0   //(uint8_t)((0 << 6) | (0 << 4) | 1); // glyphset0, paper0, ink=DARKGRAY (index 8)
  }
}


// ===========================
// Minimal "QL-ish" compat API
// ===========================
//
// IMPORTANT - this will be here, needs polish, on par with QL SBASIC features
//           - we CAN also support defined WINDOWS regions as QL, its usefull
//
// Methods requested:
//   paper(n) : 0..3
//   ink(n)   : 0..15
//   at(x,y)
//   cls()
//   print(s) : no wrapping; stops at right/bottom edge
//
class QLCompat
{
public:
  void font(uint8_t n)  { _font = (uint8_t)(n & 3); }
  void paper(uint8_t n) { _paper = (uint8_t)(n & 3); }
  void ink(uint8_t n)   { _ink   = (uint8_t)(n & 0x0F); }

  void at(uint8_t x, uint8_t y)
  {
    _cx = (uint8_t)((x < TXT_COLS) ? x : (TXT_COLS - 1));
    _cy = (uint8_t)((y < TXT_ROWS) ? y : (TXT_ROWS - 1));
  }

  void cls()
  {
    for (int i = 0; i < TXT_ROWS * TXT_COLS; i++)
    {
      paperRAM[i] = 0x20;
      charRAM[i]  = 0x20;
      attrRAM[i]  = (uint8_t)((_font << 6) | ((_paper & 3u) << 4) | (_ink & 0x0Fu));
    }
    _cx = 0;
    _cy = 0;
  }

  void print(const char* s)
  {
    if (!s) return;
    while (*s)
    {
      // Stop at screen edge (no wrap for now)
      if (_cy >= TXT_ROWS) break;
      if (_cx >= TXT_COLS) break;

      uint8_t c = (uint8_t)(*s++);

      // Basic control chars
      if (c == '\r') { _cx = 0; continue; }
      if (c == '\n') { _cx = 0; _cy++; continue; }

      const int idx = (int)_cy * (int)TXT_COLS + (int)_cx;
      paperRAM[idx] = 0x20; // space -> inversion paints paper fill
      charRAM[idx]  = c;
      attrRAM[idx]  = (uint8_t)((_font << 6) | ((_paper & 3u) << 4) | (_ink & 0x0Fu));

      _cx++;
    }
  }

  // Optional helpers (handy for demos)
  uint8_t curX() const { return _cx; }
  uint8_t curY() const { return _cy; }

private:
  uint8_t _font  = 0;
  uint8_t _paper = 0;
  uint8_t _ink   = 0;
  uint8_t _cx    = 0;
  uint8_t _cy    = 0;
};

static QLCompat ql;


// 7xtgv colors are devoted to "mixworx semantic syntax coloring"
// - expected VT52 + ANSI SGR style control in terminal mode for CP/M, FUZIX ...
// - retro terminal closest is probably VT340 in TEXT mode (sixel = question)
// - full 7xtgv will have 4 modes (related to separate GLYPHSET and PALLETES)
// - TEXT mode, mixworx semantic syntax coloring editors
// - INST mode, mixworx instrumentation panels glyphs, devices UI, 7-seg, LEDs, meters, knobs, buttons
// - SEMI mode, mixworx semigraphics having 6x10 cell converted to 2x5 pixels with paper/ink
// - WIND mode, mixworx windows chrome glyphs, nice polished set for TUI
//   - all these modes have separate COLORS AND PALLETES handling, it must stay HW-simple though
//   - but generally, here according to 2bit MODE selection in ATTR, rest of ATTR 2+4bits interpretation changes

//for now, simulated TEXT pastel papers/markers, devoted for syntax coloring/editors
enum PaperColor {
    P_W = 0,   // White (bright)
    P_R = 1,   // Red   (very light/pastel) - MARKER
    P_G = 2,   // Green (very light/pastel) - CURSOR
    P_B = 3    // Blue  (very light/pastel) - MARKER
};

//for now, simulated TEXT normal and bold inks, devoted for syntax coloring/editors
//ordering herre doesnt match to ANSI SGR, intentionally, for now
// bit0 defines normal/bold simulating color luminance
enum InkColor {
    I_T  = 0,   // Text Grey - (visibly lighter)
    I_TB = 8,   // Text Bold - Dark Grey (actually DEEP BLACK)

    I_R  = 1,   // Red
    I_RB = 9,   // Dark/Bold Red

    I_G  = 2,  // Green
    I_GB = 10,  // Dark/Bold Green

    I_B  = 3,   // Blue
    I_BB = 11,   // Dark/Bold Blue

    I_S  = 4,   // Silver Grey (very light)
    I_SB  = 12,  // Dark/Bold Silver

    I_C  = 5,  // Cyan
    I_CB = 13,  // Dark/Bold Cyan

    I_M  = 6,  // Magenta
    I_MB = 14,  // Dark/Bold Magenta

    I_Y  = 7,  // Yellow
    I_YB = 15   // Dark/Bold Yellow
};


// ===========================
// Demo: random "7xtgv cheers"
// ===========================
static void demoRandomCheers()
{
  static const char msg[] = " 7xtgv ";
  constexpr uint8_t msgLen = (uint8_t)(sizeof(msg) - 1);

  // random position (skip first line)
  uint8_t y = (TXT_ROWS > 1) ? (uint8_t)random(1, TXT_ROWS) : 0;
  uint8_t xMax = (TXT_COLS > msgLen) ? (uint8_t)(TXT_COLS - msgLen) : 0;
  uint8_t x = (xMax > 0) ? (uint8_t)random(0, (int)xMax + 1) : 0;

  uint8_t p2 = (uint8_t)random(0, 4);
  uint8_t i4 = (uint8_t)random(0, 16);
  uint8_t gs = 0;

  for (uint8_t i = 0; i < msgLen; i++) {
    setCell((int)(x + i), (int)y, 0x20, (uint8_t)msg[i], gs, p2, i4);
  }
}


// ===========================
// Arduino setup/loop
// ===========================
static uint32_t frameNo = 0;


// here, executed as DEMO using second core, toogether with USB-Serial RunCPM
// plan is to link both cores by VirtualSerial bidir channel ...
// PLUS this will handle ALSO mixworx extended ITU-T semantic keypad
//  - must be modern to support vmex/aslix
//  - must allow effective retro terminal features
//  - may be redefinable, but some things MUST be bound to fixed KEY STICKERS
//  - extended ITU-T is designed to be flexible, with MOST COMMON important keys (today)
//  - for sure, UNDO/REDO, COPY/PASTE, SAVE/VERSION, inv-T shifted cursor keys, BS/DEL/INS PGUP/PGDN, HOME/END

void setup1()
{
  GFX_DEVICE_INIT();

#ifdef GFX_BL
  //pinMode(GFX_BL, OUTPUT);
  //digitalWrite(GFX_BL, HIGH);

  pinMode(GFX_BL, OUTPUT);
  analogWrite(GFX_BL, 150); /* 0-255 */
#endif

  Serial.begin(115200);
  delay(50);
  randomSeed((uint32_t)micros());
  Serial.setDebugOutput(true);
  //while(!Serial);
  Serial.println("Arduino_GFX + compat - mixworx 7xtgv compositor example");

  initPalette256();
  initScreenRAM();

  // Initialize the controller before issuing fill/draw commands.
  if (!GFX_BEGIN_OK())
  {
    Serial.println("gfx->begin() failed!");
  }

  gfx->fillScreen(0x0000); //SOLID BLUE DESKTOP ???

  gxp = new GfxCompat(gfx);

}

void loop1() {
  frameNo++;

  // // Tiny animated proof in row 0
  // int x = (frameNo / 2) % TXT_COLS;
  // setCell(x, 0, 0x20, (uint8_t)('0' + (frameNo % 10)), (frameNo >> 6) & 3, (frameNo >> 4) & 3, frameNo & 0x0F);

  // Animated counter, top-right, with changing ink.
  char buf[32];
  snprintf(buf, sizeof(buf), "#%lu", (unsigned long)frameNo);
  ql.paper(0);
  ql.ink((uint8_t)(((frameNo >> 1) & 7) | 8));
  ql.at((uint8_t)((TXT_COLS > 8) ? (TXT_COLS - 8) : 0), 0);
  ql.print("        "); // clear old
  ql.at((uint8_t)((TXT_COLS > 8) ? (TXT_COLS - 8) : 0), 0);
  ql.print(buf);

  // Random colored text scatter (skip first line)
  demoRandomCheers();

  renderFullScreen(*gxp);

  // Remove delay when benchmarking
  //delay(1000);
}
