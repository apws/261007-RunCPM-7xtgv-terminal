#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// ===========================
// CONFIG
// ===========================
#define CELL_W 6
#define CELL_H 9

// 320x172 with 6x9 cells -> 52x19 would overflow vertically (19*9=171 fits actually, leaving 1px)
// You said ~52x18; keep 18 for margin / status row.
#define SCR_COLS 52
#define SCR_ROWS 18

// ===========================
// Arduino_GFX ST7789 init (EDIT PINS)
// ===========================
static const int8_t TFT_DC  = 8;
static const int8_t TFT_CS  = 10;
static const int8_t TFT_RST = 9;

Arduino_DataBus* bus = nullptr;
Arduino_GFX*     gfx = nullptr;

// ===========================
// Compat wrapper (Arduino_GFX only here)
// ===========================
class GfxCompat {
public:
  explicit GfxCompat(Arduino_GFX* g) : _g(g) {}
  void beginWrite() { _g->startWrite(); }
  void endWrite()   { _g->endWrite(); }
  void blit565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* pixels) {
    _g->draw16bitRGBBitmap(x, y, (uint16_t*)pixels, w, h);
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
static constexpr uint16_t CELL_PIXELS = (uint16_t)CELL_W * (uint16_t)CELL_H;
static uint16_t cellBuf[CELL_PIXELS];

// Your palette LUT (256 entries) – for theming/gamma later.
// For now we only populate the few indices we use.
static uint16_t palette256[256];

// Screen RAM
static uint8_t paperRAM[SCR_ROWS * SCR_COLS]; // paper glyph code (0..255)
static uint8_t charRAM[SCR_ROWS * SCR_COLS];  // ink glyph code   (0..255)
static uint8_t attrRAM[SCR_ROWS * SCR_COLS];  // GGPPIIII: glyphset, paper color, ink color

// Font “ROM”: 4 glyphsets × 256 chars × CELL_H rows, each row is 1 byte with bits in 7..(8-CELL_W)
static uint8_t font1bpp[4][256][CELL_H];

// ===========================
// Color helpers + palette indices
// ===========================
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// Reserve some palette slots for key theme colors.
// (You can later remap these indices however you like.)
enum : uint8_t {
  PAL_DEEPBLACK = 0,

  // Paper colors (4)
  PAL_PAPER_WHITE  = 1,
  PAL_PAPER_PRED   = 2,
  PAL_PAPER_PGREEN = 3,
  PAL_PAPER_PBLUE  = 4,

  // Ink colors (16)
  PAL_INK_GRAY        = 16,
  PAL_INK_DARKGRAY    = 17,
  PAL_INK_RED         = 18,
  PAL_INK_DARKRED     = 19,
  PAL_INK_GREEN       = 20,
  PAL_INK_DARKGREEN   = 21,
  PAL_INK_BLUE        = 22,
  PAL_INK_DARKBLUE    = 23,
  PAL_INK_SILVER      = 24,
  PAL_INK_DARKSILVER  = 25,
  PAL_INK_CYAN        = 26,
  PAL_INK_DARKCYAN    = 27,
  PAL_INK_MAGENTA     = 28,
  PAL_INK_DARKMAGENTA = 29,
  PAL_INK_YELLOW      = 30,
  PAL_INK_DARKYELLOW  = 31,
};

static void initPalette256() {
  // Default everything to deep black for now.
  for (int i = 0; i < 256; i++) palette256[i] = 0x0000;

  // Deep black
  palette256[PAL_DEEPBLACK] = 0x0000;

  // Paper palette: BRIGHT WHITE + pastel R/G/B
  palette256[PAL_PAPER_WHITE]  = rgb565(255, 255, 255);
  palette256[PAL_PAPER_PRED]   = rgb565(255, 180, 180);
  palette256[PAL_PAPER_PGREEN] = rgb565(180, 255, 180);
  palette256[PAL_PAPER_PBLUE]  = rgb565(180, 180, 255);

  // Ink palette: 8 chromas × 2 luminances + extra grays (as you described)
  // “GRAY / DARKGRAY”
  palette256[PAL_INK_GRAY]     = rgb565(160, 160, 160);
  palette256[PAL_INK_DARKGRAY] = rgb565( 80,  80,  80);

  // RED / DARKRED
  palette256[PAL_INK_RED]     = rgb565(255,  80,  80);
  palette256[PAL_INK_DARKRED] = rgb565(140,  30,  30);

  // GREEN / DARKGREEN
  palette256[PAL_INK_GREEN]     = rgb565( 80, 255,  80);
  palette256[PAL_INK_DARKGREEN] = rgb565( 30, 140,  30);

  // BLUE / DARKBLUE
  palette256[PAL_INK_BLUE]     = rgb565( 80,  80, 255);
  palette256[PAL_INK_DARKBLUE] = rgb565( 30,  30, 140);

  // SILVER / DARKSILVER (treat silver as lighter gray)
  palette256[PAL_INK_SILVER]     = rgb565(210, 210, 210);
  palette256[PAL_INK_DARKSILVER] = rgb565(110, 110, 110);

  // CYAN / DARKCYAN
  palette256[PAL_INK_CYAN]     = rgb565( 80, 255, 255);
  palette256[PAL_INK_DARKCYAN] = rgb565( 30, 140, 140);

  // MAGENTA / DARKMAGENTA
  palette256[PAL_INK_MAGENTA]     = rgb565(255,  80, 255);
  palette256[PAL_INK_DARKMAGENTA] = rgb565(140,  30, 140);

  // YELLOW / DARKYELLOW
  palette256[PAL_INK_YELLOW]     = rgb565(255, 255,  80);
  palette256[PAL_INK_DARKYELLOW] = rgb565(140, 140,  30);
}

static inline uint8_t paperPalIndexFrom2(uint8_t p2) {
  switch (p2 & 3) {
    default:
    case 0: return PAL_PAPER_WHITE;
    case 1: return PAL_PAPER_PRED;
    case 2: return PAL_PAPER_PGREEN;
    case 3: return PAL_PAPER_PBLUE;
  }
}

static inline uint8_t inkPalIndexFrom4(uint8_t i4) {
  // map 0..15 to the 16 PAL_INK_* entries above
  static const uint8_t map16[16] = {
    PAL_INK_GRAY, PAL_INK_DARKGRAY,
    PAL_INK_RED, PAL_INK_DARKRED,
    PAL_INK_GREEN, PAL_INK_DARKGREEN,
    PAL_INK_BLUE, PAL_INK_DARKBLUE,
    PAL_INK_SILVER, PAL_INK_DARKSILVER,
    PAL_INK_CYAN, PAL_INK_DARKCYAN,
    PAL_INK_MAGENTA, PAL_INK_DARKMAGENTA,
    PAL_INK_YELLOW, PAL_INK_DARKYELLOW
  };
  return map16[i4 & 0x0F];
}

// ===========================
// Demo font generator (placeholder)
// Replace this with your real 1024-char spritesheet later.
// ===========================
static uint8_t maskForCellW() { return (uint8_t)(0xFFu << (8 - CELL_W)); }

static void buildDemoFont() {
  uint8_t wmask = maskForCellW();

  for (uint8_t gs = 0; gs < 4; gs++) {
    for (uint16_t ch = 0; ch < 256; ch++) {
      for (uint8_t y = 0; y < CELL_H; y++) {
        // crude pattern, different per glyphset
        uint8_t v = (uint8_t)(ch ^ (gs * 0x5B) ^ (y * 0x37));
        // create some strokes
        uint8_t row = (uint8_t)((v & 0xE0) | ((v << 3) & 0x1C) | ((v >> 2) & 0x03));
        // baseline
        if (y == CELL_H - 1) row |= wmask;
        font1bpp[gs][(uint8_t)ch][y] = (uint8_t)(row & wmask);
      }
    }
  }
}

// ===========================
// Cell compositor
// ===========================
//
// Composition order:
//   1) fill cellBuf with deep black
//   2) inverted PAPER glyph (from paperRAM, with paper color)
//   3) normal  INK  glyph (from charRAM,  with ink color)
//
// Inversion: if glyph bit=0 -> draw paper color; if glyph bit=1 -> leave as-is (black or later ink)
//
static void composeCell565(uint8_t glyphset2, uint8_t paperGlyph, uint8_t inkGlyph,
                           uint8_t paper2, uint8_t ink4)
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
    uint16_t* out = cellBuf;
    for (uint8_t y = 0; y < CELL_H; y++) {
      uint8_t bits = (uint8_t)(font1bpp[gs][paperGlyph][y] & wmask);
      for (uint8_t x = 0; x < CELL_W; x++) {
        uint8_t m = (uint8_t)(1u << (7 - x));
        if ((bits & m) == 0) {
          out[x] = paper565;
        }
      }
      out += CELL_W;
    }
  }

  // 3) ink glyph (normal)
  // If bit is 1 -> paint ink color
  {
    uint16_t* out = cellBuf;
    for (uint8_t y = 0; y < CELL_H; y++) {
      uint8_t bits = (uint8_t)(font1bpp[gs][inkGlyph][y] & wmask);
      for (uint8_t x = 0; x < CELL_W; x++) {
        uint8_t m = (uint8_t)(1u << (7 - x));
        if (bits & m) {
          out[x] = ink565;
        }
      }
      out += CELL_W;
    }
  }
}

