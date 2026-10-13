#define XTGV_HAL_IMPLEMENTATION

// Runtime-only proof. No filesystem owner/sketch is linked at this checkpoint.
bool terminalHistoryStoragePoll()
{
    return true;
}

bool terminalConfigStoragePoll()
{
    return true;
}

#include "../../src/terminal/terminal_runtime.cpp"
#include "../platform.h"
#include <cstdio>
#include <cstdlib>
#include <array>

static void feedTest(vt::Terminal& t, const std::string& s)
{
    for (uint8_t c : s)
        t.feed(c);
}

static void require(bool condition, const char* what)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

static void testLegacyAndMargins()
{
    vt::Terminal t;
    require(t.setGeometry(80, 24, 80, 24, 1), "80x24 test geometry");
    auto feed = [&](const std::string& s)
    {
        feedTest(t, s);
    };
    auto row = [&](int r)
    {
        return std::string(reinterpret_cast<char*>(t.charRAM + t.index(0, r)), 80);
    };
    feed("\033[?2l\033H\033JHEADER\033Y!!X\033C\033DY\033B\033AZ");
    require(!t.ansi && row(1).substr(1, 3) == "XYZ", "VT52 Y and ABCD movement");
    feed("\033Y# KEEP REMOVE\033Y#%\033K");
    require(row(3).substr(0, 6) == "KEEP  " && row(0).substr(0, 6) == "HEADER",
            "VT52 K preserves prefix and other rows");
    feed("\033Y% REMOVE\033Y#%\033J");
    require(row(5) == std::string(80, ' '), "VT52 J erases following rows");
    feed("\033H\033I");
    require(row(0) == std::string(80, ' ') && row(1).substr(0, 6) == "HEADER", "VT52 I reverse index at top");
    feed("\033Fq\033Gq\033<");
    require((t.flags[0] & vt::Graphics) && !(t.flags[1] & vt::Graphics) && t.ansi, "VT52 F/G and ANSI return");
    t.reset();
    feed("HEADER\033[23;1HFOOTER\033[2;22r\033[2;1H");
    for (int n = 1; n <= 25; ++n)
        feed(std::to_string(n) + "\r\n");
    require(row(0).substr(0, 6) == "HEADER" && row(22).substr(0, 6) == "FOOTER", "scroll keeps header/footer");
    require(row(1).substr(0, 1) == "6" && row(20).substr(0, 2) == "25" && row(21) == std::string(80, ' '),
            "scroll has exact numbered contents");
    feed("\033[2;1H\033M");
    require(row(1) == std::string(80, ' ') && row(2)[0] == '6' && row(21).substr(0, 2) == "25",
            "RI inserts blank only inside region");
    for (int r = 2; r <= 22; ++r)
        feed("\033[" + std::to_string(r) + ";1H\033[KROW " + std::to_string(r));
    feed("\033[10;1H\033[3L\033[2M");
    require(row(9) == std::string(80, ' ') && row(10).substr(0, 6) == "ROW 10" && row(19).substr(0, 6) == "ROW 19" &&
                row(21) == std::string(80, ' '),
            "IL/DL preserve bounded row contents");
    feed("\033[5;20r\033[?6h\033[1;1H");
    require(t.y == 4, "origin home is top margin");
    feed("\033[16;1H");
    require(t.y == 19, "origin relative bottom");
    feed("\033[999A");
    require(t.y == 4, "origin upper clamp");
    feed("\033[999B");
    require(t.y == 19, "origin lower clamp");
    feed("\033[?6l\033[r\033[24;1HRESET\r\n");
    require(row(0).substr(0, 6) != "HEADER" && row(22).substr(0, 5) == "RESET",
            "reset restores whole-screen scrolling");
}

