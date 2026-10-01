#define XTGV_HAL_IMPLEMENTATION
#define XTGV_TERMINAL 1
// Exercise production overlay rendering for both zero and shifted HAL offsets.
bool terminalHistoryStoragePoll() { return true; }
bool terminalConfigStoragePoll() { return true; }
#include "../src/terminal/terminal_runtime.cpp"
#include <vector>
#include <iostream>

int main() {
    gfx = xtgvBoardCreateDisplay();
    displayOK = true;
    const auto &hal = vt::displayHalDefaults();
    const int ledY = 1 + hal.diskIndicatorYOffset;
    for (int zoom = 1; zoom <= 4; ++zoom) {
        const int visible = vt::evenVisibleCols(hal.physicalWidth, zoom);
        const int rows = vt::visibleRows(hal.physicalHeight, zoom);
        const int logicalRows = std::min(vt::MaxRows, rows * 2);
        for (bool right : {false, true}) for (bool bottom : {false, true}) {
            assert(terminal.setGeometry(80, logicalRows, visible, rows, zoom));
            terminal.cursorVisible = false;
            presentationGutter = hal.physicalWidth - visible * 6 * zoom;
            terminal.pan(right ? terminal.viewportLimit() : 0);
            terminal.panRows(bottom ? terminal.rowViewportLimit() : 0);
            for (int i = 0; i < vt::MaxCells; ++i) terminal.charRAM[i] = 'X';
            for (int n = 0; n < 1000; ++n) renderBatch();
            const std::vector<uint16_t> before(std::begin(gfx->framebuffer),
                                                std::end(gfx->framebuffer));
            terminalDiskActivity(true);
            diskActivityIndicatorPoll();
            renderBatch();
            assert(gfx->framebuffer[ledY * 320 + 1] == 0xf800);
            assert(gfx->framebuffer[(ledY + 5) * 320 + 6] == 0xf800);
            fakeTime += 76;
            diskActivityIndicatorPoll();
            renderBatch();
            assert(std::equal(before.begin(), before.end(), std::begin(gfx->framebuffer)));
            terminalDiskActivity(true);
            diskActivityIndicatorPoll();
            renderBatch();
            retireDiskIndicatorForPlot();
            assert(std::equal(before.begin(), before.end(), std::begin(gfx->framebuffer)));
            diskActivityUntil.store(0);
        }
    }
    delete gfx;
    delete gfxTestBus;
    std::cout << "Disk indicator restoration passed at all zooms and 2D pan edges\n";
}