static void renderFullScreen(GfxCompat& g) {
  g.beginWrite();
  for (int cy = 0; cy < SCR_ROWS; cy++) {
    for (int cx = 0; cx < SCR_COLS; cx++) {
      const int idx = cy * SCR_COLS + cx;

      uint8_t attr = attrRAM[idx];
      uint8_t glyphset2 = (uint8_t)((attr >> 6) & 0x03);
      uint8_t paper2    = (uint8_t)((attr >> 4) & 0x03);
      uint8_t ink4      = (uint8_t)(attr & 0x0F);

      uint8_t paperGlyph = paperRAM[idx]; // IMPORTANT: paper is glyph index now
      uint8_t inkGlyph   = charRAM[idx];

      composeCell565(glyphset2, paperGlyph, inkGlyph, paper2, ink4);

      int16_t px = (int16_t)(cx * CELL_W);
      int16_t py = (int16_t)(cy * CELL_H);
      g.blit565(px, py, CELL_W, CELL_H, cellBuf);
    }
  }
  g.endWrite();
}

// ===========================
// Demo setup content
// ===========================
static inline void setCell(int cx, int cy, uint8_t paperGlyph, uint8_t inkGlyph,
                           uint8_t glyphset2, uint8_t paper2, uint8_t ink4)
{
  if ((unsigned)cx >= SCR_COLS || (unsigned)cy >= SCR_ROWS) return;
  int idx = cy * SCR_COLS + cx;
  paperRAM[idx] = paperGlyph;
  charRAM[idx]  = inkGlyph;
  attrRAM[idx]  = (uint8_t)(((glyphset2 & 3) << 6) | ((paper2 & 3) << 4) | (ink4 & 0x0F));
}

