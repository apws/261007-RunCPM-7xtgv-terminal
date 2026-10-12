#include "platform.h"
#include <emscripten.h>
#include <vector>
// libc and RunCPM both use this name for different purposes.
#undef PAGESIZE
#include "../../RunCPM_7xtgv-terminal.ino"

// Canvas2D only presents the real Arduino_GFX RGB565 framebuffer. All glyphs,
// terminal parsing, graphics commands and board geometry remain C++ code.
EM_JS(void, present, (const uint16_t *pixels, int width, int height, int brightness), {
    Module.presentFrame(HEAPU16.subarray(pixels >> 1, (pixels >> 1) + width * height),
                        width, height, brightness);
});
EM_JS(void, finished, (int code, const char *error), {
    Module.guestFinished(code, UTF8ToString(error));
});
static std::vector<uint16_t> published;
EM_JS(void, deliverSnapshot, (const char *text, int cols, int rows, int x, int y,
                            int viewX, int viewY, int viewCols, int viewRows), {
    Module.screenSnapshot({text: UTF8ToString(text), cols, rows, x, y,
                           viewX, viewY, viewCols, viewRows});
});
extern "C" EMSCRIPTEN_KEEPALIVE void web_snapshot() {
    const auto s = terminalScreenSnapshot();
    deliverSnapshot(s.text.c_str(), s.columns, s.rows, s.cursorColumn, s.cursorRow,
                    s.viewport, s.rowViewport, s.visibleColumns, s.visibleRows);
}
static void publish(XtgvDesktopCanvas &canvas) {
    const auto *pixels = canvas.getFramebuffer();
    published.assign(pixels, pixels + size_t(canvas.width()) * canvas.height());
}
static void tick() {
    const double until = emscripten_get_now() + 4.0;
    do {
        Serial.pump();
        loop();
    } while (terminalDesktopWorkPending() && emscripten_get_now() < until);
    auto *canvas = xtgvDesktopCanvas();
    static uint64_t previous = UINT64_MAX;
    static int oldBrightness = -1;
    const int brightness = xtgvDesktopBrightness(xtgvDesktopBacklight());
    if (canvas && !published.empty() && (canvas->generation() != previous || brightness != oldBrightness)) {
        previous = canvas->generation(); oldBrightness = brightness;
        present(published.data(), canvas->width(), canvas->height(), brightness);
    }
    if (xtgvEmulatorFinished.load(std::memory_order_acquire)) {
        xtgvEmulatorJoinFinished(); // Only join after the worker has finished.
        emscripten_cancel_main_loop();
        const int result = !xtgvDesktopWorkerError.empty() || !xtgvDesktopSaveOK ? 1
                         : xtgvDesktopRebootRequested() ? 75 : 0;
        finished(result, xtgvDesktopWorkerError.c_str());
    }
}
int main(int argc, char **argv) {
    if (argc > 1 && !xtgvWebSelectBoard(argv[1])) {
        finished(1, "Unknown browser target");
        return 1;
    }
    xtgvWebLockBoard();
    std::filesystem::current_path("/card");
    xtgvDesktopSetCardRoot("/card");
    setup();
    xtgvDesktopCanvas()->publish = publish;
    xtgvDesktopCanvas()->flush();
    // Returning to the browser is essential: filesystem proxy requests and
    // the guest worker must not wait behind a blocking native while-loop.
    emscripten_set_main_loop(tick, 0, false);
    return 0;
}