static void testBlinkAndReversePixels()
{
    terminal.reset();
    feedTest(terminal, "\033[3;1HX\033[5mX\033[25mX\033[7mX\033[27mX");
    const int start = terminal.index(0, 2);
    auto pixelsFor = [&](int column, bool phase)
    {
        cursorPhase = phase;
        drawCell(start + column, false);
        std::array<uint16_t, 60> sample;
        // drawCell's unscaled 6x10 buffer is exactly what the real GFX call scales.
        std::copy(pixels, pixels + 60, sample.begin());
        return sample;
    };
    const auto normal = pixelsFor(0, true);
    require(pixelsFor(1, true) == normal && pixelsFor(2, false) == normal, "SGR5 on phase and SGR25 steady pixels");
    const auto hidden = pixelsFor(1, false);
    require(hidden != normal && std::all_of(hidden.begin(), hidden.end(),
                                            [&](uint16_t p)
                                            {
                                                return p == rgb565(terminal.background(start + 1));
                                            }),
            "SGR5 off phase hides glyph, leaves paper");
    const auto reverse = pixelsFor(3, true);
    const auto fg = rgb565(terminal.foreground(start));
    const auto bg = rgb565(terminal.background(start));
    for (size_t i = 0; i < normal.size(); ++i)
        require(reverse[i] == (normal[i] == fg ? bg : fg), "SGR7 swaps ink/paper pixels");
    require(pixelsFor(3, false) == reverse && pixelsFor(4, false) == normal,
            "reverse does not blink; SGR27 restores normal");
}

static void testPlotHighlightHandoff()
{
    auto feed = [](const char* s)
    {
        while (*s)
        {
            terminal.feed(uint8_t(*s++));
            updateActiveRow();
        }
    };
    auto paint = []
    {
        displayFlushAt = millis();
        renderBatch();
    };
    terminal.reset();
    feed("\033[6;1H");
    paint();
    const auto* frame = xtgvDesktopCanvas()->getFramebuffer();
    const int x = presentationGutter + 20 * 6 + 5;
    const int p = 59 * gfx->width() + x;
    const auto paper = rgb565(terminal.palette.paper[0]);
    require(frame[p] == rgb565(terminal.palette.activeLine), "old row visibly highlighted");
    gfx->drawPixel(gfx->width() - 1, gfx->height() - 1, 0x001f);
    // SUBMIT advances the cursor and starts a plot in one output batch,
    // before the previously painted highlight can be erased by renderBatch.
    feed("\r\n\033[>4;1;0;0v\r\nGenerating a Mandelbrot set\r\n");
    require(frame[p] != paper, "arming GCLS alone leaves display untouched");
    feed("\033[38;5;196m#\r\n");
    require(frame[p] == paper, "first sample retires pending old highlight");
    const int current = (terminal.y - 1) * 10 * gfx->width() + x;
    require(frame[current] == paper, "plot handoff does not paint a new active strip");
    require(frame[0] == 0xf800, "plot paints after terminal handoff");
    feed("\033[0mDone\r\nA0>");
    plotEndActivityAt = millis() - 101;
    plotEndCleanupPoll(millis());
    updateActiveRow();
    paint();
    require(frame[p] == paper, "old highlight stays erased after plot completion");
    require(frame[0] == 0xf800, "completion preserves plotted pixels");
    require(frame[gfx->width() * gfx->height() - 1] == 0x001f, "handoff preserves unrelated retained graphics");
    const int prompt = (terminal.y * 10 + 9) * gfx->width() + x;
    require(frame[prompt] == rgb565(terminal.palette.activeLine), "new prompt highlight returns");
}

