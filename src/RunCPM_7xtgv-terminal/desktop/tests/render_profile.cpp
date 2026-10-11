// Diagnostic only: real terminal renderer/GFX, no RunCPM or filesystem owner.
#define XTGV_HAL_IMPLEMENTATION
bool terminalHistoryStoragePoll() { return true; }
bool terminalConfigStoragePoll() { return true; }
#include "../../src/terminal/terminal_runtime.cpp"
#include "../platform.h"
#include "../presenter.h"
#include <iostream>
#include <chrono>
#include <stdexcept>

using Clock = std::chrono::steady_clock;
static uint64_t elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
}
static bool dirtyVisible() {
    for (int y = 0; y < terminal.visibleRows; ++y)
        for (int x = 0; x < terminal.visibleCols; ++x)
            if (terminal.dirty[terminal.index(x + terminal.viewport, y + terminal.rowViewport)]) return true;
    return false;
}
int main(int argc, char **argv) {
    const bool sdl = argc > 1 && std::string(argv[1]) == "--sdl";
#if defined(_WIN32)
    std::cout << "Delay: " << xtgvDesktopConfigureDelay(false) << '\n';
#endif
    terminalSetup();
    const std::string ini = std::string("[display:") + XTGV_DISPLAY_DEFAULTS.id +
        "]\nview_zoom=2\nzoom2_terminal=80x24\nrender_flush_hz=10\n[global]\nccp_bank=9\n";
    terminalConfigBegin();
    for (const auto c : ini) terminalConfigFeed(uint8_t(c));
    if (terminalConfigEnd()) throw std::runtime_error("Profile INI rejected");
    terminalBootConfigComplete();
    terminalLoop(); // Apply the queued INI on its actual UI owner before measuring.
    if (sdl) {
        xtgvPresenterOpen(gfx->width(), gfx->height(), "Disposable render profile");
        xtgvDesktopCanvas()->publish = xtgvPresenterPublish;
    }
    std::cout << "RENDER_PROFILE " << XTGV_DISPLAY_DEFAULTS.id << " visible="
              << terminal.visibleCols << 'x' << terminal.visibleRows
              << " full_budget=" << FullFrameBudget << " sdl=" << sdl << '\n';
    // Identical 200-line text workload; isolate parser cost from painting.
    auto start = Clock::now();
    for (int n = 0; n < 200; ++n)
        for (const auto c : std::string("VT display throughput: 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ\r\n"))
            guestOutputFeed(uint8_t(c));
    std::cout << "PARSER_200_LINES us=" << elapsed(start) << '\n';
    for (const bool paced : {true, false}) {
        for (const int hz : {10, 0}) {
            displayFlushIntervalMs = hz ? 1000 / hz : 0;
            displayFlushAt = millis();
            terminal.invalidate();
            const auto initial = xtgvDesktopCanvas()->generation();
            start = Clock::now();
            uint64_t drawUs = 0, presentUs = 0, sleepUs = 0, passes = 0;
            do {
                auto tick = Clock::now();
                renderBatch();
                drawUs += elapsed(tick);
                tick = Clock::now();
                if (sdl) xtgvPresenterPoll();
                presentUs += elapsed(tick);
                ++passes;
                if (paced) {
                    tick = Clock::now(); delay(1); sleepUs += elapsed(tick);
                }
                if (elapsed(start) > 15000000) throw std::runtime_error("Repaint timeout");
            } while (dirtyVisible() || displayFlushPending);
            std::cout << "REPAINT paced=" << paced << " flush_hz=" << hz
                      << " wall_us=" << elapsed(start) << " passes=" << passes
                      << " draw_and_upload_us=" << drawUs << " present_us=" << presentUs
                      << " sleep_us=" << sleepUs
                      << " publications=" << xtgvDesktopCanvas()->generation() - initial << '\n';
        }
    }
    if (sdl) xtgvPresenterClose();
}
