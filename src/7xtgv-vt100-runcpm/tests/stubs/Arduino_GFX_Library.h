#pragma once
#include <algorithm>
#include <assert.h>
#include <stdint.h>
class Arduino_DataBus {};
class Arduino_RPiPicoSPI : public Arduino_DataBus {
  public:
    Arduino_RPiPicoSPI(int, int, int, int, int) {}
};
class Arduino_GFX {
  public:
    uint16_t framebuffer[320 * 240] = {};
    bool begin() { return true; }
    void flush(bool = false) {}
    int width() const { return 320; }
    int height() const { return 240; }
    void fillScreen(uint16_t c) { std::fill(framebuffer, framebuffer + 320 * 240, c); }
    void fillRect(int x, int y, int w, int h, uint16_t c) {
        assert(x >= 0 && y >= 0 && x + w <= 320 && y + h <= 240);
        for (int r = y; r < y + h; r++)
            for (int col = x; col < x + w; col++)
                framebuffer[r * 320 + col] = c;
    }
    void draw16bitRGBBitmap(int x, int y, uint16_t *p, int w, int h) {
        assert(x >= 0 && y >= 0 && x + w <= 320 && y + h <= 240);
        for (int r = 0; r < h; r++)
            for (int col = 0; col < w; col++)
                framebuffer[(y + r) * 320 + x + col] = p[r * w + col];
    }
};
class Arduino_ST7789 : public Arduino_GFX {
  public:
    Arduino_ST7789(Arduino_DataBus *, int, int, int, int, int, int, int, int, int) {}
};
