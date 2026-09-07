#include "terminal_runtime.h"
#include "7xtgv-QL-font.h"
#include "7xtgv-gfxsetup.h"
#include "byte_queue.h"
#include "terminal.h"
#include <Arduino.h>
#include <algorithm>
#include <atomic>
#include <stdio.h>
#include <string.h>

static ByteQueue<4096> input;
static ByteQueue<16384> output;
// Replies cannot block rendering if a program emits queries but never reads input.
static ByteQueue<1024> replies;
static std::atomic<bool> ready{false};
static uint32_t replyOverruns = 0;
static void terminalReply(void *, const uint8_t *data, size_t n) {
    if (replies.free() < n) {
        replyOverruns++;
        return;
    }
    for (size_t i = 0; i < n; i++)
        replies.push(data[i]);
}
static void keyboardSend(void *, const uint8_t *data, size_t n) {
    // The event loop reserves 128 input slots before calling the decoder.
    for (size_t i = 0; i < n; i++)
        input.push(data[i]);
}
static vt::Terminal terminal(terminalReply);
static vt::Keyboard keyboard(terminal, keyboardSend, nullptr);
static bool displayOK = false, cursorPhase = true, connected = false;
static uint32_t blinkAt = 0, bellUntil = 0, oldBells = 0;
static uint16_t pixels[6 * 10];
static int scanCell = 0, mirrorScan = 0;
static char mirrorBuffer[8192];
static size_t mirrorLength = 0, mirrorSent = 0;
static bool mirrorReset = false;

