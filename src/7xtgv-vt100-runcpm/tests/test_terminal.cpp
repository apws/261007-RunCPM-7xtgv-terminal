#include "../byte_queue.h"
#include "../history_profiles.h"
#include "../line_history.h"
#include "../terminal.h"
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
struct PlotRecord {
    vt::Terminal::PlotEvent event;
    uint16_t color;
};
static void collectPlot(void *p, vt::Terminal::PlotEvent event, uint16_t color) {
    static_cast<std::vector<PlotRecord> *>(p)->push_back({event, color});
}
static int lastBacklight = -1;
static void control(void *, vt::Terminal::ControlEvent event, uint8_t value) {
    if (event == vt::Terminal::Backlight) lastBacklight = value;
}
int main() {
    std::string replies;
    vt::Terminal t(collect, &replies);
    t.setControlSink(control, nullptr);
    assert(t.x == 0 && t.y == 0 && t.ansi && t.attrRAM[0] == 1);
    feed(t, "HELLO\r\nWORLD");
    assert(t.charRAM[80] == 'W' && t.x == 5 && t.y == 1);
    lastBacklight = -1;
    feed(t, "\033[>63;7v");
    assert(lastBacklight == 7 && t.charRAM[85] == ' ');
    feed(t, "\033[>63;10v");
    assert(lastBacklight == 10);
    feed(t, "\033[>63;12v");
    assert(lastBacklight == 10);
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
    assert((t.attrRAM[0] & 15) == 2);
    feed(t, "\033[1mB");
    assert((t.attrRAM[1] & 15) == 3);
    feed(t, "\033[0m\033[90mD");
    assert((t.attrRAM[2] & 15) == 1);
    feed(t, "\033[0mY");
    assert(t.flags[2] == 0 && t.attrRAM[2] == 1);
    feed(t, "\033[38;2;1;4;7mZ");
    assert(t.flags[2] == 0 && t.attrRAM[2] == 1);
    // Exact xterm colors are consumed only after explicit private GCLS.  The
    // callback is display-neutral and confirms fragmented SGR/sample parsing.
    std::vector<PlotRecord> plots;
    t.setPlotSink(collectPlot, &plots);
    t.reset();
    feed(t, "\033[>4v");
    assert(t.plotting() && plots.size() == 1 && plots.back().event == vt::Terminal::PlotBegin);
    feed(t, "\033[38;5;196m#\r\n\033[38;5;21m[\r\n");
    assert(plots.size() == 5);
    assert(plots[1].event == vt::Terminal::PlotSample && plots[1].color == 0xf800);
    assert(plots[2].event == vt::Terminal::PlotRow);
    assert(plots[3].event == vt::Terminal::PlotSample && plots[3].color == 0x001f);
    assert(plots[4].event == vt::Terminal::PlotRow);
    assert(t.charRAM[0] == ' ' && t.y == 1); // samples never enter text RAM; one logical LF
    feed(t, "\033[0mDONE");
    assert(!t.plotting() && plots.back().event == vt::Terminal::PlotEnd && t.charRAM[80] == 'D');
    // Text and its CR/LF remain ordinary terminal traffic after GCLS but
    // before the first xterm sample. This is how a submitted COM command is
    // echoed on a clean line before it starts plotting.
    plots.clear();
    t.reset();
    feed(t, "\033[>4vBROT-BS\r\n");
    assert(plots.size() == 1 && t.charRAM[0] == 'B' && t.y == 1);
    feed(t, "\033[38;5;196m#\r\n");
    assert(plots.size() == 3 && plots[1].event == vt::Terminal::PlotSample &&
           plots[2].event == vt::Terminal::PlotRow && t.y == 2);
    // Invalid/incomplete extended colors must leave the following byte as text.
    plots.clear();
    t.reset();
    plots.clear(); // reset aborts the intentionally unfinished preceding plot
    feed(t, "\033[>4v\033[38;5mQ");
    assert(plots.size() == 1 && t.charRAM[0] == 'Q');
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
    feed(t, "\033[?2l\033Y");
    t.feed(32 + 10);
    t.feed(32 + 70);
    feed(t, "Z");
    assert(!t.ansi && t.charRAM[10 * 80 + 70] == 'Z');
    feed(t, "\033Z");
    assert(replies == "\033/Z");
    replies.clear();
    feed(t, "\033H\033Fq\033Gx");
    assert(t.graphics(0) && !t.graphics(1));
    feed(t, "\033<\033[6n");
    assert(t.ansi && replies == "\033[1;3R");
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
    std::string input;
    vt::Keyboard k(t, collect, &input);
    keys(k, "\033[1;3H");
    assert(t.viewport == 0 && input.empty());
    keys(k, "\033[1;3F");
    assert(t.viewport == 28);
    keys(k, "\033[>2v\033[A");
    assert(input == "\033A" && !t.ansi);
    input.clear();
    keys(k, "\033[>1v");
    feed(t, "\033[?1h");
    keys(k, "\033[B");
    assert(input == "\033OB");
    input.clear();
    plots.clear();
    keys(k, "\033[>4v");
    assert(t.plotting() && plots.size() == 1 && plots[0].event == vt::Terminal::PlotBegin);
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
    assert(lastAction == vt::Keyboard::Backlight11 && input.empty());

    vt::HistoryProfiles profiles;
    const std::string profileIni =
        "[global]\r\nexit_key=lalt+ralt+delete\r\nclear_key=lalt+ralt+escape\r\ncursor_blink_ms=250\r\n"
        "viewport_gutter_rgb=128,129,130\r\n"
        "[bank2]\r\nname=EDITOR\r\nup=send:\\x05\r\n"
        "alt_up=history_previous\r\non_select=A4:\\r\n"
        "page_up=send_cancel:\\e\r\nactive_line_rgb=210,255,210\r\n"
        "cursor_rgb=0,255,64\r\nscreen_line=true\r\ntrim_left=true\r\nline_prefix=>\r\n"
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
    // The generic line composer is a separately selectable adapter.
    const std::string composeIni = "[bank1]\r\ninput_adapter=local_compose\r\n\x1a";
    profiles.beginLoad();
    for (uint8_t c : composeIni) profiles.feed(c);
    profiles.finishLoad();
    assert(profiles.errors() == 0 && profiles.get(1).inputAdapter == vt::InputAdapterLocalCompose);

    vt::LineHistory history;
    vt::LineHistory::Recall recalled;
    assert(history.activeBank() == 0 && !history.recall(true, recalled));
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
    for (int i = 0; i <= 100; ++i) {
        std::string line = "L" + std::to_string(i);
        for (uint8_t c : line) wrapped.input(c);
        wrapped.input('\r');
    }
    assert(wrapped.recall(true, recalled));
    assert(std::string(recalled.text, recalled.length) == "L100");
    for (int i = 99; i >= 1; --i) {
        assert(wrapped.recall(true, recalled));
        assert(std::string(recalled.text, recalled.length) == "L" + std::to_string(i));
    }
    assert(!wrapped.recall(true, recalled));
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
