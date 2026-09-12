#include "terminal.h"
#include <algorithm>
#include <stdio.h>
#include <string.h>

namespace vt {
const RGB (&Ink)[16] = mixworx::Ink;
const RGB (&Paper)[4] = mixworx::Paper;
static uint8_t ansiInk(int code) {
    static const uint8_t map[8] = {0, 2, 4, 14, 6, 12, 10, 8};
    return map[code & 7];
}
static uint8_t ansiDarkInk(int code) {
    static const uint8_t map[8] = {1, 3, 5, 15, 7, 13, 11, 9};
    return map[code & 7];
}
static uint8_t selectedInk(uint8_t ink, bool bold) {
    // Even entries are the eight lighter inks; their odd companions are the
    // darker bold inks. Gray/black retain their dedicated entries.
    return bold && !(ink & 1) ? uint8_t(ink + 1) : ink;
}
Terminal::Terminal(Reply reply, void *context) : reply_(reply), context_(context) { reset(); }
void Terminal::changed(int i) {
    dirty[i] = true;
    mirrorDirty[i / Cols] = true;
}
void Terminal::invalidate() {
    std::fill(dirty, dirty + Cells, true);
    std::fill(mirrorDirty, mirrorDirty + Rows, true);
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
void Terminal::beginPlot() {
    // The private GCLS command always selects ANSI before arming one plot.
    ansi = true;
    state = Ground;
    wrapPending = false;
    applicationCursor = false;
    abortPlot();
    plotState = PlotReady;
    plotInk = 0;
    plotPaper = 0xffff;
    plotStarted = false;
    plotLogicalRow = false;
    plot(PlotBegin);
}
void Terminal::abortPlot() {
    if (plotState == PlotOff)
        return;
    plotState = PlotOff;
    plotStarted = false;
    plot(PlotAbort);
}
void Terminal::reset() {
    abortPlot();
    x = y = viewport = top = 0;
    bottom = Rows - 1;
    ansi = cursorVisible = autowrap = true;
    applicationCursor = applicationKeypad = origin = wrapPending = bold = g0 = g1 = useG1 = false;
    ink = 1;
    paper = rendition = 0;
    state = Ground;
    bells = 0;
    for (int i = 0; i < Cols; i++)
        tabs[i] = (i % 8 == 0);
    for (int i = 0; i < Cells; i++) {
        paperRAM[i] = charRAM[i] = ' ';
        attrRAM[i] = 1;
        flags[i] = 0;
    }
    save();
    invalidate();
}
void Terminal::setMode(bool a) {
    abortPlot();
    ansi = a;
    state = Ground;
    wrapPending = false;
    applicationCursor = false;
}
void Terminal::pan(int offset) {
    offset = std::max(0, std::min(Cols - Visible, offset));
    if (offset != viewport) {
        viewport = offset;
        std::fill(dirty, dirty + Cells, true);
        indicatorDirty = true;
    }
}
RGB Terminal::foreground(int i) const {
    return (flags[i] & Reverse) ? Paper[(attrRAM[i] >> 4) & 3] : Ink[attrRAM[i] & 15];
}
RGB Terminal::background(int i) const {
    return (flags[i] & Reverse) ? Ink[attrRAM[i] & 15] : Paper[(attrRAM[i] >> 4) & 3];
}
void Terminal::move(int col, int row) {
    changed(y * Cols + x);
    x = std::max(0, std::min(Cols - 1, col));
    y = std::max(0, std::min(Rows - 1, row));
    changed(y * Cols + x);
    wrapPending = false;
    mirrorCursorDirty = indicatorDirty = true;
}
void Terminal::blank(int i) {
    paperRAM[i] = charRAM[i] = ' ';
    attrRAM[i] = (paper << 4) | selectedInk(ink, bold);
    flags[i] = rendition;
    changed(i);
}
void Terminal::erase(int first, int last) {
    for (int i = first; i <= last; i++)
        blank(i);
}
void Terminal::scroll(int delta) {
    int a = top * Cols, b = (bottom + 1) * Cols;
    if (delta > 0) {
        for (int i = a; i < b - Cols; i++) {
            paperRAM[i] = paperRAM[i + Cols];
            charRAM[i] = charRAM[i + Cols];
            attrRAM[i] = attrRAM[i + Cols];
            flags[i] = flags[i + Cols];
        }
        erase(b - Cols, b - 1);
    } else {
        for (int i = b - 1; i >= a + Cols; i--) {
            paperRAM[i] = paperRAM[i - Cols];
            charRAM[i] = charRAM[i - Cols];
            attrRAM[i] = attrRAM[i - Cols];
            flags[i] = flags[i - Cols];
        }
        erase(a, a + Cols - 1);
    }
    for (int i = a; i < b; i++)
        changed(i);
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
    int i = y * Cols + x;
    paperRAM[i] = ' ';
    charRAM[i] = (c < 128 ? c : '?');
    attrRAM[i] = (paper << 4) | selectedInk(ink, bold);
    flags[i] = rendition | ((useG1 ? g1 : g0) && c >= 0x5f && c <= 0x7e ? Graphics : 0);
    changed(i);
    if (x == Cols - 1)
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
            ink = 1;
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
            ink = 1;
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
void Terminal::dispatch(uint8_t f) {
    if (invalid)
        return;
    int n = param(0), lo = origin ? top : 0, hi = origin ? bottom : Rows - 1;
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
                changed(y * Cols + x);
                mirrorCursorDirty = true;
                break;
            }
        return;
    }
    if (prefix == '>') {
        if (f == 'v' && count == 1 && params[0] == 4)
            beginPlot();
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
            erase(y * Cols + x, Cells - 1);
        else if (params[0] == 1)
            erase(0, y * Cols + x);
        else if (params[0] == 2)
            erase(0, Cells - 1);
        wrapPending = false;
        break;
    case 'K':
        if (params[0] <= 0)
            erase(y * Cols + x, (y + 1) * Cols - 1);
        else if (params[0] == 1)
            erase(y * Cols, y * Cols + x);
        else if (params[0] == 2)
            erase(y * Cols, (y + 1) * Cols - 1);
        wrapPending = false;
        break;
    case 'm':
        sgr();
        break;
    case 'r': {
        int a = param(0) - 1, b = param(1, Rows) - 1;
        if (a >= 0 && b < Rows && a < b) {
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
            std::fill(tabs, tabs + Cols, false);
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
            while (next < Cols - 1 && !tabs[next])
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
    if (state == Charset) {
        designate(c);
        state = Ground;
        return;
    }
    if (state == Hash) {
        if (c == '8') {
            for (int i = 0; i < Cells; i++) {
                blank(i);
                charRAM[i] = 'E';
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
                erase(y * Cols + x, Cells - 1);
                break;
            case 'K':
                erase(y * Cols + x, (y + 1) * Cols - 1);
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
            state = CSI;
            count = 1;
            params[0] = -1;
            prefix = 0;
            invalid = false;
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
void Keyboard::finish() {
    buffer_[size_] = 0;
    const char *s = reinterpret_cast<const char *>(buffer_);
    if (!strcmp(s, "\033[1;3H") || !strcmp(s, "\033[1;3~") || !strcmp(s, "\033[7;3~") ||
        !strcmp(s, "\033\033[H"))
        t_.pan(0);
    else if (!strcmp(s, "\033[1;3F") || !strcmp(s, "\033[4;3~") || !strcmp(s, "\033[8;3~") ||
             !strcmp(s, "\033\033[F"))
        t_.pan(Cols - Visible);
    else if (!strcmp(s, "\033[1;3D") || !strcmp(s, "\033\033[D"))
        t_.pan(t_.viewport - 4);
    else if (!strcmp(s, "\033[1;3C") || !strcmp(s, "\033\033[C"))
        t_.pan(t_.viewport + 4);
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
    } else if (size_ == 6 && !memcmp(s, "\033[>2", 4) && s[4] >= '0' && s[4] <= '9' && s[5] == 'v') {
        if (act_) act_(context_, Action(HistoryBank0 + s[4] - '0'));
    } else if (size_ == 6 && !memcmp(s, "\033[>", 3) && s[3] >= '3' && s[3] <= '5' &&
               s[4] >= '0' && s[4] <= '9' && s[5] == 'v') {
        if (act_) act_(context_, Action(ProfileKey0 + (s[3] - '3') * 10 + s[4] - '0'));
    }
    else if (size_ == 3 && (buffer_[1] == '[' || buffer_[1] == 'O') && buffer_[2] >= 'A' &&
             buffer_[2] <= 'D') {
        char out[4] = {27, '[', char(buffer_[2]), 0};
        if (!t_.ansi) {
            out[1] = out[2];
            out[2] = 0;
        } else if (t_.applicationCursor)
            out[1] = 'O';
        output(out);
    } else if (size_ == 3 && buffer_[1] == 'O' && buffer_[2] >= 'p' && buffer_[2] <= 'y') {
        if (t_.applicationKeypad) {
            char out[5] = {27, t_.ansi ? 'O' : '?', char(buffer_[2]), 0};
            output(out);
        } else {
            uint8_t c = '0' + buffer_[2] - 'p';
            send_(context_, &c, 1);
        }
    } else if (size_ == 3 && buffer_[1] == 'O' &&
               (buffer_[2] == 'M' || buffer_[2] == 'n' || buffer_[2] == 'm' || buffer_[2] == 'l')) {
        if (t_.applicationKeypad) {
            char out[4] = {27, t_.ansi ? 'O' : '?', char(buffer_[2]), 0};
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
