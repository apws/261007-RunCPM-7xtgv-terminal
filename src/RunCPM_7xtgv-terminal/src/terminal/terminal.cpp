#include "terminal.h"
#include <algorithm>
#include <stdio.h>
#include <string.h>

namespace vt {
const RGB (&Ink)[16] = mixworx::Ink;
const RGB (&Paper)[4] = mixworx::Paper;
static uint8_t ansiInk(int code) {
    static const uint8_t map[8] = {0, 1, 2, 7, 3, 6, 5, 4};
    return map[code & 7];
}
static uint8_t ansiDarkInk(int code) {
    return ansiInk(code) | 8;
}
static uint8_t selectedInk(uint8_t ink, bool bold) {
    return bold ? uint8_t(ink | 8) : ink;
}
Terminal::Terminal(Reply reply, void *context) : reply_(reply), context_(context) { reset(); }
void Terminal::changed(int i) {
    dirty[i] = true;
    mirrorDirty[i / MaxCols] = true;
}
void Terminal::invalidate() {
    std::fill(dirty, dirty + Cells, true);
    invalidateMirror();
}
void Terminal::invalidateMirror() {
    // The mirror sender visits only active rows.  Clear excluded maximum
    // rows when geometry shrinks so a snapshot can actually finish.
    std::fill(mirrorDirty, mirrorDirty + MaxRows, false);
    std::fill(mirrorDirty, mirrorDirty + rows, true);
    mirrorCursorDirty = indicatorDirty = true;
}
void Terminal::plot(PlotEvent event, uint16_t value) {
    if (plot_)
        plot_(plotContext_, event, value);
}
uint16_t Terminal::xterm565(int index) {
    static const uint8_t base[16][3] = {
        {0, 0, 0},       {205, 0, 0},   {0, 205, 0},   {205, 205, 0},
        {0, 0, 238},     {205, 0, 205}, {0, 205, 205}, {229, 229, 229},
        {127, 127, 127}, {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
        {92, 92, 255},   {255, 0, 255}, {0, 255, 255}, {255, 255, 255},
    };
    uint8_t r, g, b;
    if (index < 16) {
        r = base[index][0];
        g = base[index][1];
        b = base[index][2];
    } else if (index < 232) {
        static const uint8_t cube[6] = {0, 95, 135, 175, 215, 255};
        const int value = index - 16;
        r = cube[value / 36];
        g = cube[(value / 6) % 6];
        b = cube[value % 6];
    } else {
        r = g = b = uint8_t(8 + 10 * (index - 232));
    }
    return uint16_t((uint16_t(r >> 3) << 11) | (uint16_t(g >> 2) << 5) | (b >> 3));
}
void Terminal::beginPlot(uint8_t requestedZoom, uint16_t requestedX, uint16_t requestedY) {
    // GCLS is a temporary pixel transaction, not an output-mode switch.
    state = Ground;
    wrapPending = false;
    abortPlot();
    plotState = PlotReady;
    plotInk = 0;
    plotPaper = 0xffff;
    plotStarted = false;
    plotLogicalRow = false;
    plot(PlotConfigZoom, requestedZoom);
    plot(PlotConfigX, requestedX);
    plot(PlotConfigY, requestedY);
    plot(PlotBegin);
}
void Terminal::abortPlot() {
    if (plotState == PlotOff)
        return;
    plotState = PlotOff;
    plotStarted = false;
    plot(PlotAbort);
}
void Terminal::finishTransientOutput() {
    if (plotState != PlotOff) {
        if (plotStarted) {
            plotState = PlotOff;
            plotStarted = false;
            plot(PlotEnd);
        } else
            abortPlot();
    }
    state = Ground;
    wrapPending = false;
}
void Terminal::reset() {
    abortPlot();
    x = y = viewport = rowViewport = top = 0;
    bottom = rows - 1;
    ansi = cursorVisible = autowrap = true;
    applicationCursor = applicationKeypad = origin = wrapPending = bold = g0 = g1 = useG1 = false;
    ink = 0;
    paper = rendition = 0;
    state = Ground;
    bells = 0;
    for (int i = 0; i < MaxCols; i++)
        tabs[i] = (i % 8 == 0);
    for (int i = 0; i < MaxCells; i++) {
        paperRAM[i] = charRAM[i] = ' ';
        attrRAM[i] = 0;
        flags[i] = 0;
    }
    save();
    invalidate();
}
bool Terminal::setGeometry(int activeCols, int activeRows, int visibleColumns,
                           int visibleRowCount, int scale) {
    if (activeCols < 1 || activeCols > MaxCols || activeRows < 1 || activeRows > MaxRows ||
        visibleColumns < 1 || visibleColumns > activeCols ||
        visibleRowCount < 1 || visibleRowCount > activeRows || scale < 1 || scale > 4)
        return false;
    abortPlot();
    const int oldCols = cols, oldRows = rows;
    cols = activeCols;
    rows = activeRows;
    visibleCols = visibleColumns;
    visibleRows = visibleRowCount;
    zoom = scale;
    // Cells outside the preserved intersection must never reappear after a
    // later enlargement.  Reset them to ordinary terminal blanks.
    for (int row = 0; row < MaxRows; ++row)
        for (int col = 0; col < MaxCols; ++col)
            if (row >= rows || col >= cols || row >= oldRows || col >= oldCols) {
                const int i = index(col, row);
                paperRAM[i] = charRAM[i] = ' ';
                attrRAM[i] = 0;
                flags[i] = 0;
            }
    x = std::max(0, std::min(cols - 1, x));
    y = std::max(0, std::min(rows - 1, y));
    savedX = std::max(0, std::min(cols - 1, savedX));
    savedY = std::max(0, std::min(rows - 1, savedY));
    top = 0;
    bottom = rows - 1;
    viewport = rowViewport = 0;
    compositionDirty = true;
    wrapPending = false;
    state = Ground;
    for (int i = 0; i < MaxCols; ++i)
        tabs[i] = (i % 8 == 0);
    invalidate();
    return true;
}
bool Terminal::setView(int visibleColumns, int visibleRowCount, int scale) {
    if (visibleColumns < 1 || visibleColumns > cols ||
        visibleRowCount < 1 || visibleRowCount > rows || scale < 1 || scale > 4)
        return false;
    if (visibleCols == visibleColumns && visibleRows == visibleRowCount && zoom == scale)
        return true;
    abortPlot();
    visibleCols = visibleColumns;
    visibleRows = visibleRowCount;
    zoom = scale;
    viewport = std::min(viewport, viewportLimit());
    rowViewport = std::min(rowViewport, rowViewportLimit());
    compositionDirty = indicatorDirty = true;
    std::fill(dirty, dirty + Cells, true);
    return true;
}
void Terminal::setMode(bool a) {
    abortPlot();
    ansi = a;
    state = Ground;
    wrapPending = false;
    applicationCursor = false;
}
void Terminal::pan(int offset) {
    offset = std::max(0, std::min(viewportLimit(), offset));
    if (offset != viewport) {
        viewport = offset;
        std::fill(dirty, dirty + Cells, true);
        indicatorDirty = compositionDirty = true;
    }
}
void Terminal::panRows(int offset) {
    offset = std::max(0, std::min(rowViewportLimit(), offset));
    if (offset != rowViewport) {
        rowViewport = offset;
        std::fill(dirty, dirty + Cells, true);
        indicatorDirty = compositionDirty = true;
    }
}
static int steppedViewport(int current, int total, int visible, int direction) {
    const int limit = total > visible ? total - visible : 0;
    if (!limit || !direction)
        return 0;
    // Use enough evenly distributed views to cover the logical plane. This
    // avoids a nearly duplicate final sliver when total is not a multiple of
    // visible, while always including both exact edges.
    const int views = (total + visible - 1) / visible;
    const int segments = views - 1;
    if (direction > 0) {
        for (int step = 1; step <= segments; ++step) {
            const int position = (limit * step + segments / 2) / segments;
            if (position > current)
                return position;
        }
        return limit;
    }
    for (int step = segments - 1; step >= 0; --step) {
        const int position = (limit * step + segments / 2) / segments;
        if (position < current)
            return position;
    }
    return 0;
}
void Terminal::panHorizontal(int direction) {
    pan(steppedViewport(viewport, cols, visibleCols, direction));
}
void Terminal::panVertical(int direction) {
    panRows(steppedViewport(rowViewport, rows, visibleRows, direction));
}
void Terminal::panOverview(int direction) {
    if (!direction) return;
    const int nextX = steppedViewport(viewport, cols, visibleCols, direction);
    if (nextX != viewport) {
        pan(nextX);
        return;
    }
    const int nextY = steppedViewport(rowViewport, rows, visibleRows, direction);
    if (nextY != rowViewport) {
        panRows(nextY);
        pan(direction > 0 ? 0 : viewportLimit());
    }
}
RGB Terminal::foreground(int i) const {
    return (flags[i] & Reverse) ? palette.paper[(attrRAM[i] >> 4) & 3] : palette.ink[attrRAM[i] & 15];
}
RGB Terminal::background(int i) const {
    return (flags[i] & Reverse) ? palette.ink[attrRAM[i] & 15] : palette.paper[(attrRAM[i] >> 4) & 3];
}
void Terminal::move(int col, int row) {
    changed(index(x, y));
    x = std::max(0, std::min(cols - 1, col));
    y = std::max(0, std::min(rows - 1, row));
    changed(index(x, y));
    wrapPending = false;
    mirrorCursorDirty = indicatorDirty = true;
}
void Terminal::blank(int i) {
    paperRAM[i] = charRAM[i] = ' ';
    attrRAM[i] = (paper << 4) | selectedInk(ink, bold);
    flags[i] = rendition;
    changed(i);
}
void Terminal::erase(int firstRow, int firstCol, int lastRow, int lastCol) {
    firstRow = std::max(0, firstRow); lastRow = std::min(rows - 1, lastRow);
    if (firstRow > lastRow) return;
    for (int row = firstRow; row <= lastRow; ++row) {
        const int begin = row == firstRow ? std::max(0, firstCol) : 0;
        const int end = row == lastRow ? std::min(cols - 1, lastCol) : cols - 1;
        for (int col = begin; col <= end; ++col)
            blank(index(col, row));
    }
}
void Terminal::eraseDisplay(bool beforeCursor, bool afterCursor) {
    if (beforeCursor)
        erase(0, 0, y, x);
    if (afterCursor)
        erase(y, x, rows - 1, cols - 1);
}
void Terminal::shiftLines(int firstRow, int count, bool insert) {
    if (firstRow < top || firstRow > bottom || count <= 0)
        return;
    count = std::min(count, bottom - firstRow + 1);
    if (insert) {
        for (int row = bottom; row >= firstRow + count; --row) {
            const int dst = index(0, row), src = index(0, row - count);
            memcpy(paperRAM + dst, paperRAM + src, cols);
            memcpy(charRAM + dst, charRAM + src, cols);
            memcpy(attrRAM + dst, attrRAM + src, cols);
            memcpy(flags + dst, flags + src, cols);
        }
        erase(firstRow, 0, firstRow + count - 1, cols - 1);
    } else {
        for (int row = firstRow; row <= bottom - count; ++row) {
            const int dst = index(0, row), src = index(0, row + count);
            memcpy(paperRAM + dst, paperRAM + src, cols);
            memcpy(charRAM + dst, charRAM + src, cols);
            memcpy(attrRAM + dst, attrRAM + src, cols);
            memcpy(flags + dst, flags + src, cols);
        }
        erase(bottom - count + 1, 0, bottom, cols - 1);
    }
    for (int row = firstRow; row <= bottom; ++row)
        for (int col = 0; col < cols; ++col)
            changed(index(col, row));
    // A completed local plot is no longer retained once ordinary terminal
    // scrolling repaints its cells. Let the display restore its gutter once.
    plot(PlotScroll);
}
void Terminal::scroll(int delta) {
    shiftLines(top, 1, delta < 0);
}
void Terminal::index(bool reverse) {
    if (reverse) {
        if (y == top)
            scroll(-1);
        else
            move(x, y - 1);
    } else {
        if (y == bottom)
            scroll(1);
        else
            move(x, y + 1);
    }
    wrapPending = false;
}
void Terminal::put(uint8_t c) {
    if (plotState == PlotSampleArmed) {
        plotStarted = true;
        plot(PlotSample, plotInk);
        plotState = PlotReady;
        return;
    }
    if (wrapPending && autowrap) {
        move(0, y);
        index();
    }
    int i = index(x, y);
    paperRAM[i] = ' ';
    charRAM[i] = (c < 128 ? c : '?');
    attrRAM[i] = (paper << 4) | selectedInk(ink, bold);
    flags[i] = rendition | ((useG1 ? g1 : g0) && c >= 0x5f && c <= 0x7e ? Graphics : 0);
    changed(i);
    if (x == cols - 1)
        wrapPending = autowrap;
    else
        move(x + 1, y);
    mirrorCursorDirty = true;
}
void Terminal::save() {
    savedX = x;
    savedY = y;
    savedInk = ink;
    savedPaper = paper;
    savedRendition = rendition;
    savedBold = bold;
    savedOrigin = origin;
    savedWrap = wrapPending;
    savedG0 = g0;
    savedG1 = g1;
    savedUseG1 = useG1;
}
void Terminal::restore() {
    ink = savedInk;
    paper = savedPaper;
    rendition = savedRendition;
    bold = savedBold;
    origin = savedOrigin;
    g0 = savedG0;
    g1 = savedG1;
    useG1 = savedUseG1;
    move(savedX, savedY);
    wrapPending = savedWrap;
}
void Terminal::send(const char *s) {
    if (reply_)
        reply_(context_, reinterpret_cast<const uint8_t *>(s), strlen(s));
}
int Terminal::param(int n, int fallback) const {
    return n < count && params[n] > 0 ? params[n] : fallback;
}
void Terminal::sgr() {
    bool finishPlot = false;
    for (int j = 0; j < count; j++) {
        int p = params[j] < 0 ? 0 : params[j];
        if (p == 0) {
            ink = 0;
            paper = 0;
            rendition = 0;
            bold = false;
            finishPlot = plotting();
        } else if (p == 1)
            bold = true;
        else if (p == 22)
            bold = false;
        else if (p == 4)
            rendition |= Underline;
        else if (p == 24)
            rendition &= ~Underline;
        else if (p == 5)
            rendition |= Blink;
        else if (p == 25)
            rendition &= ~Blink;
        else if (p == 7)
            rendition |= Reverse;
        else if (p == 27)
            rendition &= ~Reverse;
        else if (p == 39) {
            ink = 0;
            plotInk = 0;
        } else if (p == 49) {
            paper = 0;
            plotPaper = 0xffff;
        }
        else if (p >= 30 && p <= 37)
            ink = ansiInk(p - 30);
        else if (p >= 90 && p <= 97)
            ink = ansiDarkInk(p - 90);
        else if (p >= 40 && p <= 47) {
            // Preserve the familiar terminal white/red/green/blue paper
            // choices instead of letting ANSI gray select pastel green by
            // numerical RGB distance.
            static const uint8_t paperMap[8] = {2, 1, 2, 2, 3, 1, 2, 0};
            paper = paperMap[p - 40];
        } else if (p >= 100 && p <= 107) {
            static const uint8_t brightPaperMap[8] = {2, 1, 2, 2, 3, 1, 2, 0};
            paper = brightPaperMap[p - 100];
        }
        // Consume unsupported extended colors as a unit, never as bold/reverse commands.
        else if (p == 38 || p == 48) {
            if (j + 1 < count && params[j + 1] == 5) {
                if (j + 2 < count && params[j + 2] >= 0 && params[j + 2] <= 255) {
                    if (p == 38 && plotState != PlotOff) {
                        plotInk = xterm565(params[j + 2]);
                        plotState = PlotSampleArmed;
                    } else if (p == 48 && plotState != PlotOff) {
                        plotPaper = xterm565(params[j + 2]);
                    }
                }
                j = std::min(count - 1, j + 2);
            } else if (j + 1 < count && params[j + 1] == 2)
                j = std::min(count - 1, j + 4);
        }
    }
    if (finishPlot) {
        plotState = PlotOff;
        plot(PlotEnd);
    }
}
void Terminal::beginCSI() {
    state = CSI;
    count = 1;
    params[0] = -1;
    prefix = 0;
    invalid = false;
}
void Terminal::dispatchPrivate(uint8_t f) {
    if (prefix == '>') {
        if (f == 'v' && params[0] == 4 && count >= 1 && count <= 4) {
            const int requestedZoom = count >= 2 && params[1] >= 0 ? params[1] : 1;
            const int requestedX = count >= 3 && params[2] >= 0 ? params[2] : 0;
            const int requestedY = count >= 4 && params[3] >= 0 ? params[3] : 0;
            if (requestedZoom >= 1 && requestedZoom <= 4 && requestedX <= 9999 &&
                requestedY <= 9999)
                beginPlot(uint8_t(requestedZoom), uint16_t(requestedX), uint16_t(requestedY));
        }
        else if (f == 'v' && count == 2 && params[0] == 63 && params[1] >= 0 && params[1] <= 34)
            control(Backlight, uint8_t(params[1]));
        else if (f == 'v' && count == 2 && params[0] == 68 && params[1] >= 1 && params[1] <= 4)
            control(Zoom, uint8_t(params[1]));
        else if (f == 'v' && count == 2 && params[0] == 69 && params[1] >= 0 && params[1] <= 9)
            control(Palette, uint8_t(params[1]));
        else if (f == 'v' && count == 1 && params[0] == 72)
            control(QueryInfo, 0);
        else if (f == 'v' && count == 2 && params[0] == 73 &&
                 params[1] >= 0 && params[1] <= 4)
            control(LoadPreset, uint8_t(params[1]));
        else if (f == 'v' && count == 4 && params[0] == 73 &&
                 params[1] >= 1 && params[1] <= 4 &&
                 params[2] >= 2 && params[2] <= MaxCols && !(params[2] & 1) &&
                 params[3] >= 1 && params[3] <= MaxRows)
            control(SetGuestGeometry, uint8_t(params[1]),
                    uint16_t(params[2]), uint16_t(params[3]));
    }
}
void Terminal::dispatch(uint8_t f) {
    if (invalid)
        return;
    if (prefix == '>') {
        dispatchPrivate(f);
        return;
    }
    // VT52 accepts our reserved private controls and only the exact SGR
    // forms emitted by the plot stream. Ordinary ANSI commands remain inert.
    if (!ansi) {
        if (prefix || f != 'm' || !plotting())
            return;
        const bool end = count == 1 && params[0] <= 0;
        const bool color = count == 3 && (params[0] == 38 || params[0] == 48) &&
                           params[1] == 5 && params[2] >= 0 && params[2] <= 255;
        if (!end && !color)
            return;
        sgr();
        return;
    }
    int n = param(0), lo = origin ? top : 0, hi = origin ? bottom : rows - 1;
    if (prefix == '?') {
        if (f != 'h' && f != 'l')
            return;
        bool on = f == 'h';
        for (int j = 0; j < count; j++)
            switch (params[j]) {
            case 1:
                applicationCursor = on;
                break;
            case 2:
                setMode(on);
                break;
            case 6:
                origin = on;
                move(0, on ? top : 0);
                break;
            case 7:
                autowrap = on;
                wrapPending = false;
                break;
            case 25:
                cursorVisible = on;
                changed(index(x, y));
                mirrorCursorDirty = true;
                break;
            }
        return;
    }
    if (prefix)
        return;
    switch (f) {
    case 'A':
        move(x, std::max(lo, y - n));
        break;
    case 'B':
        move(x, std::min(hi, y + n));
        break;
    case 'C':
        move(x + n, y);
        break;
    case 'D':
        move(x - n, y);
        break;
    case 'E':
        move(0, std::min(hi, y + n));
        break;
    case 'F':
        move(0, std::max(lo, y - n));
        break;
    case 'G':
    case '`':
        move(n - 1, y);
        break;
    case 'd':
        move(x, std::min(hi, lo + n - 1));
        break;
    case 'H':
    case 'f':
        move(param(1) - 1, std::min(hi, lo + n - 1));
        break;
    case 'J':
        if (params[0] <= 0)
            eraseDisplay(false, true);
        else if (params[0] == 1)
            eraseDisplay(true, false);
        else if (params[0] == 2) {
            erase(0, 0, rows - 1, cols - 1);
            control(FullClear, 0);
        }
        wrapPending = false;
        break;
    case 'K':
        if (params[0] <= 0)
            erase(y, x, y, cols - 1);
        else if (params[0] == 1)
            erase(y, 0, y, x);
        else if (params[0] == 2)
            erase(y, 0, y, cols - 1);
        wrapPending = false;
        break;
    case 'L':
    case 'M':
        if (y >= top && y <= bottom) {
            shiftLines(y, n, f == 'L');
            move(0, y);
        }
        break;
    case 'm':
        sgr();
        break;
    case 'r': {
        int a = param(0) - 1, b = param(1, rows) - 1;
        if (a >= 0 && b < rows && a < b) {
            top = a;
            bottom = b;
            move(0, origin ? top : 0);
        }
        break;
    }
    case 'g':
        if (params[0] <= 0)
            tabs[x] = false;
        else if (params[0] == 3)
            std::fill(tabs, tabs + cols, false);
        break;
    case 's':
        save();
        break;
    case 'u':
        restore();
        break;
    case 'n':
        if (params[0] == 5)
            send("\033[0n");
        else if (params[0] == 6) {
            char b[32];
            snprintf(b, sizeof(b), "\033[%d;%dR", y - (origin ? top : 0) + 1, x + 1);
            send(b);
        }
        break;
    case 'c':
        if (params[0] <= 0)
            send("\033[?1;0c");
        break;
    }
}
void Terminal::designate(uint8_t c) {
    if (designateG1)
        g1 = (c == '0');
    else
        g0 = (c == '0');
}
void Terminal::feed(uint8_t c) {
    if (plotState == PlotSampleArmed && c < 32)
        plotState = PlotReady; // A control sequence cannot also be a sample.
    if (c == 24 || c == 26) {
        state = Ground;
        return;
    }
    if (c == 127)
        return;
    if (state == String || state == StringEscape) {
        if (c == 7 || (state == StringEscape && c == '\\'))
            state = Ground;
        else
            state = (c == 27 ? StringEscape : String);
        return;
    }
    if (c == 27) {
        state = Escape;
        return;
    }
    // VT52 addressing includes bytes >= 0x20 only.
    if (c < 32) {
        // GCLS is only armed until its first actual xterm sample.  CCP/SUBMIT
        // output in that window is ordinary ANSI text, including its CR/LF.
        // Once plotting has begun, CR/LF belongs to the pixel stream.
        if (plotState != PlotOff && plotStarted) {
            if (c == 13)
                return;
            if (c == 10) {
                if (!plotLogicalRow) {
                    move(0, y);
                    index();
                    plotLogicalRow = true;
                }
                plot(PlotRow);
                return;
            }
        }
        switch (c) {
        case 7:
            bells++;
            indicatorDirty = true;
            break;
        case 8:
            move(x - 1, y);
            break;
        case 9: {
            int next = x + 1;
            while (next < cols - 1 && !tabs[next])
                next++;
            move(next, y);
            break;
        }
        case 10:
        case 11:
        case 12:
            index();
            break;
        case 13:
            move(0, y);
            break;
        case 14:
            useG1 = true;
            break;
        case 15:
            useG1 = false;
            break;
        }
        return;
    }
    if (state == VTAddressRow) {
        addressRow = int(c) - 32;
        state = VTAddressCol;
        return;
    }
    if (state == VTAddressCol) {
        move(int(c) - 32, addressRow);
        state = Ground;
        return;
    }
    // C1 CSI is an ANSI ground-state control, not a VT52 address byte or a
    // printable upper-half glyph. Plot sample markers remain opaque bytes.
    if (state == Ground && ansi && plotState != PlotSampleArmed && c == 0x9b) {
        beginCSI();
        return;
    }
    if (state == Charset) {
        designate(c);
        state = Ground;
        return;
    }
    if (state == Hash) {
        if (c == '8') {
            for (int row = 0; row < rows; ++row)
                for (int col = 0; col < cols; ++col) {
                    const int i = index(col, row);
                    blank(i); charRAM[i] = 'E';
                }
        }
        state = Ground;
        return;
    }
    if (state == Escape) {
        state = Ground;
        if (c == ']' || c == 'P' || c == '^' || c == '_') {
            state = String;
            return;
        }
        if (!ansi) {
            if (c == '[') {
                beginCSI();
                return;
            }
            switch (c) {
            case 'A':
                move(x, y - 1);
                break;
            case 'B':
                move(x, y + 1);
                break;
            case 'C':
                move(x + 1, y);
                break;
            case 'D':
                move(x - 1, y);
                break;
            case 'H':
                move(0, 0);
                break;
            case 'I':
                index(true);
                break;
            case 'J':
                eraseDisplay(false, true);
                break;
            case 'K':
                erase(y, x, y, cols - 1);
                break;
            case 'Y':
                state = VTAddressRow;
                break;
            case 'Z':
                send("\033/Z");
                break;
            case 'F':
                g0 = true;
                useG1 = false;
                break;
            case 'G':
                g0 = false;
                useG1 = false;
                break;
            case '=':
                applicationKeypad = true;
                break;
            case '>':
                applicationKeypad = false;
                break;
            case '<':
                setMode(true);
                break;
            }
            return;
        }
        switch (c) {
        case '[':
            beginCSI();
            break;
        case '(':
        case ')':
            state = Charset;
            designateG1 = (c == ')');
            break;
        case '#':
            state = Hash;
            break;
        case '7':
            save();
            break;
        case '8':
            restore();
            break;
        case 'D':
            index();
            break;
        case 'M':
            index(true);
            break;
        case 'E':
            move(0, y);
            index();
            break;
        case 'H':
            tabs[x] = true;
            break;
        case 'c':
            reset();
            break;
        case 'Z':
            send("\033[?1;0c");
            break;
        case '=':
            applicationKeypad = true;
            break;
        case '>':
            applicationKeypad = false;
            break;
        }
        return;
    }
    if (state == CSI) {
        if (c >= 0x40 && c <= 0x7e) {
            dispatch(c);
            state = Ground;
            return;
        }
        if (c >= '0' && c <= '9') {
            int &p = params[count - 1];
            if (p < 0)
                p = 0;
            p = std::min(9999, p * 10 + c - '0');
        } else if (c == ';') {
            if (count < 16)
                params[count++] = -1;
            else
                invalid = true;
        } else if ((c == '?' || c == '>') && count == 1 && params[0] < 0 && !prefix)
            prefix = c;
        else
            invalid = true;
        return;
    }
    put(c);
}
void Keyboard::output(const char *s) {
    send_(context_, reinterpret_cast<const uint8_t *>(s), strlen(s));
}
void Keyboard::flush() {
    if (size_)
        send_(context_, buffer_, size_);
    size_ = 0;
}
// Versioned host-only event: CSI >70;1;<base>;<modifiers>;<fallback-DDD...>v.
// The original host bytes travel with the event so an unbound key is safe.
// Three decimal digits per byte keep the CSI body below 0x40 until final 'v'.
bool decodePrivateKeyEvent(const uint8_t *s, size_t size, KeyChord &chord,
                           uint8_t fallback[16], size_t &fallbackLength) {
    if (size < 13 || memcmp(s, "\033[>70;1;", 8) || s[size - 1] != 'v')
        return false;
    size_t pos = 8;
    auto decimal = [&](unsigned &value) {
        value = 0;
        size_t digits = 0;
        while (pos < size && s[pos] >= '0' && s[pos] <= '9') {
            value = value * 10 + s[pos++] - '0';
            if (++digits > 3 || value > 255) return false;
        }
        if (!digits || pos >= size || s[pos++] != ';') return false;
        return true;
    };
    unsigned base = 0, modifiers = 0;
    if (!decimal(base) || !decimal(modifiers)) return false;
    chord = {uint8_t(base), uint8_t(modifiers)};
    if (!validKeyChord(chord)) return false;
    const size_t digitsLength = size - 1 - pos;
    if (digitsLength > 48 || digitsLength % 3) return false;
    fallbackLength = digitsLength / 3;
    for (size_t i = 0; i < fallbackLength; ++i) {
        unsigned value = 0;
        for (size_t digit = 0; digit < 3; ++digit) {
            const uint8_t c = s[pos + 3 * i + digit];
            if (c < '0' || c > '9') return false;
            value = value * 10 + c - '0';
        }
        if (value > 255) return false;
        fallback[i] = uint8_t(value);
    }
    return true;
}
void Keyboard::finish() {
    buffer_[size_] = 0;
    const char *s = reinterpret_cast<const char *>(buffer_);
    KeyChord chord;
    uint8_t fallback[16];
    size_t fallbackLength = 0;
    if (decodePrivateKeyEvent(buffer_, size_, chord, fallback, fallbackLength)) {
        if (keyEvent_) keyEvent_(context_, chord, fallback, fallbackLength);
        else if (fallbackLength) send_(context_, fallback, fallbackLength);
    }
    else if (!strcmp(s, "\033[1;3H") || !strcmp(s, "\033[1;3~") || !strcmp(s, "\033[7;3~") ||
        !strcmp(s, "\033\033[H"))
        t_.panHorizontal(-1);
    else if (!strcmp(s, "\033[1;3F") || !strcmp(s, "\033[4;3~") || !strcmp(s, "\033[8;3~") ||
             !strcmp(s, "\033\033[F"))
        t_.panHorizontal(1);
    else if (!strcmp(s, "\033[>71;0v"))
        t_.panVertical(-1);
    else if (!strcmp(s, "\033[>71;1v"))
        t_.panVertical(1);
    else if (!strcmp(s, "\033[>0v")) {
        t_.abortPlot();
        t_.invalidate();
    }
    else if (!strcmp(s, "\033[>1v"))
        t_.setMode(true);
    else if (!strcmp(s, "\033[>2v"))
        t_.setMode(false);
    else if (!strcmp(s, "\033[>4v"))
        t_.beginPlot();
    else if (!strcmp(s, "\033[>10v")) {
        if (act_) act_(context_, HistoryPrevious);
    } else if (!strcmp(s, "\033[>11v")) {
        if (act_) act_(context_, HistoryNext);
    } else if (!strcmp(s, "\033[>65v")) {
        if (act_) act_(context_, HistoryDelete);
    } else if (!strcmp(s, "\033[>12v")) {
        if (act_) act_(context_, HistoryBankPrevious);
    } else if (!strcmp(s, "\033[>13v")) {
        if (act_) act_(context_, HistoryBankNext);
    } else if (!strcmp(s, "\033[>14v")) {
        if (act_) act_(context_, HistoryBankShow);
    } else if (!strcmp(s, "\033[>15v")) {
        if (act_) act_(context_, RawToggle);
    } else if (!strcmp(s, "\033[>16v")) {
        if (act_) act_(context_, RawOn);
    } else if (!strcmp(s, "\033[>17v")) {
        if (act_) act_(context_, RawOff);
    } else if (!strcmp(s, "\033[>60v")) {
        if (act_) act_(context_, ProfileExit);
    } else if (!strcmp(s, "\033[>61v")) {
        if (act_) act_(context_, ProfileClear);
    } else if (!strcmp(s, "\033[>62v")) {
        if (act_) act_(context_, MirrorSnapshot);
    } else if (!strcmp(s, "\033[>66v")) {
        if (act_) act_(context_, ZoomIn);
    } else if (!strcmp(s, "\033[>67v")) {
        if (act_) act_(context_, ZoomOut);
    } else if ((size_ == 8 || size_ == 9) && !memcmp(s, "\033[>63;", 6) && s[size_ - 1] == 'v') {
        int value = -1;
        if (size_ == 8 && s[6] >= '0' && s[6] <= '9') value = s[6] - '0';
        else if (size_ == 9 && s[6] >= '1' && s[6] <= '3' && s[7] >= '0' && s[7] <= '9')
            value = (s[6] - '0') * 10 + s[7] - '0';
        // 0..32 set a direct level; 33/34 are one step up/down.
        if (value >= 0 && value <= 34 && act_) act_(context_, Action(Backlight0 + value));
    } else if (size_ == 6 && !memcmp(s, "\033[>2", 4) && s[4] >= '0' && s[4] <= '9' && s[5] == 'v') {
        if (act_) act_(context_, Action(HistoryBank0 + s[4] - '0'));
    } else if (size_ == 6 && !memcmp(s, "\033[>", 3) && s[3] >= '3' && s[3] <= '5' &&
               s[4] >= '0' && s[4] <= '9' && s[5] == 'v') {
        if (act_) act_(context_, Action(ProfileKey0 + (s[3] - '3') * 10 + s[4] - '0'));
    }
    else if (size_ == 3 && (buffer_[1] == '[' || buffer_[1] == 'O') && buffer_[2] >= 'A' &&
             buffer_[2] <= 'D') {
        char out[4] = {27, '[', char(buffer_[2]), 0};
        if (t_.applicationCursor)
            out[1] = 'O';
        output(out);
    } else if (size_ == 3 && buffer_[1] == 'O' && buffer_[2] >= 'p' && buffer_[2] <= 'y') {
        if (t_.applicationKeypad) {
            char out[5] = {27, 'O', char(buffer_[2]), 0};
            output(out);
        } else {
            uint8_t c = '0' + buffer_[2] - 'p';
            send_(context_, &c, 1);
        }
    } else if (size_ == 3 && buffer_[1] == 'O' &&
               (buffer_[2] == 'M' || buffer_[2] == 'n' || buffer_[2] == 'm' || buffer_[2] == 'l')) {
        if (t_.applicationKeypad) {
            char out[4] = {27, 'O', char(buffer_[2]), 0};
            output(out);
        } else {
            uint8_t c = buffer_[2] == 'M'   ? 13
                        : buffer_[2] == 'n' ? '.'
                        : buffer_[2] == 'm' ? '-'
                                            : ',';
            send_(context_, &c, 1);
        }
    } else {
        flush();
        return;
    }
    size_ = 0;
}
void Keyboard::feed(uint8_t c, uint32_t now) {
    tick(now);
    last_ = now;
    if (!size_) {
        if (c == 27) {
            buffer_[size_++] = c;
            return;
        }
        if (c == 127)
            c = 8;
        if (c == 10)
            c = 13;
        if (c == 8 && act_) {
            act_(context_, ProfileBackspace);
            return;
        }
        send_(context_, &c, 1);
        return;
    }
    if (size_ == sizeof(buffer_) - 1)
        flush();
    buffer_[size_++] = c;
    size_t start = (size_ > 1 && buffer_[1] == 27) ? 2 : 1;
    if (size_ <= start)
        return;
    if (buffer_[start] != '[' && buffer_[start] != 'O') {
        flush();
        return;
    }
    if (size_ > start + 1 && c >= 0x40 && c <= 0x7e)
        finish();
}
void Keyboard::tick(uint32_t now) {
    if (size_ && uint32_t(now - last_) >= 50)
        flush();
}
} // namespace vt