static void initScreenRAM() {
  // Default: paper glyph = space, ink glyph = space, attributes some defaults
  for (int i = 0; i < SCR_ROWS * SCR_COLS; i++) {
    paperRAM[i] = 0x20;  // paper glyph (inverted) - if your real font makes space empty, inversion will paint all paper.
    charRAM[i]  = 0x20;  // ink glyph
    attrRAM[i]  = (uint8_t)((0 << 6) | (0 << 4) | 1); // glyphset0, paper0, ink=DARKGRAY (index 1)
  }

  // A header band: use paper as full block (demo: 0xFF will look "busy" in our demo font)
  for (int x = 0; x < SCR_COLS; x++) {
    setCell(x, 0, 0x20, (uint8_t)('A' + (x % 26)), 0, (x & 3), (x & 0x0F));
  }

  // Fill the rest with varying attributes to prove mapping
  for (int y = 1; y < SCR_ROWS; y++) {
    for (int x = 0; x < SCR_COLS; x++) {
      uint8_t gs = (uint8_t)((x >> 4) & 3);
      uint8_t p2 = (uint8_t)(y & 3);
      uint8_t i4 = (uint8_t)(x & 0x0F);

      uint8_t paperGlyph = 0x20;                // space -> inversion paints paper across cell (good “paper fill”)
      uint8_t inkGlyph   = (uint8_t)((x + y) & 0xFF);

      setCell(x, y, paperGlyph, inkGlyph, gs, p2, i4);
    }
  }
}

// ===========================
// Arduino setup/loop
// ===========================
static uint32_t frameNo = 0;

void setup() {
  Serial.begin(115200);
  delay(50);

  initPalette256();
  buildDemoFont();
  initScreenRAM();

  bus = new Arduino_HWSPI(TFT_DC, TFT_CS);

  // NOTE: pick the correct geometry/offsets for *your* ST7789 320x172.
  // Many “weird” small ST7789 panels need col/row offsets; adjust if image is shifted.
  // If your panel is truly 320x172, it may be a “windowed” panel on a 320x240 controller.
  gfx = new Arduino_ST7789(bus, TFT_RST, 0 /*rotation*/, true /*IPS*/, 320, 172);
  gfx->begin();
  gfx->fillScreen(0x0000);

  gxp = new GfxCompat(gfx);
}

void loop() {
  frameNo++;

  // Tiny animated proof in row 0
  int x = (frameNo / 2) % SCR_COLS;
  setCell(x, 0, 0x20, (uint8_t)('0' + (frameNo % 10)), (frameNo >> 6) & 3, (frameNo >> 4) & 3, frameNo & 0x0F);

  renderFullScreen(*gxp);

  // Remove delay when benchmarking
  delay(16);
}
