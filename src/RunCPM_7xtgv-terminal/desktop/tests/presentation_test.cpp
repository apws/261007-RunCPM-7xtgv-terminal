#include "../platform.h"
#include "../../hardware/arduino_7xtgv/board.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
static std::vector<uint16_t> published;
static void require(bool ok, const char *why) {
    if (!ok) { std::fprintf(stderr, "%s\n", why); std::exit(1); }
}
static void capture(XtgvDesktopCanvas &canvas) {
    published.assign(canvas.getFramebuffer(), canvas.getFramebuffer() + canvas.width() * canvas.height());
}
int main() {
    require(xtgvDesktopBrightness(0) == 0, "off must stay fully black");
    require(xtgvDesktopBrightness(XTGV_BACKLIGHT_DUTIES[31]) == 255, "board maximum must reach full SDL brightness");
    uint8_t previous = 0;
    for (unsigned duty = 1; duty < 256; ++duty) {
        const auto brightness = xtgvDesktopBrightness(uint8_t(duty));
        require(brightness >= previous && brightness >= duty, "brightness curve must lift and never reverse");
        previous = brightness;
    }
    require(xtgvDesktopBrightness(XTGV_BACKLIGHT_DUTIES[1]) > 0, "first lit level stays visible");
    XtgvDesktopCanvas canvas(32, 24);
    require(canvas.begin(), "Canvas allocation");
    canvas.publish = capture;
    canvas.fillScreen(0x1234); canvas.flush();
    const auto generation = canvas.generation();
    canvas.fillRect(0, 0, 4, 4, 0xffff);
    require(published[0] == 0x1234 && canvas.generation() == generation, "unflushed drawing leaked");
    canvas.flush();
    require(published[0] == 0xffff && published[31] == 0x1234, "flush did not publish exact image");
    uint16_t pixels[] = {0xf800, 0x07e0, 0x001f, 0xffff};
    canvas.draw16bitRGBBitmap(0, 0, pixels, 2, 2);
    canvas.publishDirect();
    require(published[0] == 0xf800 && published[32] == 0x001f, "direct bitmap did not publish");
    const auto settled = canvas.generation();
    canvas.publishDirect();
    require(canvas.generation() == settled, "idle direct mode republishes");
    xtgvDesktopSetBacklight(0);
    require(canvas.getFramebuffer()[0] == 0xf800 && published[0] == 0xf800, "backlight changed pixels");
    std::puts("Buffered visibility, direct bitmap, idle publication and backlight: PASS");
}
