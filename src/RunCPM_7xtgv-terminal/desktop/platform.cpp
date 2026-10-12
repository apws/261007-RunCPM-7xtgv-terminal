#include <Arduino.h>
#include <chrono>
#include <thread>
#include "platform.h"
#include "../hardware/arduino_7xtgv/board.h"
#include "../src/terminal/line_history.h"
#include <new>
#include <cmath>
#if defined(_WIN32)
#include <windows.h>
#include <cstdio>
#endif

namespace {
const auto epoch = std::chrono::steady_clock::now();
std::atomic<uint8_t> backlight{0};
std::atomic<bool> rebootRequested{false};
std::atomic<bool> stopRequested{false};
thread_local bool guestOwner = false;
std::filesystem::path cardRoot;
XtgvDesktopCanvas *canvas = nullptr;
#if defined(_WIN32)
// Configured before the guest thread starts; immutable while either thread runs.
bool coarseDelay = false;
class WindowsDelay {
public:
    WindowsDelay() : timer_(CreateWaitableTimerExW(nullptr, nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE)) {}
    ~WindowsDelay() { if (timer_) CloseHandle(timer_); }
    WindowsDelay(const WindowsDelay &) = delete;
    WindowsDelay &operator=(const WindowsDelay &) = delete;
    bool available() const { return timer_ != nullptr; }
    bool wait(uint64_t microseconds) {
        if (!timer_) return false;
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(microseconds * 10);
        if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)
            && WaitForSingleObject(timer_, INFINITE) == WAIT_OBJECT_0) return true;
        std::fprintf(stderr, "Desktop timer wait failed (%lu); using standard sleep\n",
                     static_cast<unsigned long>(GetLastError()));
        CloseHandle(timer_);
        timer_ = nullptr;
        return false;
    }
private:
    HANDLE timer_;
};
// A separate timer per thread prevents the guest and UI from rearming each
// other's wait. Windows 10 1803+ supports this timer; no busy-wait fallback.
WindowsDelay &windowsDelay() { thread_local WindowsDelay timer; return timer; }
#endif
}

#if defined(_WIN32)
const char *xtgvDesktopConfigureDelay(bool coarse) {
    coarseDelay = coarse;
    if (coarse) return "standard sleep (coarse baseline)";
    return windowsDelay().available() ? "high-resolution waitable timer"
                                      : "standard sleep (high-resolution timer unavailable)";
}
#endif

XtgvDesktopSerial Serial;
XtgvHostStorage SD;
void xtgvDesktopSetWorker(bool active) { guestOwner = active; }
void xtgvDesktopCheckStop() {
    if (guestOwner && stopRequested.load()) throw XtgvDesktopStop{};
}
void xtgvDesktopRequestStop() { stopRequested.store(true); }
bool xtgvDesktopStopping() { return stopRequested.load(); }
XtgvDesktopCanvas *xtgvDesktopCanvas() { return canvas; }

XtgvBoardStorageStatus xtgvBoardStorageInit() { return {true, SD.begin(cardRoot)}; }
bool xtgvBoardStorageSetMHz(uint8_t) { return false; }
void xtgvBoardPrintStorageStatus(const XtgvBoardStorageStatus &status, void (*write)(const char *)) {
    write(status.mediaReady ? "Host filesystem ...OK\r\n" : "Host filesystem ...UNAVAILABLE\r\n");
}
void xtgvBoardPrintStorageSummary(void (*write)(const char *)) {
#if defined(XTGV_WEB)
    write("Storage: browser MEMFS (SD/File compatibility)\r\n");
#else
    write("Storage: desktop host filesystem (SD/File compatibility)\r\n");
#endif
}
void xtgvBoardPrintBootExtra(void (*write)(const char *)) {
#if defined(XTGV_WEB)
    write("Backend: browser WebAssembly proof\r\n");
#else
    write("Backend: desktop\r\n");
#endif
}
Arduino_GFX *xtgvBoardCreateDisplay() {
    canvas = new (std::nothrow) XtgvDesktopCanvas(XTGV_DISPLAY_DEFAULTS.physicalWidth,
                                                XTGV_DISPLAY_DEFAULTS.physicalHeight);
    return canvas;
}
bool xtgvBoardBeginDisplay(Arduino_GFX *display) { return display && display->begin(); }
vt::LineHistory *xtgvBoardCreateHistory() { return new (std::nothrow) vt::LineHistory; }
void xtgvBoardBacklightInit() { xtgvDesktopSetBacklight(0); }
void xtgvBoardBacklightWrite(uint8_t duty) { xtgvDesktopSetBacklight(duty); }
void runCPMHardwareReboot() { xtgvDesktopRequestReboot(); }

