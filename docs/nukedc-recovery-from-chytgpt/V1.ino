/*
  7xtgv-style cell text demo with a compat layer for:
    - Arduino_GFX  (fast path)
    - Adafruit_SPITFT / Adafruit_GFX (fast path if SPITFT; fallback otherwise)

  Cell renderer:
    - CELL_W x CELL_H pixels per character cell (opaque)
    - Glyph is 1bpp, left-aligned in an 8-bit row byte (supports widths <= 8)
    - Pixel = ink if glyph bit=1 else paper
*/

#include <Arduino.h>

// ===========================
// CONFIG: cell geometry
// ===========================
#ifndef CELL_W
  #define CELL_W 6      // try 6
#endif
#ifndef CELL_H
  #define CELL_H 9      // try 9 or 10
#endif

// Screen in cells (choose based on your panel resolution)
#ifndef SCR_COLS
  #define SCR_COLS 40
#endif
#ifndef SCR_ROWS
  #define SCR_ROWS 20
#endif

// ===========================
// BACKEND SELECT
// ===========================
// Exactly one of these should be 1.
#ifndef USE_ARDUINO_GFX
  #define USE_ARDUINO_GFX 1
#endif
#ifndef USE_ADAFRUIT_GFX
  #define USE_ADAFRUIT_GFX 0
#endif

#if (USE_ARDUINO_GFX + USE_ADAFRUIT_GFX) != 1
  #error "Select exactly one backend: set USE_ARDUINO_GFX=1 or USE_ADAFRUIT_GFX=1"
#endif

// ===========================
// Include selected backend
// ===========================
#if USE_ARDUINO_GFX
  #include <Arduino_GFX_Library.h>
#elif USE_ADAFRUIT_GFX
  #include <Adafruit_GFX.h>
  #include <Adafruit_SPITFT.h>
  // You must include the concrete driver you use, e.g.
  // #include <Adafruit_ST7789.h>
#endif

// ===========================
// Low-level compat interface
// ===========================
class GfxCompat {
public:
  virtual ~GfxCompat() {}
  virtual void beginWrite() = 0;
  virtual void endWrite() = 0;
  virtual void blit565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* pixels) = 0;
  virtual int16_t width() const = 0;
  virtual int16_t height() const = 0;
};

// -------- Arduino_GFX backend --------
#if USE_ARDUINO_GFX
class GfxCompat_ArduinoGFX : public GfxCompat {
public:
  explicit GfxCompat_ArduinoGFX(Arduino_GFX* gfx) : _gfx(gfx) {}

  void beginWrite() override { _gfx->startWrite(); }
  void endWrite() override   { _gfx->endWrite(); }

  void blit565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* pixels) override {
    // This typically does: set window + stream RGB565
    _gfx->draw16bitRGBBitmap(x, y, (uint16_t*)pixels, w, h);
  }

  int16_t width() const override  { return _gfx->width(); }
  int16_t height() const override { return _gfx->height(); }

private:
  Arduino_GFX* _gfx;
};
#endif

// -------- Adafruit backend --------
#if USE_ADAFRUIT_GFX
class GfxCompat_Adafruit : public GfxCompat {
public:
  // Prefer SPITFT for fast window+stream. If you only have Adafruit_GFX*,
  // you can pass nullptr for spitft and it will fall back to drawRGBBitmap.
  GfxCompat_Adafruit(Adafruit_GFX* gfx, Adafruit_SPITFT* spitft)
  : _gfx(gfx), _spitft(spitft) {}

  void beginWrite() override {
    if (_spitft) _spitft->startWrite();
    // else: no transaction primitive available at pure GFX level
  }

  void endWrite() override {
    if (_spitft) _spitft->endWrite();
  }

  void blit565(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* pixels) override {
    if (_spitft) {
      // Fast path: set window once + stream pixels
      _spitft->setAddrWindow(x, y, w, h);
      // writePixels expects 16-bit values in native endian; most SPI TFTs want MSB first on wire,
      // and the driver handles byte order.
      _spitft->writePixels((uint16_t*)pixels, (uint32_t)w * (uint32_t)h);
    } else {
      // Fallback: still works, may be slower depending on driver
      _gfx->drawRGBBitmap(x, y, (uint16_t*)pixels, w, h);
    }
  }

  int16_t width() const override  { return _gfx->width(); }
  int16_t height() const override { return _gfx->height(); }

private:
  Adafruit_GFX* _gfx;
  Adafruit_SPITFT* _spitft;
};
#endif

// ===========================
// 7xtgv-style RAM state
// ===========================
static constexpr uint16_t CELL_PIXELS = (uint16_t)CELL_W * (uint16_t)CELL_H;
static uint16_t cellBuf[CELL_PIXELS];

// 256-entry RGB565 palette
static uint16_t palette256[256];

// “subpalette” maps (paper 2-bit -> palette idx), (ink 4-bit -> palette idx)
static uint8_t paperMap[4];
static uint8_t inkMap[16];

