#include "../src/terminal/byte_queue.h"
#include "../src/terminal/history_profiles.h"
#include "../src/terminal/line_history.h"
#include "../src/terminal/terminal.h"
#include <algorithm>
#include <assert.h>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>
static void collect(void *p, const uint8_t *s, size_t n) {
    static_cast<std::string *>(p)->append(reinterpret_cast<const char *>(s), n);
}
static void feed(vt::Terminal &t, const std::string &s) {
    for (uint8_t c : s)
        t.feed(c);
}
static void keys(vt::Keyboard &k, const std::string &s, uint32_t now = 0) {
    for (uint8_t c : s)
        k.feed(c, now++);
}
static int lastAction = -1;
static void action(void *, vt::Keyboard::Action value) { lastAction = value; }
struct KeyEventCapture {
    vt::KeyChord chord;
    std::string sent, fallback;
    unsigned events = 0;
};
static void collectKeyBytes(void *p, const uint8_t *s, size_t n) {
    static_cast<KeyEventCapture *>(p)->sent.append(reinterpret_cast<const char *>(s), n);
}
static void collectKeyEvent(void *p, vt::KeyChord chord, const uint8_t *s, size_t n) {
    auto &capture = *static_cast<KeyEventCapture *>(p);
    capture.chord = chord;
    capture.fallback.assign(reinterpret_cast<const char *>(s), n);
    ++capture.events;
}
static void testKeyboardOverrides() {
    vt::Terminal terminal;
    KeyEventCapture capture;
    vt::Keyboard keyboard(terminal, collectKeyBytes, &capture, nullptr, collectKeyEvent);
    const std::string num1 = "\033[>70;1;" + std::to_string(vt::KeyNum1) + ";0;043v";
    keys(keyboard, num1);
    assert(capture.events == 1 && capture.chord == (vt::KeyChord{vt::KeyNum1, 0}));
    assert(capture.fallback == "+" && capture.sent.empty());
    const std::string shiftedA = "\033[>70;1;" + std::to_string(vt::KeyA) + ";3;001v";
    keys(keyboard, shiftedA);
    assert(capture.events == 2 && capture.chord == (vt::KeyChord{vt::KeyA, 3}));
    assert(capture.fallback == std::string(1, '\x01'));
    const std::string invalid = "\033[>70;1;" + std::to_string(vt::KeyA) + ";0;097v";
    keys(keyboard, invalid);
    assert(capture.events == 2 && capture.sent == invalid);
    capture.sent.clear();
    keys(keyboard, "\033[>70;1;28;0;999v");
    assert(capture.sent == "\033[>70;1;28;0;999v");
    capture.sent.clear();
    keys(keyboard, "\033[Av");
    assert(capture.sent == "\033[Av");

    vt::KeyChord chord;
    assert(vt::parseKeyChordName("f5_ctrl_shift_alt", chord) &&
           chord == (vt::KeyChord{vt::KeyF5, 7}));
    assert(vt::parseKeyChordName("BS1_ALT", chord) &&
           chord == (vt::KeyChord{vt::KeyBs1, vt::KeyAlt}));
    assert(vt::parseKeyChordName("page_up_ctrl_shift", chord) &&
           chord == (vt::KeyChord{vt::KeyPageUp, vt::KeyCtrl | vt::KeyShift}));
    assert(vt::parseKeyChordName("kp_decimal_ctrl_alt", chord) &&
           chord == (vt::KeyChord{vt::KeyKpDecimal, vt::KeyCtrl | vt::KeyAlt}));
    const char *canonicalSuffixes[] = {
        "", "_ctrl", "_shift", "_ctrl_shift",
        "_alt", "_ctrl_alt", "_shift_alt", "_ctrl_shift_alt"
    };
    const char *allBaseNames[] = {
        nullptr,
        "left", "right", "home", "end", "delete", "insert", "up", "down",
        "page_up", "page_down", "backspace", "escape", "enter", "space",
        "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12",
        "num0", "num1", "num2", "num3", "num4", "num5", "num6", "num7", "num8", "num9",
        "bs2", "bs1", "p1", "p2", "l1", "l2", "l3", "z1", "m1", "m2", "m3",
        "kp_decimal", "kp_delete",
        "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
        "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"
    };
    static_assert(sizeof(allBaseNames) / sizeof(allBaseNames[0]) == vt::KeyZ + 1);
    for (uint8_t base = vt::KeyLeft; base <= vt::KeyZ; ++base)
        for (uint8_t modifiers = 0; modifiers < 8; ++modifiers) {
            const std::string name = std::string(allBaseNames[base]) + canonicalSuffixes[modifiers];
            const bool valid = vt::validKeyChord({base, modifiers});
            assert(vt::parseKeyChordName(name.c_str(), chord) == valid);
            if (valid) assert(chord == (vt::KeyChord{base, modifiers}));
        }
    assert(!vt::parseKeyChordName("a", chord));
    assert(!vt::parseKeyChordName("a_shift", chord));
    assert(!vt::parseKeyChordName("bs1", chord));
    assert(!vt::parseKeyChordName("l3_shift", chord));
    assert(!vt::parseKeyChordName("ctrl_left", chord));
    assert(!vt::parseKeyChordName("shift_ctrl_a", chord));
    assert(!vt::parseKeyChordName("a_shift_ctrl", chord));
    assert(!vt::parseKeyChordName("a_ctrl_ctrl", chord));
    assert(!vt::parseKeyChordName("left_alt_shift", chord));

    vt::HistoryProfiles profiles;
    const std::string ini =
        "[global]\nleft=send:G\nnum1=send:9\na_ctrl_shift=send:X\n"
        "kp_decimal=send:,\nleft_ctrl=send:C\n"
        "[bank2]\ninput=wordstar\nleft=send:B\nleft_ctrl=send:D\nnum1=none\n"
        "f5_ctrl_shift_alt=send:\\eQ\n"
        "[bank3]\ninput=wordstar\n";
    profiles.beginLoad();
    for (uint8_t c : ini) profiles.feed(c);
    profiles.finishLoad();
    assert(profiles.errors() == 0);
    auto binding = [&](uint8_t bank, vt::KeyChord event) {
        return profiles.bindingFor(bank, event);
    };
    assert(binding(2, {vt::KeyLeft, 0})->bytes[0] == 'B');
    assert(binding(3, {vt::KeyLeft, 0})->bytes[0] == 'G');
    assert(binding(2, {vt::KeyNum1, 0})->kind == vt::BindingNone);
    assert(binding(3, {vt::KeyNum1, 0})->bytes[0] == '9');
    assert(binding(2, {vt::KeyLeft, vt::KeyCtrl})->bytes[0] == 'D');
    assert(binding(3, {vt::KeyLeft, vt::KeyCtrl})->bytes[0] == 'C');
    assert(binding(2, {vt::KeyA, 3})->bytes[0] == 'X');
    assert(binding(2, {vt::KeyKpDecimal, 0})->bytes[0] == ',');
    assert(binding(2, {vt::KeyF5, 7})->bytes[0] == 27);
    assert(binding(2, {vt::KeyF5, 7})->bytes[1] == 'Q');
    assert(binding(2, {vt::KeyNum2, 0}) == nullptr);

    const std::string duplicate = "[global]\nnum1=send:7\nnum1=send:8\n"
                                  "[bank1]\nup=send:A\nup=send:B\n"
                                  "left_ctrl=send:C\nleft_ctrl=send:D\n";
    profiles.beginLoad();
    for (uint8_t c : duplicate) profiles.feed(c);
    profiles.finishLoad();
    assert(profiles.errors() == 3);
    assert(binding(0, {vt::KeyNum1, 0})->bytes[0] == '7');
    assert(binding(1, {vt::KeyUp, 0})->bytes[0] == 'A');

    std::string overfull = "[global]\n";
    const char *mods[] = {"_ctrl", "_alt", "_ctrl_alt", "_ctrl_shift", "_ctrl_shift_alt"};
    for (const char *mod : mods)
        for (char letter = 'a'; letter <= 'z'; ++letter)
            overfull += std::string(1, letter) + mod + "=send:Z\n";
    profiles.beginLoad();
    for (uint8_t c : overfull) profiles.feed(c);
    profiles.finishLoad();
    assert(profiles.errors() == 2); // 130 unique chords, 128 slots.
    assert(binding(0, {vt::KeyZ, uint8_t(vt::KeyCtrl | vt::KeyShift | vt::KeyAlt)}) == nullptr);
}
static void testCaretControlNotation() {
    vt::HistoryProfiles profiles;
    const char *ini = R"ini([bank2]
left=send:\^K^D
right=send:\^K\^D
f5=send:\^@\^A\^Z\^[\^\\^]\^^\^_\^?
f6=send:\^a
exit_command=\^K\^D\r
)ini";
    profiles.beginLoad();
    for (const char *p = ini; *p; ++p) profiles.feed(uint8_t(*p));
    profiles.finishLoad();
    assert(profiles.errors() == 0);
    const vt::KeyBinding &left = profiles.get(2).keys[vt::AppLeft];
    assert(left.length == 3 && left.bytes[0] == 11 && left.bytes[1] == '^' &&
           left.bytes[2] == 'D');
    const vt::KeyBinding &right = profiles.get(2).keys[vt::AppRight];
    assert(right.length == 2 && right.bytes[0] == 11 && right.bytes[1] == 4);
    const vt::KeyBinding *f5 = profiles.bindingFor(2, {vt::KeyF5, 0});
    const uint8_t expected[] = {0, 1, 26, 27, 28, 29, 30, 31, 127};
    assert(f5 && f5->length == sizeof(expected));
    assert(!memcmp(f5->bytes, expected, sizeof(expected)));
    const vt::KeyBinding *f6 = profiles.bindingFor(2, {vt::KeyF6, 0});
    assert(f6 && f6->length == 1 && f6->bytes[0] == 1);
    assert(profiles.get(2).exitLength == 3 && profiles.get(2).exitBytes[0] == 11 &&
           profiles.get(2).exitBytes[1] == 4 && profiles.get(2).exitBytes[2] == 13);

    const char *invalid = "[bank2]\nf5=send:\\^5\nf6=send:\\^\n";
    profiles.beginLoad();
    for (const char *p = invalid; *p; ++p) profiles.feed(uint8_t(*p));
    profiles.finishLoad();
    assert(profiles.errors() == 2);
    assert(profiles.bindingFor(2, {vt::KeyF5, 0}) == nullptr);
    assert(profiles.bindingFor(2, {vt::KeyF6, 0}) == nullptr);
}
static void testProfileParseDiagnostics() {
    vt::HistoryProfiles profiles;
    const char *ini = "[bank2]\r\n"
                      "not_a_setting\r\n"
                      "f5=send:\\^5\r\n"
                      "[bank99]\r\n";
    profiles.beginLoad();
    for (const char *p = ini; *p; ++p) profiles.feed(uint8_t(*p));
    profiles.finishLoad();
    assert(profiles.errors() == 3);
    assert(profiles.errorLineCount() == 3);
    const auto &first = profiles.errorLine(0);
    assert(first.number == 2 && !strcmp(first.section, "[bank2]") &&
           !strcmp(first.text, "not_a_setting"));
    const auto &second = profiles.errorLine(1);
    assert(second.number == 3 && !strcmp(second.section, "[bank2]") &&
           !strcmp(second.text, "f5=send:\\^5"));
    const auto &third = profiles.errorLine(2);
    assert(third.number == 4 && !strcmp(third.section, "[invalid]") &&
           !strcmp(third.text, "[bank99]"));

    std::string many = "[bank0]\n";
    for (unsigned i = 0; i < 12; ++i)
        many += "bad" + std::to_string(i) + "\n";
    profiles.beginLoad();
    for (uint8_t c : many) profiles.feed(c);
    profiles.finishLoad();
    assert(profiles.errors() == 12 && profiles.errorLineCount() == 10);
    assert(profiles.errorLine(9).number == 11 && !strcmp(profiles.errorLine(9).text, "bad9"));
}
struct PlotRecord {
    vt::Terminal::PlotEvent event;
    uint16_t color;
};
static void collectPlot(void *p, vt::Terminal::PlotEvent event, uint16_t color) {
    static_cast<std::vector<PlotRecord> *>(p)->push_back({event, color});
}
static int lastBacklight = -1;
static int lastZoom = -1;
static int lastGuestEvent = -1, lastGuestCols = 0, lastGuestRows = 0;
static void control(void *, vt::Terminal::ControlEvent event, uint8_t value,
                    uint16_t cols, uint16_t rows) {
    if (event == vt::Terminal::Backlight) lastBacklight = value;
    if (event == vt::Terminal::Zoom) lastZoom = value;
    if (event >= vt::Terminal::QueryInfo) {
        lastGuestEvent = event;
        lastZoom = value;
        lastGuestCols = cols;
        lastGuestRows = rows;
    }
}
static void testLineEditing() {
    vt::Terminal t;
    std::vector<PlotRecord> plots;
    t.setPlotSink(collectPlot, &plots);
    assert(t.setGeometry(8, 6, 4, 6, 2));
    t.pan(3);
    feed(t, "\033[2;5r"); // the first and last screen rows stay outside the margins
    auto clean = [&]() {
        std::fill(t.dirty, t.dirty + vt::MaxCells, false);
        std::fill(t.mirrorDirty, t.mirrorDirty + vt::MaxRows, false);
        plots.clear();
    };
    auto seed = [&]() {
        for (int row = 0; row < t.rows; ++row)
            for (int col = 0; col < t.cols; ++col) {
                const int i = t.index(col, row);
                t.paperRAM[i] = uint8_t('a' + row);
                t.charRAM[i] = uint8_t('A' + row);
                t.attrRAM[i] = uint8_t(0x10 + row);
                t.flags[i] = row & 1 ? vt::Underline : vt::Reverse;
            }
        clean();
    };
    auto expectRow = [&](int row, uint8_t ch) {
        for (int col = 0; col < t.cols; ++col)
            assert(t.charRAM[t.index(col, row)] == ch);
    };

    feed(t, "\033[3;4H");
    seed();
    feed(t, "\033[L"); // omitted count inserts one line at the cursor
    assert(t.x == 0 && t.y == 2 && t.viewport == 3 && t.zoom == 2);
    expectRow(0, 'A'); expectRow(1, 'B'); expectRow(2, ' ');
    expectRow(3, 'C'); expectRow(4, 'D'); expectRow(5, 'F');
    for (int col = 0; col < t.cols; ++col) {
        const int blank = t.index(col, 2), moved = t.index(col, 3);
        assert(t.paperRAM[blank] == ' ' && t.attrRAM[blank] == 0 && t.flags[blank] == 0);
        assert(t.paperRAM[moved] == 'c' && t.attrRAM[moved] == 0x12 &&
               t.flags[moved] == vt::Reverse);
    }
    for (int row = 0; row < t.rows; ++row) {
        assert(t.mirrorDirty[row] == (row >= 2 && row <= 4));
        for (int col = 0; col < t.cols; ++col)
            assert(t.dirty[t.index(col, row)] == (row >= 2 && row <= 4));
    }
    assert(plots.size() == 1 && plots[0].event == vt::Terminal::PlotScroll);

    feed(t, "\033[1;4H"); // outside the scroll region: both commands are inert
    clean();
    feed(t, "\033[1L\033[1M");
    assert(t.x == 3 && t.y == 0 && plots.empty());
    for (int row = 0; row < t.rows; ++row) {
        assert(!t.mirrorDirty[row]);
        for (int col = 0; col < t.cols; ++col)
            assert(!t.dirty[t.index(col, row)]);
    }

    feed(t, "\033[3;7H");
    clean();
    t.feed(0x9b); // 8-bit CSI uses the same delete-line handler
    feed(t, "M");
    assert(t.x == 0 && t.y == 2);
    expectRow(2, 'C'); expectRow(3, 'D'); expectRow(4, ' ');
    assert(plots.size() == 1 && plots[0].event == vt::Terminal::PlotScroll);

    feed(t, "\033[3;8H");
    seed();
    feed(t, "\033[2M");
    expectRow(1, 'B'); expectRow(2, 'E'); expectRow(3, ' ');
    expectRow(4, ' '); expectRow(5, 'F');
    feed(t, "\033[2L");
    expectRow(2, ' '); expectRow(3, ' '); expectRow(4, 'E');

    feed(t, "\033[4;8H");
    seed();
    feed(t, "\033[0L"); // zero also means one
    expectRow(3, ' '); expectRow(4, 'D');
    feed(t, "\033[4;8H\033[99M"); // clamp to rows before the bottom margin
    expectRow(2, 'C'); expectRow(3, ' '); expectRow(4, ' ');
    expectRow(5, 'F');
}
int main() {
    testKeyboardOverrides();
    testCaretControlNotation();
    testProfileParseDiagnostics();
    testLineEditing();
    std::string replies;
    vt::Terminal t(collect, &replies);
    t.setControlSink(control, nullptr);
    assert(t.x == 0 && t.y == 0 && t.ansi && t.attrRAM[0] == 0);
    feed(t, "HELLO\r\nWORLD");
    assert(t.charRAM[80] == 'W' && t.x == 5 && t.y == 1);
    lastBacklight = -1;
    feed(t, "\033[>63;7v");
    assert(lastBacklight == 7 && t.charRAM[85] == ' ');
    feed(t, "\033[>63;10v");
    assert(lastBacklight == 10);
    for (int level = 0; level <= 31; ++level) {
        feed(t, "\033[>63;" + std::to_string(level) + "v");
        assert(lastBacklight == level);
    }
    feed(t, "\033[>63;32v");
    assert(lastBacklight == 32);
    feed(t, "\033[>63;33v");
    assert(lastBacklight == 33);
    feed(t, "\033[>63;12v");
    assert(lastBacklight == 12);
    lastZoom = -1;
    feed(t, "\033[>68;3v");
    assert(lastZoom == 3);
    feed(t, "\033[>68;0v");
    assert(lastZoom == 3);
    feed(t, "\033[>72v");
    assert(lastGuestEvent == vt::Terminal::QueryInfo);
    feed(t, "\033[>73;2v");
    assert(lastGuestEvent == vt::Terminal::LoadPreset && lastZoom == 2);
    feed(t, "\033[>73;2;80;24v");
    assert(lastGuestEvent == vt::Terminal::SetGuestGeometry &&
           lastGuestCols == 80 && lastGuestRows == 24);
    feed(t, "\033[>73;2;79;24v\033[>73;2;80;999v");
    assert(lastGuestCols == 80 && lastGuestRows == 24);
    feed(t, "\033[24;80HX");
    assert(t.x == 79 && t.y == 23 && t.charRAM[1919] == 'X');
    feed(t, "Y");
    assert(t.charRAM[23 * 80] == 'Y' && t.charRAM[22 * 80 + 79] == 'X' && t.x == 1);
    t.reset();
    feed(t, std::string(80, 'A'));
    assert(t.y == 0 && t.x == 79);
    feed(t, "\rB");
    assert(t.y == 0 && t.charRAM[0] == 'B');
    feed(t, "\033[?7l\033[1;80HXY");
    assert(t.charRAM[79] == 'Y' && t.y == 0);
    t.reset();
    feed(t, "abcdef\033[1;3H\033[K");
    assert(t.charRAM[1] == 'b' && t.charRAM[2] == ' ');
    feed(t, "\033[2J");
    assert(t.x == 2 && t.y == 0 && t.charRAM[0] == ' ');
    feed(t, "\033[3;6r\033[?6h");
    assert(t.y == 2);
    feed(t, "\033[99;1H");
    assert(t.y == 5);
    feed(t, "\033[6n");
    assert(replies == "\033[4;1R");
    replies.clear();
    feed(t, "\033[?6l\033[1;1HTOP\033[24;1HBOTTOM\033[6;1H\n");
    assert(t.charRAM[0] == 'T' && t.charRAM[23 * 80] == 'B');
    feed(t, "\033[3;1H\033M");
    assert(t.y == 2 && t.charRAM[0] == 'T');
    t.reset();
    feed(t, "\t");
    assert(t.x == 8);
    feed(t, "\033[g\r\t");
    assert(t.x == 16);
    feed(t, "\033[3g\r\t");
    assert(t.x == 79);
    t.reset();
    feed(t, "\033[31;4;7mX");
    assert(t.flags[0] == (vt::Underline | vt::Reverse));
    assert((t.attrRAM[0] & 15) == 1);
    feed(t, "\033[1mB");
    assert((t.attrRAM[1] & 15) == 9);
    feed(t, "\033[0m\033[90mD");
    assert((t.attrRAM[2] & 15) == 8);
    feed(t, "\033[0mY");
    assert(t.flags[3] == 0 && t.attrRAM[3] == 0);
    feed(t, "\033[38;2;1;4;7mZ");
    assert(t.flags[4] == 0 && t.attrRAM[4] == 0);
    feed(t, "\033[30mA\033[1mB\033[39mC");
    assert((t.attrRAM[5] & 15) == 0);
    assert((t.attrRAM[6] & 15) == 8);
    assert((t.attrRAM[7] & 15) == 8); // SGR 39 keeps the active bold attribute.
    feed(t, "\033[22mD");
    assert((t.attrRAM[8] & 15) == 0);
    // Exact xterm colors are consumed only after explicit private GCLS.  The
    // callback is display-neutral and confirms fragmented SGR/sample parsing.
    std::vector<PlotRecord> plots;
    t.setPlotSink(collectPlot, &plots);
    t.reset();
    feed(t, "\033[>4v");
    assert(t.plotting() && plots.size() == 4 &&
           plots[0].event == vt::Terminal::PlotConfigZoom && plots[0].color == 1 &&
           plots[1].event == vt::Terminal::PlotConfigX && plots[1].color == 0 &&
           plots[2].event == vt::Terminal::PlotConfigY && plots[2].color == 0 &&
           plots[3].event == vt::Terminal::PlotBegin);
    feed(t, "\033[38;5;196m#\r\n\033[38;5;21m[\r\n");
    assert(plots.size() == 8);
    assert(plots[4].event == vt::Terminal::PlotSample && plots[4].color == 0xf800);
    assert(plots[5].event == vt::Terminal::PlotRow);
    assert(plots[6].event == vt::Terminal::PlotSample && plots[6].color == 0x001f);
    assert(plots[7].event == vt::Terminal::PlotRow);
    assert(t.charRAM[0] == ' ' && t.y == 1); // samples never enter text RAM; one logical LF
    feed(t, "\033[0mDONE");
    assert(!t.plotting() && plots.back().event == vt::Terminal::PlotEnd && t.charRAM[80] == 'D');
    // Text and its CR/LF remain ordinary terminal traffic after GCLS but
    // before the first xterm sample. This is how a submitted COM command is
    // echoed on a clean line before it starts plotting.
    plots.clear();
    t.reset();
    feed(t, "\033[>4vBROT-BS\r\n");
    assert(plots.size() == 4 && t.charRAM[0] == 'B' && t.y == 1);
    feed(t, "\033[38;5;196m#\r\n");
    assert(plots.size() == 6 && plots[4].event == vt::Terminal::PlotSample &&
           plots[5].event == vt::Terminal::PlotRow && t.y == 2);
    // GCLS scale and origin are physical coordinates; omitted parts retain
    // the documented 1,0,0 defaults. Invalid scale does not arm a plot.
    plots.clear();
    t.reset();
    plots.clear(); // reset aborts the preceding intentionally active plot
    feed(t, "\033[>4;2;100;40v");
    assert(plots.size() == 4 && plots[0].color == 2 && plots[1].color == 100 &&
           plots[2].color == 40 && plots[3].event == vt::Terminal::PlotBegin);
    t.reset();
    plots.clear();
    feed(t, "\033[>4;5;100;40v");
    assert(!t.plotting() && plots.empty());
    // Invalid/incomplete extended colors must leave the following byte as text.
    plots.clear();
    t.reset();
    plots.clear(); // reset aborts the intentionally unfinished preceding plot
    feed(t, "\033[>4v\033[38;5mQ");
    assert(plots.size() == 4 && t.charRAM[0] == 'Q');
    t.reset();
    feed(t, "\033(0lqqk\033(Bx");
    assert(t.graphics(3) && !t.graphics(7));
    feed(t, "\033)0\016q\017x");
    assert(t.graphics(5) && !t.graphics(6));
    t.reset();
    feed(t, "\033(0ABC_q");
    assert(!t.graphics(0) && t.graphics(3) && t.graphics(4));
    t.reset();
    feed(t, "\033[5;8H\033[4m\0337\033[1;1H\033[0m\0338X");
    assert(t.charRAM[4 * 80 + 7] == 'X' && t.flags[4 * 80 + 7] == vt::Underline);
    t.reset();
    feed(t, "\033[31m\033[?2l\033Y");
    t.feed(32 + 10);
    t.feed(32 + 70);
    feed(t, "Z");
    assert(!t.ansi && t.charRAM[10 * 80 + 70] == 'Z' &&
           (t.attrRAM[10 * 80 + 70] & 15) == 1);
    feed(t, "\033Z");
    assert(replies == "\033/Z");
    replies.clear();
    feed(t, "\033H\033Fq\033Gx");
    assert(t.graphics(0) && !t.graphics(1));
    feed(t, "\033<\033[6n");
    assert(t.ansi && replies == "\033[1;3R");
    feed(t, "A");
    assert((t.attrRAM[2] & 15) == 1); // mode changes preserve ink.
    // Both CSI encodings reach the same ANSI scanner; C1 is not an output
    // mode switch and fragmented bytes are handled just like ESC [.
    t.reset();
    t.feed(0x9b);
    feed(t, "2;3H");
    t.feed(0x9b);
    feed(t, "31mR");
    assert(t.ansi && t.charRAM[t.index(2, 1)] == 'R' &&
           (t.attrRAM[t.index(2, 1)] & 15) == 1);
    t.feed(0x9b);
    feed(t, "0mN"); // FAND's compact three-byte attribute reset.
    assert(t.charRAM[t.index(3, 1)] == 'N' &&
           (t.attrRAM[t.index(3, 1)] & 15) == 0);
    t.feed(0x9b);
    feed(t, "1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17mS");
    assert(t.charRAM[t.index(4, 1)] == 'S' && t.ansi);
    // RunCPM's Console7 output mask turns C1 CSI into a bare ESC; compact
    // attributes cannot work until Console8 lets the original byte through.
    t.reset();
    for (uint8_t c : std::string("\x9b" "31mX", 5)) t.feed(c & 0x7f);
    assert(t.charRAM[2] == 'X' && (t.attrRAM[2] & 15) == 0);
    t.reset();
    feed(t, "\033[?2l\033Y");
    t.feed(0x9b); // VT52 addressing column 123, not a C1 CSI prefix.
    t.feed(uint8_t(' ' + 2));
    assert(!t.ansi && t.x == 2 && t.y == t.rows - 1);
    lastZoom = -1;
    feed(t, "\033[>68;3v");
    assert(lastZoom == 3 && !t.ansi);
    const int vtX = t.x, vtY = t.y;
    feed(t, "\033[1;1H"); // ordinary ANSI cursor movement is inert in VT52.
    assert(t.x == vtX && t.y == vtY && !t.ansi);
    t.feed(0x9b);
    feed(t, "1;1H");
    assert(t.charRAM[t.index(vtX, vtY)] == '?' && !t.ansi);
    plots.clear();
    feed(t, "\033[>4v");
    assert(t.plotting() && !t.ansi);
    feed(t, "\033[31m"); // not a GCLS SGR subset: ignore it in VT52.
    assert(t.plotting());
    const int beforePlot = t.index(t.x, t.y);
    feed(t, "Q");
    assert((t.attrRAM[beforePlot] & 15) == 0);
    feed(t, "\033[38;5;196m#\r\n\033[0m");
    assert(!t.plotting() && !t.ansi && plots.back().event == vt::Terminal::PlotEnd);
    const int afterPlot = t.index(t.x, t.y);
    feed(t, "T");
    assert((t.attrRAM[afterPlot] & 15) == 0);
    feed(t, "\033<");
    assert(t.ansi);
    replies.clear();
    feed(t, "\033[5n\033[c");
    assert(replies == "\033[0n\033[?1;0c");
    t.reset();
    feed(t, "\033]0;evil title\007OK\033Pignored\033\\!");
    assert(t.charRAM[0] == 'O' && t.charRAM[2] == '!');
    feed(t, "\033[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17mQ");
    assert(t.flags[3] == 0);
    feed(t, "\033[99999999999999999999999999;999999999H!");
    assert(t.charRAM[1919] == '!');
    t.reset();
    t.pan(28);
    feed(t, "\033[2;70H\033[6n");
    assert(t.viewport == 28 && t.x == 69);
    t.pan(-10);
    assert(t.viewport == 0 && t.x == 69);
    t.pan(1000);
    assert(t.viewport == 28);
    // Active geometry changes retain the overlap, clear excluded backing, and
    // retain the maximum row stride rather than repacking the planes.
    t.reset();
    feed(t, "KEEP");
    assert(t.setGeometry(40, 12, 26, 12, 2));
    assert(t.cols == 40 && t.rows == 12 && t.visibleCols == 26 && t.zoom == 2);
    assert(t.charRAM[t.index(0, 0)] == 'K');
    t.charRAM[t.index(39, 11)] = 'X';
    assert(t.setGeometry(20, 6, 16, 6, 3));
    assert(t.charRAM[t.index(0, 0)] == 'K');
    assert(t.setGeometry(80, 24, 52, 24, 1));
    assert(t.charRAM[t.index(39, 11)] == ' ');
    std::string input;
    vt::Keyboard k(t, collect, &input);
    keys(k, "\033[1;3H");
    assert(t.viewport == 0 && input.empty());
    keys(k, "\033[1;3F");
    assert(t.viewport == 28);
    // Wider logical planes use evenly distributed manual views rather than a
    // near-duplicate sliver at the far edge. Vertical views use the matching
    // private controls emitted by Ctrl+Shift+Alt+Home/End.
    assert(t.setGeometry(80, 24, 26, 10, 1));
    assert(t.viewport == 0 && t.rowViewport == 0);
    keys(k, "\033[1;3F"); assert(t.viewport == 18);
    keys(k, "\033[1;3F"); assert(t.viewport == 36);
    keys(k, "\033[1;3F"); assert(t.viewport == 54);
    keys(k, "\033[1;3F"); assert(t.viewport == 54);
    keys(k, "\033[1;3H"); assert(t.viewport == 36);
    keys(k, "\033[>71;1v"); assert(t.rowViewport == 7);
    keys(k, "\033[>71;1v"); assert(t.rowViewport == 14);
    keys(k, "\033[>71;1v"); assert(t.rowViewport == 14);
    keys(k, "\033[>71;0v"); assert(t.rowViewport == 7);
    keys(k, "\033[>71;0v"); assert(t.rowViewport == 0);
    t.charRAM[t.index(70, 20)] = 'Z';
    feed(t, "\033[20;70H\0337");
    assert(t.setView(12, 6, 2));
    assert(t.cols == 80 && t.rows == 24 && t.zoom == 2 &&
           t.charRAM[t.index(70, 20)] == 'Z' && t.x == 69 && t.y == 19);
    t.pan(68);
    t.panRows(18);
    t.panOverview(-1);
    assert(t.rowViewport == 18 && t.viewport < 68);
    t.pan(68);
    t.panRows(12);
    t.panOverview(1);
    assert(t.rowViewport == 18 && t.viewport == 0);
    assert(t.setView(26, 10, 1));
    feed(t, "\0338");
    assert(t.x == 69 && t.y == 19 && t.charRAM[t.index(70, 20)] == 'Z');
    assert(t.setGeometry(80, 24, 52, 24, 1));
    keys(k, "\033[>2v\033[A");
    assert(input == "\033[A" && !t.ansi); // fallback is independent of output mode
    input.clear();
    keys(k, "\033[>1v");
    feed(t, "\033[?1h");
    keys(k, "\033[B");
    assert(input == "\033OB");
    input.clear();
    plots.clear();
    keys(k, "\033[>4v");
    assert(t.plotting() && plots.size() == 4 &&
           plots.back().event == vt::Terminal::PlotBegin);
    keys(k, "\033[>0v");
    assert(!t.plotting() && plots.back().event == vt::Terminal::PlotAbort);
    keys(k, "\033[99~");
    assert(input == "\033[99~");
    input.clear();
    k.feed(27, 100);
    k.tick(149);
    assert(input.empty());
    k.tick(150);
    assert(input == "\033");
    input.clear();
    keys(k, std::string("\177\n\003", 3), 200);
    assert(input == std::string("\b\r\003", 3));
    input.clear();
    keys(k, "\033\033[H", 300);
    assert(t.viewport == 0 && input.empty());
    vt::Keyboard historyKeys(t, collect, &input, action);
    keys(historyKeys, "\033[>10v");
    assert(lastAction == vt::Keyboard::HistoryPrevious && input.empty());
    keys(historyKeys, "\033[>13v");
    assert(lastAction == vt::Keyboard::HistoryBankNext && input.empty());
    keys(historyKeys, "\033[>14v");
    assert(lastAction == vt::Keyboard::HistoryBankShow && input.empty());
    keys(historyKeys, "\033[>15v");
    assert(lastAction == vt::Keyboard::RawToggle && input.empty());
    keys(historyKeys, "\033[>16v");
    assert(lastAction == vt::Keyboard::RawOn && input.empty());
    keys(historyKeys, "\033[>17v");
    assert(lastAction == vt::Keyboard::RawOff && input.empty());
    keys(historyKeys, "\033[>60v");
    assert(lastAction == vt::Keyboard::ProfileExit && input.empty());
    keys(historyKeys, "\033[>61v");
    assert(lastAction == vt::Keyboard::ProfileClear && input.empty());
    keys(historyKeys, "\033[>22v");
    assert(lastAction == vt::Keyboard::HistoryBank0 + 2 && input.empty());
    keys(historyKeys, "\033[>30v");
    assert(lastAction == vt::Keyboard::ProfileKey0 + vt::AppLeft && input.empty());
    keys(historyKeys, "\033[>35v");
    assert(lastAction == vt::Keyboard::ProfileKey0 + vt::AppInsert && input.empty());
    keys(historyKeys, "\033[>39v");
    assert(lastAction == vt::Keyboard::ProfileKey0 + vt::AppAltDown && input.empty());
    keys(historyKeys, "\033[>59v");
    assert(lastAction == vt::Keyboard::ProfileKey0 + vt::AppAltPageDown && input.empty());
    keys(historyKeys, "\033[>63;9v");
    assert(lastAction == vt::Keyboard::Backlight0 + 9 && input.empty());
    keys(historyKeys, "\033[>63;11v");
    assert(lastAction == vt::Keyboard::Backlight0 + 11 && input.empty());
    keys(historyKeys, "\033[>63;34v");
    assert(lastAction == vt::Keyboard::Backlight34 && input.empty());
    keys(historyKeys, "\033[>66v");
    assert(lastAction == vt::Keyboard::ZoomIn && input.empty());
    keys(historyKeys, "\033[>67v");
    assert(lastAction == vt::Keyboard::ZoomOut && input.empty());

    vt::HistoryProfiles profiles;
    assert(profiles.global().crtRampupMs == 0);
    for (const std::string duration : {"0", "1", "1500", "60000", "60001", "-1", "abc", "1ms", ""}) {
        profiles.beginLoad();
        const std::string ini = "[global]\nbacklight_level=32\ncrt_rampup_ms=" + duration + "\n";
        for (uint8_t c : ini) profiles.feed(c);
        profiles.finishLoad();
        const bool valid = duration == "0" || duration == "1" || duration == "1500" || duration == "60000";
        assert(profiles.errors() == (valid ? 0u : 1u));
        assert(profiles.global().backlightLevel == 31);
        assert(profiles.global().crtRampupMs == (valid ? unsigned(std::stoi(duration)) : 0u));
    }
    const std::string profileIni =
        "[global]\r\nexit_key=lalt+ralt+delete\r\nclear_key=lalt+ralt+escape\r\ncursor_blink_ms=250\r\n"
        "viewport_gutter_rgb=128,129,130\r\nviewport_active_gutter_rgb=active_line\r\n"
        "[bank2]\r\nname=EDITOR\r\nup=send:\\x05\r\n"
        "up_alt=history_previous\r\non_select=A4:\\r\n"
        "page_up=send_cancel:\\e\r\nactive_line_rgb=210,255,210\r\n"
        "cursor_rgb=0,255,64\r\nscreen_line=true\r\ntrim_left=true\r\nline_prefix=>\r\n"
        "activate_numbered_line=true\r\n"
        "input_adapter=mbasic_native\r\n"
        "command_new_line=true\r\n"
        "history_retype_delay_ms=75\r\n"
        "exit_command=BYE\\r\r\nclear_command=PAGE\\r\r\n\x1a ignored padding";
    profiles.beginLoad();
    for (uint8_t c : profileIni) profiles.feed(c);
    profiles.finishLoad();
    assert(profiles.errors() == 0);
    assert(profiles.global().exitKey == vt::GlobalKeyDualAltDelete);
    assert(profiles.global().clearKey == vt::GlobalKeyDualAltEscape);
    assert(profiles.global().cursorBlinkMs == 250);
    assert(profiles.global().viewportGutterRGB[0] == 128);
    assert(profiles.global().viewportGutterRGB[1] == 129);
    assert(profiles.global().viewportGutterRGB[2] == 130);
    assert(profiles.global().viewportActiveGutterFollowsLine);
    assert(std::string(profiles.get(2).name) == "EDITOR");
    assert(profiles.get(2).keys[vt::AppUp].kind == vt::BindingSend);
    assert(profiles.get(2).keys[vt::AppUp].length == 1);
    assert(profiles.get(2).keys[vt::AppUp].bytes[0] == 5);
    assert(profiles.get(2).keys[vt::AppAltUp].kind == vt::BindingHistoryPrevious);
    assert(profiles.get(2).selectLength == 4 && profiles.get(2).selectBytes[3] == 13);
    assert(profiles.get(2).keys[vt::AppPageUp].kind == vt::BindingSendCancel);
    assert(profiles.get(2).activeLineRGB[0] == 210 && profiles.get(2).cursorRGB[1] == 255);
    assert(profiles.get(2).historyRetypeDelayMs == 75);
    assert(profiles.get(2).trimLeft);
    assert(profiles.get(2).activateNumberedLine);
    assert(profiles.get(2).screenLine && profiles.get(2).linePrefixBytes[0] == '>');
    assert(profiles.get(2).inputAdapter == vt::InputAdapterMbasicNative);
    assert(profiles.get(2).commandNewLine);
    assert(profiles.get(2).exitLength == 4 && profiles.get(2).clearLength == 5);
    assert(profiles.get(5).keys[vt::AppLeft].bytes[0] == 0x0a);
    assert(profiles.get(5).keys[vt::AppRight].bytes[0] == 0x0c);
    assert(profiles.get(5).historyClearLength == 2);
    assert(profiles.get(5).historyClearBytes[0] == 0x06);
    assert(profiles.get(5).historyClearBytes[1] == 0x18);
    assert(profiles.get(5).activatePrefixLength == 1 &&
           profiles.get(5).activatePrefixBytes[0] == '>');
    assert(profiles.get(5).activateNumberedLine);
    assert(profiles.get(5).editingSurface == vt::EditingSurfaceLine);
    assert(profiles.get(2).editingSurface == vt::EditingSurfaceScreen);
    const std::string surfaceIni = "[bank5]\nediting_surface=screen\n";
    profiles.beginLoad();
    for (uint8_t c : surfaceIni) profiles.feed(c);
    profiles.finishLoad();
    assert(profiles.errors() == 0 &&
           profiles.get(5).editingSurface == vt::EditingSurfaceScreen);
    // The generic line composer is a separately selectable adapter.
    const std::string composeIni = "[bank1]\r\ninput_adapter=local_compose\r\n\x1a";
    profiles.beginLoad();
    for (uint8_t c : composeIni) profiles.feed(c);
    profiles.finishLoad();
    assert(profiles.errors() == 0 && profiles.get(1).inputAdapter == vt::InputAdapterLocalCompose);

    // A portable shared card selects only its matching display section;
    // identifiers are case-insensitive and odd active widths normalize down.
    vt::HistoryProfiles displayProfiles;
    displayProfiles.beginLoad();
    const std::string displayIni =
        "[display:rp2_2]\nview_zoom=2\nzoom2_terminal=41x12\nrender_flush_hz=15\n"
        "[display:ES3_5]\nview_zoom=4\nzoom4_terminal=20x12\n";
    for (uint8_t c : displayIni) displayProfiles.feed(c);
    displayProfiles.finishLoad();
    assert(displayProfiles.errors() == 0 && displayProfiles.display().viewZoom == 2);
    assert(displayProfiles.display().geometry[1].cols == 40 &&
           displayProfiles.display().geometry[1].rows == 12 &&
           displayProfiles.display().renderFlushHz == 15);

    // Broken headings cannot leak settings into the preceding valid section.
    displayProfiles.beginLoad();
    for (uint8_t c : std::string(
             "[display:RP2_2]\nview_zoom=2\n[display:ES3_5\nview_zoom=4\n"
             "[global]\nbacklight_level=16\n[typo]\nbacklight_level=31\n"
             "[bank1\nname=must not apply\n"
             "[display:RP2_2]\nrender_flush_hz=12\n")) displayProfiles.feed(c);
    displayProfiles.finishLoad();
    assert(displayProfiles.errors() == 6);
    assert(displayProfiles.display().viewZoom == 2);
    assert(displayProfiles.global().backlightLevel == 16);
    assert(displayProfiles.display().renderFlushHz == 12);
    displayProfiles.beginLoad();
    const std::string longHeading = "[display:RP2_2]\nview_zoom=2\n[" +
        std::string(250, 'X') + "]\nview_zoom=4\n";
    for (uint8_t c : longHeading) displayProfiles.feed(c);
    displayProfiles.finishLoad();
    assert(displayProfiles.errors() == 2 && displayProfiles.display().viewZoom == 2);

    // A group is intentionally shareable; absent fields retain each HAL's defaults.
    auto sharedHal = vt::displayHalDefaults();
    sharedHal.id = "GROUP_5A";
    for (uint16_t flush : {uint16_t(15), uint16_t(30)}) {
        sharedHal.defaultRenderFlushHz = flush;
        displayProfiles.beginLoad(sharedHal);
        for (uint8_t c : std::string(
                 "[display:group_5a]\nview_zoom=2\nzoom2_terminal=40x12\n"
                 "[display:GROUP_5B]\nview_zoom=4\nunknown_setting=ignored\n"))
            displayProfiles.feed(c);
        displayProfiles.finishLoad();
        assert(displayProfiles.errors() == 0 && displayProfiles.display().viewZoom == 2);
        assert(displayProfiles.display().renderFlushHz == flush);
    }

    // Mode, console width and input keys are independent. A late input=
    // line must not overwrite an explicit key entered earlier in the INI.
    vt::HistoryProfiles legacyProfiles;
    const std::string legacyIni =
        "[global]\nmode=vt52\nconsole_bits=8\n"
        "[bank2]\nup=send:\\e[A\ninput=wordstar\nmode=ansi\n"
        "zoom1_terminal=79x20\nzoom3_terminal=80x24\n"
        "[bank3]\ninput=vt52\n";
    legacyProfiles.beginLoad();
    for (uint8_t c : legacyIni) legacyProfiles.feed(c);
    legacyProfiles.finishLoad();
    assert(legacyProfiles.errors() == 0); // 24 rows use a vertical 3x viewport.
    assert(legacyProfiles.global().consoleBits == 8 &&
           legacyProfiles.modeFor(0) == vt::OutputVT52 &&
           legacyProfiles.modeFor(2) == vt::OutputAnsi &&
           legacyProfiles.modeFor(3) == vt::OutputVT52);
    assert(legacyProfiles.geometryFor(2, 1).cols == 78 &&
           legacyProfiles.geometryFor(2, 1).rows == 20 &&
           legacyProfiles.geometryFor(2, 3).cols == 80 &&
           legacyProfiles.geometryFor(2, 3).rows == 24);
    assert(legacyProfiles.get(2).keys[vt::AppUp].length == 3 &&
           legacyProfiles.get(2).keys[vt::AppUp].bytes[1] == '[');
    assert(legacyProfiles.get(2).keys[vt::AppLeft].bytes[0] == 0x13 &&
           legacyProfiles.get(2).keys[vt::AppPageUp].bytes[0] == 0x12);
    assert(legacyProfiles.get(3).keys[vt::AppLeft].length == 2 &&
           legacyProfiles.get(3).keys[vt::AppLeft].bytes[0] == 27 &&
           legacyProfiles.get(3).keys[vt::AppLeft].bytes[1] == 'D');
    assert(legacyProfiles.get(3).keys[vt::AppAltUp].kind == vt::BindingHistoryPrevious);
    assert(legacyProfiles.get(2).editingSurface == vt::EditingSurfaceScreen);

    vt::LineHistory history;
    vt::LineHistory::Recall recalled;
    assert(history.activeBank() == 0 && !history.recall(true, recalled));
    for (uint8_t c : std::string(125, 'X')) history.input(c);
    history.input(13);
    assert(history.recall(true, recalled) &&
           recalled.length == vt::LineHistory::MaxCcpRecall);
    assert(history.deleteRecalled() && history.bankCount(0) == 0);
    history.select(1);
    for (uint8_t c : std::string("dir\rstat\r")) history.input(c);
    assert(history.recall(true, recalled));
    assert(recalled.erase == 0 && std::string(recalled.text, recalled.length) == "stat");
    history.replaceCurrent("stat", 4, 4); // screen resync preserves recall position
    assert(history.recall(true, recalled));
    assert(recalled.erase == 4 && std::string(recalled.text, recalled.length) == "dir");
    assert(history.recall(false, recalled));
    assert(recalled.erase == 3 && std::string(recalled.text, recalled.length) == "stat");
    assert(history.recall(false, recalled));
    assert(recalled.erase == 4 && recalled.length == 0);
    history.select(2);
    history.input('Q');
    history.input('\r');
    for (uint8_t c : std::string("print 42\r")) history.input(c);
    assert(history.recall(true, recalled));
    assert(std::string(recalled.text, recalled.length) == "print 42");
    assert(!history.recall(true, recalled));
    history.select(1);
    assert(history.recall(true, recalled));
    assert(std::string(recalled.text, recalled.length) == "stat");
    vt::LineHistory cleanup;
    for (uint8_t c : std::string("old\rgarbage1\rmiddle\rgarbage2\rnew\r")) cleanup.input(c);
    assert(cleanup.recall(true, recalled) &&
           std::string(recalled.text, recalled.length) == "new");
    assert(cleanup.recall(true, recalled) &&
           std::string(recalled.text, recalled.length) == "garbage2");
    assert(cleanup.deleteRecalled());
    assert(cleanup.recall(true, recalled) && recalled.erase == 0 &&
           std::string(recalled.text, recalled.length) == "middle");
    assert(cleanup.recall(true, recalled) &&
           std::string(recalled.text, recalled.length) == "garbage1");
    assert(cleanup.deleteRecalled());
    assert(cleanup.recall(true, recalled) && recalled.erase == 0 &&
           std::string(recalled.text, recalled.length) == "old");
    vt::LineHistory filtered;
    for (uint8_t c : std::string("dir\rdump\rdir a\rstat\rdir b\r")) filtered.input(c);
    filtered.input('d'); filtered.input('i');
    assert(filtered.recall(true, recalled) && recalled.erase == 2 &&
           std::string(recalled.text, recalled.length) == "dir b ");
    assert(filtered.deleteRecalled());
    assert(filtered.recall(true, recalled) && recalled.erase == 0 &&
           std::string(recalled.text, recalled.length) == "dir a ");
    assert(filtered.recall(true, recalled) &&
           std::string(recalled.text, recalled.length) == "dir ");
    // Filtered traversal wraps rather than stopping at the oldest match.
    assert(filtered.recall(true, recalled) &&
           std::string(recalled.text, recalled.length) == "dir a ");
    assert(filtered.recall(false, recalled) &&
           std::string(recalled.text, recalled.length) == "dir ");
    assert(filtered.recall(false, recalled) &&
           std::string(recalled.text, recalled.length) == "dir a ");
    const uint8_t filteredCount = filtered.bankCount(0);
    filtered.input('\r'); // unchanged filtered recall is deliberately not re-added
    assert(filtered.bankCount(0) == filteredCount);
    // The token immediately before the cursor, not the whole input line,
    // becomes the filter prefix.
    vt::LineHistory tokenFilter;
    for (uint8_t c : std::string("PRINT\rPRINTER\rLIST\r")) tokenFilter.input(c);
    for (uint8_t c : std::string("10 pr")) tokenFilter.input(c);
    assert(tokenFilter.recall(true, recalled) &&
           std::string(recalled.text, recalled.length) == "10 PRINTER ");
    const uint8_t keywordCount = tokenFilter.bankCount(0);
    for (uint8_t c : std::string("HELLO\r")) tokenFilter.input(c);
    assert(tokenFilter.bankCount(0) == keywordCount); // nested source line stays out
    // Inline completion preserves the edited suffix and does not duplicate
    // the whitespace already following the cursor.
    vt::LineHistory inlineFilter;
    for (uint8_t c : std::string("FOR\r")) inlineFilter.input(c);
    for (uint8_t c : std::string("10 f i = 1 TO 100")) inlineFilter.input(c);
    while (inlineFilter.currentCursor() > 4) inlineFilter.left();
    assert(inlineFilter.recall(true, recalled) &&
           std::string(recalled.text, recalled.length) == "10 FOR i = 1 TO 100");
    vt::LineHistory missingKeyword;
    for (uint8_t c : std::string("10 PRINT 42\rLIST\r")) missingKeyword.input(c);
    for (uint8_t c : std::string("10 f i = 1 TO 100")) missingKeyword.input(c);
    while (missingKeyword.currentCursor() > 4) missingKeyword.left();
    assert(!missingKeyword.recall(true, recalled));
    char intact[vt::LineHistory::MaxLine]; uint8_t intactLength, intactCursor;
    missingKeyword.copyCurrent(intact, intactLength, intactCursor);
    assert(std::string(intact, intactLength) == "10 f i = 1 TO 100" && intactCursor == 4);
    vt::LineHistory replFilter;
    for (uint8_t c : std::string("LOAD\rLIST\r")) replFilter.input(c);
    replFilter.input('l');
    assert(replFilter.recall(true, recalled) && std::string(recalled.text, recalled.length) == "LIST ");
    for (uint8_t c : std::string("DISK\r")) replFilter.input(c);
    assert(replFilter.bankCount(0) == 3); // edited column-zero recall is an immediate command
    vt::LineHistory midCursor;
    for (uint8_t c : std::string("stat\rdir\r")) midCursor.input(c);
    midCursor.input('d'); midCursor.input('i'); midCursor.left();
    assert(midCursor.recall(true, recalled) &&
           std::string(recalled.text, recalled.length) == "dir i");
    assert(midCursor.recall(true, recalled) &&
           std::string(recalled.text, recalled.length) == "dir i");
    vt::LineHistory edited;
    for (uint8_t c : std::string("PRINT 12345")) edited.input(c);
    edited.left(); edited.left();
    edited.eraseAt();
    edited.input('X');
    edited.input('\r');
    assert(edited.recall(true, recalled));
    assert(std::string(recalled.text, recalled.length) == "PRINT 123X5");
    edited.replaceCurrent("10 PRINT 42", 11, 8);
    edited.input('X');
    edited.input('\r');
    assert(edited.recall(true, recalled));
    assert(std::string(recalled.text, recalled.length) == "10 PRINTX 42");
    vt::LineHistory wrapped;
    for (int i = 0; i <= vt::LineHistory::Depth; ++i) {
        std::string line = "L" + std::to_string(i);
        for (uint8_t c : line) wrapped.input(c);
        wrapped.input('\r');
    }
    assert(wrapped.bankCount(0) == vt::LineHistory::Depth);
    char oldest[vt::LineHistory::MaxLine]; uint8_t oldestLength = 0;
    assert(wrapped.copyEntry(0, 0, oldest, oldestLength) &&
           std::string(oldest, oldestLength) == "L1");
    assert(wrapped.recall(true, recalled));
    assert(std::string(recalled.text, recalled.length) == "L255");
    for (int i = vt::LineHistory::Depth - 1; i >= 1; --i) {
        assert(wrapped.recall(true, recalled));
        assert(std::string(recalled.text, recalled.length) == "L" + std::to_string(i));
    }
    assert(!wrapped.recall(true, recalled));
    // A full filtered bank must wrap without a uint8_t offset becoming zero.
    wrapped.select(0);
    wrapped.input('L');
    assert(wrapped.recall(true, recalled) &&
           std::string(recalled.text, recalled.length) == "L255 ");
    for (int i = vt::LineHistory::Depth - 1; i >= 1; --i)
        assert(wrapped.recall(true, recalled));
    assert(wrapped.recall(true, recalled) &&
           std::string(recalled.text, recalled.length) == "L255 ");
    // Random hostile streams must never escape fixed buffers or screen bounds.
    std::mt19937 rng(42);
    for (int i = 0; i < 200000; i++) {
        t.feed(uint8_t(rng()));
        assert(t.x >= 0 && t.x < 80 && t.y >= 0 && t.y < 24);
    }
    ByteQueue<16> small;
    for (int i = 0; i < 16; i++) {
        assert(small.push(i));
    }
    assert(!small.push(17));
    for (int i = 0; i < 16; i++) {
        uint8_t c;
        assert(small.pop(c) && c == i);
    }
    uint8_t c;
    assert(!small.pop(c));
    // Exercise the exact transport implementation concurrently under backpressure.
    ByteQueue<4096> in;
    ByteQueue<16384> out;
    constexpr int N = 1000000;
    std::thread producer([&] {
        for (int i = 0; i < N; i++) {
            while (!out.push(uint8_t(i)))
                std::this_thread::yield();
        }
        for (int i = 0; i < 4096; i++) {
            uint8_t b;
            while (!in.pop(b))
                std::this_thread::yield();
            assert(b == uint8_t(i));
        }
    });
    // Fill the input direction while the simulated CPU refuses to read it.
    for (int i = 0; i < 4096; i++)
        assert(in.push(uint8_t(i)));
    assert(!in.push(0));
    for (int i = 0; i < N; i++) {
        uint8_t b;
        while (!out.pop(b))
            std::this_thread::yield();
        assert(b == uint8_t(i));
    }
    producer.join();
    std::cout << "terminal, keyboard, bounds/fuzz, and bidirectional queue tests passed\n";
}