void XtgvDesktopCanvas::flush(bool) {
    ++generation_;
    if (publish) publish(*this);
    dirty_ = false;
}
void XtgvDesktopCanvas::publishDirect() { if (dirty_) flush(); }
void XtgvDesktopCanvas::writePixelPreclipped(int16_t x, int16_t y, uint16_t c) {
    dirty_ = true; Arduino_Canvas::writePixelPreclipped(x, y, c);
}
void XtgvDesktopCanvas::writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) {
    dirty_ = true; Arduino_Canvas::writeFastVLine(x, y, h, c);
}
void XtgvDesktopCanvas::writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) {
    dirty_ = true; Arduino_Canvas::writeFastHLine(x, y, w, c);
}
void XtgvDesktopCanvas::writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    dirty_ = true; Arduino_Canvas::writeFillRectPreclipped(x, y, w, h, c);
}
void XtgvDesktopCanvas::draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t *b, int16_t w, int16_t h) {
    dirty_ = true; Arduino_Canvas::draw16bitRGBBitmap(x, y, b, w, h);
}
void XtgvDesktopCanvas::draw16bitBeRGBBitmap(int16_t x, int16_t y, uint16_t *b, int16_t w, int16_t h) {
    dirty_ = true; Arduino_Canvas::draw16bitBeRGBBitmap(x, y, b, w, h);
}
void XtgvDesktopCanvas::draw16bitRGBBitmapWithTranColor(int16_t x, int16_t y, uint16_t *b, uint16_t c, int16_t w, int16_t h) {
    dirty_ = true; Arduino_Canvas::draw16bitRGBBitmapWithTranColor(x, y, b, c, w, h);
}
void XtgvDesktopCanvas::drawIndexedBitmap(int16_t x, int16_t y, uint8_t *b, uint16_t *p, int16_t w, int16_t h, int16_t skip) {
    dirty_ = true; Arduino_Canvas::drawIndexedBitmap(x, y, b, p, w, h, skip);
}
void XtgvDesktopCanvas::drawIndexedBitmap(int16_t x, int16_t y, uint8_t *b, uint16_t *p, uint8_t c, int16_t w, int16_t h, int16_t skip) {
    dirty_ = true; Arduino_Canvas::drawIndexedBitmap(x, y, b, p, c, w, h, skip);
}
int XtgvDesktopSerial::available() {
    std::lock_guard<std::mutex> lock(mutex_);
    return int(input_.size());
}
int XtgvDesktopSerial::read() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (input_.empty()) return -1;
    const int value = input_.front();
    input_.pop_front();
    return value;
}
size_t XtgvDesktopSerial::feed(const uint8_t *bytes, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t accepted = 0;
    while (accepted < count && input_.size() < 16384)
        input_.push_back(bytes[accepted++]);
    return accepted;
}
uint8_t xtgvDesktopBacklight() { return backlight.load(); }
uint8_t xtgvDesktopBrightness(uint8_t duty) {
    // Physical PWM is not an SDL RGB multiplier. Normalize the board's
    // thermal/electrical ceiling, then lift shadows with one global curve.
    // This is a visual approximation, not measured panel calibration.
    const auto maximum = XTGV_BACKLIGHT_DUTIES[31];
    if (!duty) return 0;
    if (duty >= maximum) return 255;
    return uint8_t(std::lround(255.0 * std::pow(double(duty) / maximum, 1.0 / 2.2)));
}
void xtgvDesktopSetBacklight(uint8_t duty) { backlight.store(duty); }
void xtgvDesktopRequestReboot() { rebootRequested.store(true); xtgvDesktopRequestStop(); }
bool xtgvDesktopRebootRequested() { return rebootRequested.load(); }
void xtgvDesktopSetCardRoot(const std::filesystem::path &root) { cardRoot = root; }
const std::filesystem::path &xtgvDesktopCardRoot() { return cardRoot; }

uint32_t millis() {
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - epoch).count());
}
uint32_t micros() {
    return uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - epoch).count());
}
void delay(uint32_t milliseconds) {
    xtgvDesktopCheckStop();
#if defined(_WIN32)
    if (!milliseconds) return;
    if (!coarseDelay && windowsDelay().wait(uint64_t(milliseconds) * 1000)) return;
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}
void delayMicroseconds(uint32_t microseconds) {
    xtgvDesktopCheckStop();
#if defined(_WIN32)
    if (!microseconds) return;
    if (!coarseDelay && windowsDelay().wait(microseconds)) return;
#endif
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
}
void yield() { xtgvDesktopCheckStop(); std::this_thread::yield(); }

// memcpy also handles unaligned flash-like addresses without aliasing UB.
uint8_t xtgvPgmReadByte(const void *address) {
    uint8_t value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}
uint16_t xtgvPgmReadWord(const void *address) {
    uint16_t value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}
uint32_t xtgvPgmReadDword(const void *address) {
    uint32_t value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}
const void *pgm_read_ptr(const void *address) {
    const void *value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}