// Screen RAM
static uint8_t charRAM[SCR_ROWS * SCR_COLS];   // 0..255
static uint8_t paperRAM[SCR_ROWS * SCR_COLS];  // 0..3 (2 bits)
static uint8_t attrRAM[SCR_ROWS * SCR_COLS];   // [7:6]=glyphset 0..3, [3:0]=ink 0..15

// Font “ROM”: 4 glyphsets × 256 chars × CELL_H rows, each row packed into 1 byte (supports widths<=8)
static uint8_t font1bpp[4][256][CELL_H];

// ===========================
// Helpers
// ===========================
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  // r,g,b are 0..255
  uint16_t R = (uint16_t)(r >> 3) & 0x1F;
  uint16_t G = (uint16_t)(g >> 2) & 0x3F;
  uint16_t B = (uint16_t)(b >> 3) & 0x1F;
  return (uint16_t)((R << 11) | (G << 5) | B);
}

static void buildDemoPalette() {
  // Simple RGB cube-ish palette for demo. Replace with your real 256 palette.
  for (int i = 0; i < 256; i++) {
    uint8_t r = (uint8_t)((i & 0xE0));          // 3 bits-ish
    uint8_t g = (uint8_t)((i & 0x1C) << 3);     // 3 bits-ish
    uint8_t b = (uint8_t)((i & 0x03) * 85);     // 2 bits -> 0,85,170,255
    palette256[i] = rgb565(r, g, b);
  }

  // Subpalette mapping (demo):
  // paper 0..3 -> palette indices
  paperMap[0] = 0x00; // black-ish
  paperMap[1] = 0xE0; // red-ish
  paperMap[2] = 0x1C; // green-ish
  paperMap[3] = 0xFF; // bright-ish

  // ink 0..15 -> palette indices (demo ramp)
  for (int i = 0; i < 16; i++) inkMap[i] = (uint8_t)(i * 17); // 0..255
}

static uint8_t makeDemoGlyphRow(uint8_t ch, uint8_t glyphset, uint8_t row) {
  // This makes a deterministic “techy” pattern so you can see glyphsets/inks/papers working.
  // Replace with your real font ROM later.
  uint8_t v = (uint8_t)(ch ^ (glyphset * 0x55) ^ (row * 0x33));
  // Keep only left CELL_W bits, aligned to bit 7..(8-CELL_W)
  // For readability, create a pseudo “vertical strokes” look:
  uint8_t pattern = (uint8_t)((v & 0xC0) | ((v << 2) & 0x30) | ((v << 4) & 0x0C) | ((v << 6) & 0x03));
  // Mask to width
  uint8_t mask = (uint8_t)(0xFF << (8 - CELL_W));
  return (uint8_t)(pattern & mask);
}

static void buildDemoFont() {
  for (uint8_t gs = 0; gs < 4; gs++) {
    for (uint16_t ch = 0; ch < 256; ch++) {
      for (uint8_t y = 0; y < CELL_H; y++) {
        // Put a simple “baseline” on last row for readability
        uint8_t rowBits = makeDemoGlyphRow((uint8_t)ch, gs, y);
        if (y == CELL_H - 1) rowBits |= (uint8_t)(0xFF << (8 - CELL_W));
        font1bpp[gs][(uint8_t)ch][y] = rowBits;
      }
    }
  }
}

static void putCharCell(int cx, int cy, uint8_t ch, uint8_t paper2, uint8_t glyphset2, uint8_t ink4) {
  if (cx < 0 || cx >= SCR_COLS || cy < 0 || cy >= SCR_ROWS) return;
  const int idx = cy * SCR_COLS + cx;
  charRAM[idx] = ch;
  paperRAM[idx] = (uint8_t)(paper2 & 0x03);
  attrRAM[idx] = (uint8_t)(((glyphset2 & 0x03) << 6) | (ink4 & 0x0F));
}

static void writeString(int cx, int cy, const char* s, uint8_t paper2, uint8_t glyphset2, uint8_t ink4) {
  while (*s && cx < SCR_COLS) {
    putCharCell(cx, cy, (uint8_t)*s, paper2, glyphset2, ink4);
    cx++;
    s++;
  }
}

static void composeCell565(uint8_t ch, uint8_t paper2, uint8_t glyphset2, uint8_t ink4) {
  const uint16_t paper565 = palette256[paperMap[paper2 & 0x03]];
  const uint16_t ink565   = palette256[inkMap[ink4 & 0x0F]];
  const uint8_t gs = (glyphset2 & 0x03);

  uint16_t* out = cellBuf;

  for (uint8_t y = 0; y < CELL_H; y++) {
    uint8_t bits = font1bpp[gs][ch][y]; // bits in 7..(8-CELL_W)
    for (uint8_t x = 0; x < CELL_W; x++) {
      // left aligned: test bit (7-x)
      uint8_t m = (uint8_t)(1u << (7 - x));
      *out++ = (bits & m) ? ink565 : paper565;
    }
  }
}

