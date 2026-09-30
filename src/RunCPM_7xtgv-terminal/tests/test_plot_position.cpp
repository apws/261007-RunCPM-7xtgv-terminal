// Exercise the complete positioned-plot/output/cleanup sequence with the
// deferred-flush path used by the 800x480 RGB target.
#define GFX_DEFERRED_FLUSH 1
bool terminalHistoryStoragePoll() { return true; }
bool terminalConfigStoragePoll() { return true; }
#include "../src/terminal/terminal_runtime.cpp"
#include <iostream>
#include <string>

static void feedOutput(const std::string &bytes) {
    for (uint8_t byte : bytes) {
        while (!output.push(byte)) {
            ++fakeTime;
            terminalLoop();
        }
    }
}

int main() {
    terminalSetup();
    assert(displayOK && plotScanline);
    assert(terminal.setGeometry(80, 12, 26, 12, 2));
    presentationGutter = 8;
    Serial.online = true;
    for (int i = 0; i < 1000; ++i) {
        ++fakeTime;
        terminalLoop();
    }

    feedOutput("\033[2J\033[HA6>VTG100\r\n\033[>4;2;100;100vA6>GPIX\r\n");
    for (int row = 0; row < 10; ++row) {
        for (int col = 0; col < 10; ++col)
            feedOutput("\033[38;5;196m1");
        feedOutput("\r\n");
    }
    feedOutput("\033[0mA6>\r\n");
    for (int i = 0; i < 500; ++i) {
        ++fakeTime;
        terminalLoop();
    }
    assert(plotCanvasValid && !terminal.plotting());
    assert(gfx->framebuffer[100 * 320 + 100] == 0xf800);
    assert(gfx->framebuffer[119 * 320 + 119] == 0xf800);
    assert(gfx->framebuffer[100 * 320 + 99] == 0xffff);
    // The retained image is discarded by a full geometry redraw. The gutter
    // must be restored at both viewport edges after changing zoom/pan.
    vt::DisplayProfile profile = profiles.display();
    profile.viewZoom = 1;
    profile.geometry[0] = {52, 24}; // no panning: ordinary grey gutter
    applyDisplayProfile(profile);
    ++fakeTime;
    terminalLoop();
    assert(!plotCanvasValid);
    assert(gfx->framebuffer[100 * 320 + 1] == rgb565({128, 128, 128}));
    profile.geometry[0] = {80, 24}; // panning enabled: active-line gutter
    applyDisplayProfile(profile);
    ++fakeTime;
    terminalLoop();
    for (uint8_t byte : std::string("\033[>4;2;100;100v\033[38;5;196m1\r\n\033[0m"))
        terminal.feed(byte);
    fakeTime += 100;
    terminalLoop();
    assert(plotCanvasValid);
    assert(gfx->framebuffer[100 * 320 + 100] == 0xf800);
    terminal.pan(terminal.viewportLimit());
    ++fakeTime;
    terminalLoop();
    assert(!plotCanvasValid);
    assert(gfx->framebuffer[100 * 320 + 313] == rgb565({205, 255, 205}));
    for (uint8_t byte : std::string("\033[>4;2;100;100v\033[38;5;21m1\r\n\033[0m"))
        terminal.feed(byte);
    assert(gfx->framebuffer[100 * 320 + 100] == 0x001f);
    fakeTime += 100;
    terminalLoop();
    for (int viewZoom = 2; viewZoom <= 4; ++viewZoom) {
        profile.viewZoom = uint8_t(viewZoom);
        applyDisplayProfile(profile);
        ++fakeTime;
        terminalLoop();
        assert(!plotCanvasValid);
        for (uint8_t byte : std::string("\033[>4;2;100;100v\033[38;5;196m1\r\n\033[0m"))
            terminal.feed(byte);
        assert(gfx->framebuffer[100 * 320 + 100] == 0xf800);
        fakeTime += 100;
        terminalLoop();
    }
    // A normal text scroll overwrites the plot's terminal cells. It must also
    // retire the retained image and repaint a gutter that the plot covered.
    profile.viewZoom = 1;
    profile.geometry[0] = {52, 24};
    applyDisplayProfile(profile);
    ++fakeTime;
    terminalLoop();
    for (uint8_t byte : std::string("\033[>4;2;0;100v\033[38;5;196m1\r\n\033[0m"))
        terminal.feed(byte);
    fakeTime += 100;
    terminalLoop();
    assert(plotCanvasValid && gfx->framebuffer[100 * 320 + 1] == 0xf800);
    terminal.y = terminal.rows - 1;
    terminal.feed('\n');
    assert(!terminal.compositionDirty); // scrolling needs no full-screen clear
    ++fakeTime;
    terminalLoop();
    assert(!plotCanvasValid);
    assert(gfx->framebuffer[100 * 320 + 1] == rgb565({128, 128, 128}));
    profile.geometry[0] = {80, 24};
    applyDisplayProfile(profile);
    terminal.pan(terminal.viewportLimit());
    ++fakeTime;
    terminalLoop();
    for (uint8_t byte : std::string("\033[>4;2;313;100v\033[38;5;21m1\r\n\033[0m"))
        terminal.feed(byte);
    fakeTime += 100;
    terminalLoop();
    assert(plotCanvasValid && gfx->framebuffer[100 * 320 + 313] == 0x001f);
    terminal.y = terminal.rows - 1;
    terminal.feed('\n');
    assert(!terminal.compositionDirty);
    ++fakeTime;
    terminalLoop();
    assert(!plotCanvasValid);
    assert(gfx->framebuffer[100 * 320 + 313] == rgb565({205, 255, 205}));
    std::cout << "physical plot origin and post-plot gutter redraw passed\n";
}
