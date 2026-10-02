#pragma once
#include <Arduino.h>
#include <algorithm>
#include <array>
#include <assert.h>
#include <stdint.h>
class Arduino_DataBus { public: virtual ~Arduino_DataBus() = default; };
inline Arduino_DataBus *gfxTestBus = nullptr;
inline std::array<int, 5> gfxTestSpiPins{};
inline std::array<int, 9> gfxTestPanel{};
constexpr int GFX_NOT_DEFINED = -1;
inline bool gfxBeginOK = true;
class Arduino_RPiPicoSPI : public Arduino_DataBus {
  public:
    Arduino_RPiPicoSPI(int dc, int cs, int sck, int mosi, int miso) {
        gfxTestBus = this;
        gfxTestSpiPins = {dc, cs, sck, mosi, miso};
    }
};
class Arduino_GFX {
  public:
    virtual ~Arduino_GFX() = default;
    uint16_t framebuffer[320 * 240] = {};
    unsigned flushCount = 0;
    bool begin() { assert(backlight == 0); return gfxBeginOK; }
    void flush(bool = false) { ++flushCount; }
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
    Arduino_ST7789(Arduino_DataBus *, int rst, int rot, int ips, int w, int h,
                   int x0, int y0, int x1, int y1) {
        gfxTestPanel = {rst, rot, ips, w, h, x0, y0, x1, y1};
    }
};

class Arduino_ESP32QSPI : public Arduino_DataBus {
public:
    Arduino_ESP32QSPI(int, int, int, int, int, int) { gfxTestBus = this; }
};
class Arduino_AXS15231B : public Arduino_GFX {
public:
    Arduino_AXS15231B(Arduino_DataBus *, int, int, bool, int, int) {}
};
class Arduino_Canvas : public Arduino_GFX {
public:
    Arduino_Canvas(int, int, Arduino_GFX *panel, int, int, int) : panel_(panel) {}
    ~Arduino_Canvas() override { delete panel_; }
private:
    Arduino_GFX *panel_;
};
class Arduino_ESP32RGBPanel {
public:
    template <typename... Args> Arduino_ESP32RGBPanel(Args...) {}
};
struct lcd_init_cmd_t {
    int cmd;
    const void *data;
    size_t data_bytes;
    unsigned int delay_ms;
};
class Arduino_ESP32DSIPanel {
public:
    Arduino_ESP32DSIPanel(int, int, int, int, int, int, int, int) {}
};
class Arduino_DSI_Display : public Arduino_GFX {
public:
    Arduino_DSI_Display(int w, int h, Arduino_ESP32DSIPanel *panel,
                        int rotation, bool flush, int rst,
                        const lcd_init_cmd_t *commands, size_t count)
        : panel_(panel) {
        assert(w == 800 && h == 1280 && rotation == 1 && !flush && rst == 27);
        assert(commands && count > 100);
    }
    ~Arduino_DSI_Display() override { delete panel_; }
private:
    Arduino_ESP32DSIPanel *panel_;
};
class Arduino_RGB_Display : public Arduino_GFX {
public:
    // Keep this pixel fixture 320x240; board construction is checked separately.
    Arduino_RGB_Display(int, int, Arduino_ESP32RGBPanel *panel, int, bool)
        : panel_(panel) {}
    ~Arduino_RGB_Display() override { delete panel_; }
private:
    Arduino_ESP32RGBPanel *panel_;
};