static void renderFullScreen(GfxCompat& gfx) {
  const int16_t pxW = (int16_t)(SCR_COLS * CELL_W);
  const int16_t pxH = (int16_t)(SCR_ROWS * CELL_H);

  // Clip to panel (simple)
  if (pxW > gfx.width() || pxH > gfx.height()) {
    // You can adjust SCR_COLS/SCR_ROWS or add clipping if needed
  }

  gfx.beginWrite();
  for (int cy = 0; cy < SCR_ROWS; cy++) {
    for (int cx = 0; cx < SCR_COLS; cx++) {
      const int idx = cy * SCR_COLS + cx;
      const uint8_t ch = charRAM[idx];
      const uint8_t paper2 = (uint8_t)(paperRAM[idx] & 0x03);
      const uint8_t attr = attrRAM[idx];
      const uint8_t glyphset2 = (uint8_t)((attr >> 6) & 0x03);
      const uint8_t ink4 = (uint8_t)(attr & 0x0F);

      composeCell565(ch, paper2, glyphset2, ink4);

      const int16_t x = (int16_t)(cx * CELL_W);
      const int16_t y = (int16_t)(cy * CELL_H);
      gfx.blit565(x, y, CELL_W, CELL_H, cellBuf);
    }
  }
  gfx.endWrite();
}

// ===========================
// Instantiate your real display objects here
// ===========================

#if USE_ARDUINO_GFX
// Example wiring for Arduino_GFX (you must adapt to your SPI pins / board):
// Many people use Arduino_GFX's helpers:
//   Arduino_DataBus *bus = new Arduino_HWSPI(...);
//   Arduino_GFX *gfx = new Arduino_ST7789(bus, ...);
//
// Below is a generic template — adjust constructor to your exact board/panel.

Arduino_DataBus* bus = nullptr;
Arduino_GFX* rawGfx = nullptr;
GfxCompat_ArduinoGFX* gfxCompat = nullptr;

static void initDisplay() {
  // ---- EDIT THESE FOR YOUR BOARD ----
  const int8_t TFT_DC  = 8;
  const int8_t TFT_CS  = 10;
  const int8_t TFT_RST = 9;

  bus = new Arduino_HWSPI(TFT_DC, TFT_CS);
  // Common ST7789 240x240/240x320 variants — pick correct one for your panel.
  // Arduino_ST7789(bus, rst, rotation, ips, width, height, col_offset1, row_offset1, col_offset2, row_offset2)
  rawGfx = new Arduino_ST7789(bus, TFT_RST, 0 /*rotation*/, true /*IPS*/, 240, 240);
  rawGfx->begin();
  rawGfx->fillScreen(BLACK);

  gfxCompat = new GfxCompat_ArduinoGFX(rawGfx);
}
#endif

#if USE_ADAFRUIT_GFX
// You must instantiate your concrete driver, e.g. Adafruit_ST7789 tft = ...
// Then wrap it with GfxCompat_Adafruit(&tft, &tft);

#endif

// ===========================
// Demo content
// ===========================
static uint32_t frameNo = 0;

static void initScreenRAM() {
  // Clear
  for (int i = 0; i < SCR_ROWS * SCR_COLS; i++) {
    charRAM[i] = (uint8_t)' ';
    paperRAM[i] = 0;
    attrRAM[i]  = 0;
  }

  // A header line with varying ink + glyphset
  writeString(0, 0, "7xtgv CELL DEMO 1024CH 256PAL", 0, 0, 15);

  // Fill some lines with patterns
  for (int y = 2; y < SCR_ROWS; y++) {
    for (int x = 0; x < SCR_COLS; x++) {
      uint8_t ch = (uint8_t)((x + y) & 0xFF);
      uint8_t paper2 = (uint8_t)(y & 0x03);
      uint8_t glyphset2 = (uint8_t)((x >> 4) & 0x03);
      uint8_t ink4 = (uint8_t)(x & 0x0F);
      putCharCell(x, y, ch, paper2, glyphset2, ink4);
    }
  }
}

static void animateOverlay() {
  // Put a counter in the top row, changing glyphset/ink/paper to prove attributes.
  char buf[32];
  snprintf(buf, sizeof(buf), "F=%lu", (unsigned long)frameNo);

  uint8_t paper2   = (uint8_t)((frameNo >> 4) & 0x03);
  uint8_t glyphset = (uint8_t)((frameNo >> 6) & 0x03);
  uint8_t ink4     = (uint8_t)(frameNo & 0x0F);

  // write at fixed place
  writeString(0, 1, "ATTR:", 0, 0, 14);
  writeString(6, 1, buf, paper2, glyphset, ink4);
}

// ===========================
// Arduino setup/loop
// ===========================
void setup() {
  Serial.begin(115200);
  delay(50);

  buildDemoPalette();
  buildDemoFont();
  initScreenRAM();

#if USE_ARDUINO_GFX
  initDisplay();
#elif USE_ADAFRUIT_GFX
  // init your Adafruit TFT here, then create gfxCompat
#endif
}

void loop() {
  frameNo++;
  animateOverlay();

#if USE_ARDUINO_GFX
  renderFullScreen(*gfxCompat);
#elif USE_ADAFRUIT_GFX
  // renderFullScreen(*gfxCompat);
#endif

  delay(16); // ~60 fps-ish demo pacing; remove when benchmarking
}
