#pragma once
#include <canvas/Arduino_Canvas.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <filesystem>
#include "storage.h"

using File = XtgvHostFile;
extern XtgvHostStorage SD;
struct XtgvDesktopStop {};
void xtgvDesktopSetWorker(bool active);
void xtgvDesktopCheckStop();
void xtgvDesktopRequestStop();
bool xtgvDesktopStopping();
#if defined(_WIN32)
// Call once before starting the guest worker. The returned label describes
// the UI thread's delay implementation for native diagnostic logs.
const char *xtgvDesktopConfigureDelay(bool coarse);
#endif

// Drawing remains in the real library. Flush marks a completed render batch.
class XtgvDesktopCanvas : public Arduino_Canvas {
public:
    XtgvDesktopCanvas(int16_t width, int16_t height)
        : Arduino_Canvas(width, height, nullptr) {}
    void flush(bool = false) override;
    void publishDirect();
    void writePixelPreclipped(int16_t x, int16_t y, uint16_t c) override;
    void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override;
    void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override;
    void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override;
    void draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t *b, int16_t w, int16_t h) override;
    void draw16bitBeRGBBitmap(int16_t x, int16_t y, uint16_t *b, int16_t w, int16_t h) override;
    void draw16bitRGBBitmapWithTranColor(int16_t x, int16_t y, uint16_t *b, uint16_t c, int16_t w, int16_t h) override;
    void drawIndexedBitmap(int16_t x, int16_t y, uint8_t *b, uint16_t *p, int16_t w, int16_t h, int16_t skip = 0) override;
    void drawIndexedBitmap(int16_t x, int16_t y, uint8_t *b, uint16_t *p, uint8_t c, int16_t w, int16_t h, int16_t skip = 0) override;
    uint64_t generation() const { return generation_; }
    void (*publish)(XtgvDesktopCanvas &) = nullptr;
private:
    bool dirty_ = true;
    uint64_t generation_ = 0; // renderer and presenter have the same UI owner
};
XtgvDesktopCanvas *xtgvDesktopCanvas();

uint8_t xtgvDesktopBacklight();
uint8_t xtgvDesktopBrightness(uint8_t duty);
void xtgvDesktopSetBacklight(uint8_t duty);
void xtgvDesktopRequestReboot();
bool xtgvDesktopRebootRequested();
void xtgvDesktopSetCardRoot(const std::filesystem::path &root);
const std::filesystem::path &xtgvDesktopCardRoot();