static uint16_t rgb565(vt::RGB c) { return ((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3); }
// DEC special graphics masks. The original QL font banks remain byte-identical.
static uint8_t graphicsRow(uint8_t c, int y) {
    const uint8_t full = 0xfc, center = 0x20;
    switch (c) {
    case '_':
        return 0;
    case 'j':
        return y < 4 ? center : y == 4 ? 0xe0 : 0; // lower right
    case 'k':
        return y > 4 ? center : y == 4 ? 0xe0 : 0; // upper right
    case 'l':
        return y > 4 ? center : y == 4 ? 0x3c : 0;
    case 'm':
        return y < 4 ? center : y == 4 ? 0x3c : 0;
    case 'n':
        return y == 4 ? full : center;
    case 'q':
        return y == 4 ? full : 0;
    case 't':
        return y == 4 ? 0x3c : center;
    case 'u':
        return y == 4 ? 0xe0 : center;
    case 'v':
        return y < 4 ? center : y == 4 ? full : 0;
    case 'w':
        return y > 4 ? center : y == 4 ? full : 0;
    case 'x':
        return center;
    case 'o':
        return y == 0 ? full : 0;
    case 'p':
        return y == 2 ? full : 0;
    case 'r':
        return y == 6 ? full : 0;
    case 's':
        return y == 8 ? full : 0;
    case '`': {
        const uint8_t diamond[10] = {0, 0, 0x20, 0x70, 0xf8, 0x70, 0x20, 0, 0, 0};
        return diamond[y];
    }
    case 'a':
        return y & 1 ? 0xa8 : 0x54;
    case 'f':
        return y == 1 || y == 3 ? 0x60 : y == 2 ? 0x90 : 0;
    case 'g':
        return y == 3 ? 0x20 : y == 4 ? 0x70 : y == 5 ? 0x20 : y == 7 ? 0x70 : 0;
    case '~':
        return y == 4 || y == 5 ? 0x60 : 0;
    case 'y': {
        const uint8_t le[10] = {0, 0x10, 0x20, 0x40, 0x20, 0x10, 0, 0x70, 0, 0};
        return le[y];
    }
    case 'z': {
        const uint8_t ge[10] = {0, 0x40, 0x20, 0x10, 0x20, 0x40, 0, 0x70, 0, 0};
        return ge[y];
    }
    case '|':
        return (y == 3 || y == 6 ? 0xf8 : 0) | (y > 0 && y < 9 ? uint8_t(0x08 << (y / 2)) : 0);
    case '{':
        return font1bpp[0]['p'][y]; // pi approximation at 6x10
    case '}':
        return font1bpp[0]['L'][y]; // pound approximation
    default:
        return font1bpp[0]['?'][y];
    }
}
static void drawCell(int i) {
    int column = i % vt::Cols - terminal.viewport, row = i / vt::Cols;
    uint8_t bank = (terminal.attrRAM[i] >> 6) & 3, ch = terminal.charRAM[i];
    uint16_t fg = rgb565(terminal.foreground(i)), bg = rgb565(terminal.background(i));
    bool cursor =
        terminal.cursorVisible && cursorPhase && terminal.x == i % vt::Cols && terminal.y == row;
    bool blinkOff = (terminal.flags[i] & vt::Blink) && !cursorPhase;
    for (int y = 0; y < 10; y++) {
        uint8_t inkBits = (terminal.flags[i] & vt::Graphics)
                              ? graphicsRow(ch, y)
                              : font1bpp[bank][ch < 128 ? ch : '?'][y];
        if ((terminal.flags[i] & vt::Underline) && y == 8)
            inkBits = 0xfc;
        for (int x = 0; x < 6; x++) {
            uint8_t mask = 0x80 >> x;
            uint16_t color = bg;
            if (!blinkOff && (inkBits & mask))
                color = fg;
            if (cursor && y >= 8)
                color = (color == fg) ? bg : fg;
            pixels[y * 6 + x] = color;
        }
    }
    gfx->draw16bitRGBBitmap(column * 6 + GFX_OFS_X, row * 10 + GFX_OFS_Y, pixels, 6, 10);
}
static void drawIndicator() {
    gfx->fillRect(312, 0, 8, 240, 0);
    // Track shows the visible fraction of the 80-column page.
    int start = terminal.viewport * 220 / vt::Cols;
    gfx->fillRect(314, start + 10, 4, vt::Visible * 220 / vt::Cols, 0x7bef);
    if (terminal.x < terminal.viewport)
        gfx->fillRect(312, 0, 8, 6, 0xffff);
    if (terminal.x >= terminal.viewport + vt::Visible)
        gfx->fillRect(312, 234, 8, 6, 0xffff);
    if (int32_t(bellUntil - millis()) > 0)
        gfx->fillRect(312, 112, 8, 16, 0xffe0);
    if (replyOverruns)
        gfx->fillRect(312, 106, 8, 4, 0xf800);
}
static void renderBatch() {
    if (!displayOK)
        return;
    int drawn = 0;
    for (int checked = 0; checked < vt::Visible * vt::Rows && drawn < 12; checked++) {
        int view = scanCell++ % (vt::Visible * vt::Rows);
        int i = (view / vt::Visible) * vt::Cols + view % vt::Visible + terminal.viewport;
        if (terminal.dirty[i]) {
            drawCell(i);
            terminal.dirty[i] = false;
            drawn++;
        }
    }
    // Keep the scan index bounded even after months of continuous operation.
    scanCell %= vt::Visible * vt::Rows;
    if (terminal.indicatorDirty) {
        drawIndicator();
        terminal.indicatorDirty = false;
    }
}
static const char *graphicText(uint8_t ch) {
    switch (ch) {
    case '_':
        return " ";
    case 'j':
        return "┘";
    case 'k':
        return "┐";
    case 'l':
        return "┌";
    case 'm':
        return "└";
    case 'n':
        return "┼";
    case 'q':
        return "─";
    case 't':
        return "├";
    case 'u':
        return "┤";
    case 'v':
        return "┴";
    case 'w':
        return "┬";
    case 'x':
        return "│";
    case 'o':
        return "⎺";
    case 'p':
        return "⎻";
    case 'r':
        return "⎼";
    case 's':
        return "⎽";
    case '`':
        return "◆";
    case 'a':
        return "▒";
    case 'f':
        return "°";
    case 'g':
        return "±";
    case 'y':
        return "≤";
    case 'z':
        return "≥";
    case '{':
        return "π";
    case '|':
        return "≠";
    case '}':
        return "£";
    case '~':
        return "·";
    default:
        return "?";
    }
}
static void mirrorBuild() {
    mirrorLength = mirrorSent = 0;
    if (mirrorReset) {
        const char *reset = "\033[0m\033[?7l\033[2J\033[H";
        strcpy(mirrorBuffer, reset);
        mirrorLength = strlen(reset);
        mirrorReset = false;
        return;
    }
    int row = -1;
    for (int j = 0; j < vt::Rows; j++) {
        int r = (mirrorScan + j) % vt::Rows;
        if (terminal.mirrorDirty[r]) {
            row = r;
            mirrorScan = (r + 1) % vt::Rows;
            break;
        }
    }
    if (row >= 0) {
        mirrorLength = snprintf(mirrorBuffer, sizeof(mirrorBuffer), "\033[?25l\033[%d;1H", row + 1);
        int oldAttr = -1, oldFlags = -1;
        for (int col = 0; col < vt::Cols; col++) {
            int i = row * vt::Cols + col;
            if (oldAttr != terminal.attrRAM[i] || oldFlags != terminal.flags[i]) {
                vt::RGB f = terminal.foreground(i), b = terminal.background(i);
                mirrorLength += snprintf(
                    mirrorBuffer + mirrorLength, sizeof(mirrorBuffer) - mirrorLength,
                    "\033[0;%d;38;2;%u;%u;%u;48;2;%u;%u;%um",
                    (terminal.flags[i] & vt::Underline) ? 4 : 24, f.r, f.g, f.b, b.r, b.g, b.b);
                oldAttr = terminal.attrRAM[i];
                oldFlags = terminal.flags[i];
            }
            if (terminal.graphics(i)) {
                const char *s = graphicText(terminal.charRAM[i]);
                size_t n = strlen(s);
                memcpy(mirrorBuffer + mirrorLength, s, n);
                mirrorLength += n;
            } else
                mirrorBuffer[mirrorLength++] =
                    terminal.charRAM[i] >= 32 && terminal.charRAM[i] < 127 ? terminal.charRAM[i]
                                                                           : '?';
        }
        terminal.mirrorDirty[row] = false;
    }
    if (row >= 0 || terminal.mirrorCursorDirty) {
        mirrorLength += snprintf(mirrorBuffer + mirrorLength, sizeof(mirrorBuffer) - mirrorLength,
                                 "\033[%d;%dH\033[?25%c", terminal.y + 1, terminal.x + 1,
                                 terminal.cursorVisible ? 'h' : 'l');
        terminal.mirrorCursorDirty = false;
    }
}
static void mirrorPump() {
    bool now = bool(Serial);
    if (now != connected) {
        connected = now;
        mirrorLength = mirrorSent = 0;
        if (now) {
            terminal.invalidate();
            mirrorReset = true;
        }
    }
    if (!connected)
        return;
    if (mirrorSent == mirrorLength)
        mirrorBuild();
    int available = Serial.availableForWrite();
    if (available > 0 && mirrorSent < mirrorLength) {
        size_t n = std::min(size_t(available), std::min(size_t(64), mirrorLength - mirrorSent));
        mirrorSent += Serial.write(reinterpret_cast<const uint8_t *>(mirrorBuffer + mirrorSent), n);
    }
}
void terminalSetup() {
    Serial.begin(115200);
    // No USB diagnostics: this port carries only the interpreted screen mirror.
    GFX_DEVICE_INIT();
    displayOK = gfx->begin();
    pinMode(GFX_BL, OUTPUT);
    analogWrite(GFX_BL, 150);
    if (displayOK)
        gfx->fillScreen(0);
    terminal.reset();
    if (!displayOK) {
        const char *s = "TFT initialization failed; PC mirror remains available.\r\n";
        while (*s)
            terminal.feed(*s++);
    }
    ready.store(true, std::memory_order_release);
}
void terminalWaitReady() {
    while (!ready.load(std::memory_order_acquire))
        delay(1);
}
int virtualAvailable() { return input.available(); }
uint8_t virtualRead() {
    uint8_t c;
    while (!input.pop(c))
        delay(1);
    return c;
}
void virtualWrite(uint8_t c) {
    while (!output.push(c))
        delay(1);
}
void terminalLoop() {
    uint8_t c;
    while (input.free() && replies.pop(c))
        input.push(c);
    // Reserve room for a timed-out 63-byte sequence plus the next completed key.
    if (input.free() >= 128 && replies.available() == 0) {
        keyboard.tick(millis());
        for (int n = 0; n < 64 && input.free() >= 128 && Serial.available(); n++)
            keyboard.feed(Serial.read(), millis());
    }
    // Always consume CP/M output, even when its input queue is full.
    for (int n = 0; n < 512 && output.pop(c); n++)
        terminal.feed(c);
    uint32_t now = millis();
    if (uint32_t(now - blinkAt) >= 500) {
        blinkAt = now;
        cursorPhase = !cursorPhase;
        // Cursor and SGR-5 text share the phase, so repaint every visible
        // blinking cell when the phase changes.
        for (int i = 0; i < vt::Cells; ++i) {
            if (terminal.flags[i] & vt::Blink)
                terminal.dirty[i] = true;
        }
        terminal.dirty[terminal.y * vt::Cols + terminal.x] = true;
    }
    if (terminal.bells != oldBells) {
        oldBells = terminal.bells;
        bellUntil = now + 150;
        terminal.indicatorDirty = true;
    }
    if (bellUntil && int32_t(now - bellUntil) >= 0) {
        bellUntil = 0;
        terminal.indicatorDirty = true;
    }
    static uint32_t observedOverruns = 0;
    if (observedOverruns != replyOverruns) {
        observedOverruns = replyOverruns;
        terminal.indicatorDirty = true;
    }
    renderBatch();
    mirrorPump();
}