static void testDesktopFrameScheduling()
{
    terminal.reset();
    terminal.invalidate();
    terminal.compositionDirty = true;
    displayFlushIntervalMs = 100;
    displayFlushAt = millis() + 1000;
    auto* canvas = xtgvDesktopCanvas();
    const auto before = canvas->generation();
    renderBatch();
    require(canvas->generation() == before && terminal.compositionDirty,
            "desktop defers drawing/publication until the frame deadline");
    require(!terminalDesktopWorkPending(), "future frame deadline must permit idle sleep");
    require(output.push('X') && terminalDesktopWorkPending(), "queued guest output must bypass desktop idle sleep");
    uint8_t discarded;
    require(output.pop(discarded), "remove scheduler fixture byte");
    displayFlushAt = millis();
    renderBatch();
    for (int y = 0; y < terminal.visibleRows; ++y)
        for (int x = 0; x < terminal.visibleCols; ++x)
            require(!terminal.dirty[terminal.index(x + terminal.viewport, y + terminal.rowViewport)],
                    "desktop completes all visible cells in one pass");
    require(canvas->generation() == before + 1, "one complete frame is published");
    renderBatch();
    require(canvas->generation() == before + 1, "idle frame does not republish");
    displayFlushIntervalMs = 0;
    displayFlushAt = millis();
    terminal.invalidate();
    renderBatch();
    terminal.invalidate();
    renderBatch();
    require(canvas->generation() == before + 3, "zero Hz permits each changed complete frame");
}

int main()
{
    terminalSetup();
    require(ready && gfx && gfx->width() == XTGV_DISPLAY_DEFAULTS.physicalWidth &&
                gfx->height() == XTGV_DISPLAY_DEFAULTS.physicalHeight,
            "production runtime uses desktop board");
    require(xtgvDesktopBacklight() == 0, "boot starts with backlight off");
    terminalConfigBegin();
    const std::string ini =
        std::string("[display:") + XTGV_DISPLAY_DEFAULTS.id + "]\nview_zoom=1\nrender_flush_hz=10\n";
    for (auto c : ini)
        terminalConfigFeed(uint8_t(c));
    require(terminalConfigEnd() == 0, "selected real-board INI section accepted");
    terminalBootConfigComplete();
    const char* stream = "\033[2J\033[H7xtgv native desktop\r\n\033[31mRED\033[0m";
    while (*stream)
        virtualWrite(uint8_t(*stream++));
    for (unsigned i = 0; i < 100; ++i)
    {
        terminalLoop();
        delay(1);
    }
    auto* canvas = dynamic_cast<XtgvDesktopCanvas*>(gfx);
    require(canvas && canvas->generation() > 0, "real Canvas flush batches");
    require(activeDisplayProfile.renderFlushHz == 10 && displayFlushIntervalMs == 100,
            "real-board INI selects 10 Hz production scheduler");
    if (BufferedDisplay)
    {
        const auto before = canvas->generation();
        displayFlushAt = millis() + 100;
        requestDisplayFlush();
        flushCanvasIfDue();
        require(canvas->generation() == before, "buffered policy waits until due");
        displayFlushAt = millis();
        flushCanvasIfDue();
        require(canvas->generation() == before + 1, "due batch publishes exactly once");
    }
    const uint16_t* pixels = canvas->getFramebuffer();
    bool different = false;
    for (size_t i = 0; i < size_t(gfx->width()) * gfx->height(); ++i)
        different = different || pixels[i] != pixels[0];
    require(different, "real QL-font terminal paints nonuniform pixels");
    const uint8_t key[] = {'x'};
    require(Serial.feed(key, sizeof(key)) == sizeof(key), "input shim accepts bytes");
    terminalLoop();
    require(virtualAvailable() == 1 && virtualRead() == 'x', "existing keyboard path forwards input to guest queue");
    const uint16_t original = pixels[100];
    xtgvBoardBacklightWrite(0);
    require(pixels[100] == original, "backlight does not mutate framebuffer");
    testPlotHighlightHandoff();
    testLegacyAndMargins();
    testBlinkAndReversePixels();
    testDesktopFrameScheduling();
    std::printf("Production terminal + real Arduino_GFX %s %dx%d: PASS\n", XTGV_DISPLAY_DEFAULTS.id, gfx->width(),
                gfx->height());
}
