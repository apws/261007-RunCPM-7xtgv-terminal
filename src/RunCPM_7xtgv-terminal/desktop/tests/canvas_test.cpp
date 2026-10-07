#include <Arduino_GFX_Library.h>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

int main() {
    Arduino_Canvas canvas(1280, 800, nullptr);
    require(canvas.begin(), "allocate real 1280x800 canvas");
    require(canvas.width() == 1280 && canvas.height() == 800, "geometry");
    uint16_t *pixels = canvas.getFramebuffer();
    canvas.fillScreen(0x1234);
    for (size_t i = 0; i < 1280U * 800U; ++i)
        require(pixels[i] == 0x1234, "full RGB565 fill");
    canvas.fillRect(-2, -2, 4, 4, 0xf800);
    require(pixels[0] == 0xf800 && pixels[1281] == 0xf800, "clipped rectangle");
    require(pixels[2] == 0x1234, "clipping leaves neighbors unchanged");
    uint16_t bitmap[] = {0x001f, 0x07e0, 0xffff, 0x0000};
    canvas.draw16bitRGBBitmap(1278, 798, bitmap, 2, 2);
    require(pixels[798U * 1280U + 1278] == 0x001f, "bitmap top-left");
    require(pixels[799U * 1280U + 1279] == 0, "bitmap bottom-right");
    canvas.setRotation(1);
    require(canvas.width() == 800 && canvas.height() == 1280, "rotated geometry");
    canvas.drawPixel(0, 0, 0xabcd);
    require(pixels[1279] == 0xabcd, "rotation uses original physical buffer");
    canvas.setRotation(0);
    canvas.flush(); // no physical output, no SDL dependency
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < 1280U * 800U; ++i) {
        hash = (hash ^ uint8_t(pixels[i])) * 1099511628211ULL;
        hash = (hash ^ uint8_t(pixels[i] >> 8)) * 1099511628211ULL;
    }
    require(hash == 0xb0d514776ca9ac2fULL, "deterministic framebuffer hash");
    std::printf("Real Arduino_GFX 1.6.4 canvas: PASS; RGB565 hash=%016llx\n",
                static_cast<unsigned long long>(hash));
}
