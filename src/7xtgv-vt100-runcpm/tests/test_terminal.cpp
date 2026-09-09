#include "../byte_queue.h"
#include "../history_profiles.h"
#include "../line_history.h"
#include "../terminal.h"
#include <assert.h>
#include <iostream>
#include <random>
#include <string>
#include <thread>
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
int main() {
    std::string replies;
    vt::Terminal t(collect, &replies);
    assert(t.x == 0 && t.y == 0 && t.ansi && t.attrRAM[0] == 1);
    feed(t, "HELLO\r\nWORLD");
    assert(t.charRAM[80] == 'W' && t.x == 5 && t.y == 1);
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
    keys(k, "\033[1;3C");
    assert(t.viewport == 4 && input.empty());
    keys(k, "\033[1;3F");
    assert(t.viewport == 28);
    keys(k, "\033[1;3D");
    assert(t.viewport == 24);
    keys(k, "\033[>2v\033[A");
    assert(input == "\033A" && !t.ansi);
    input.clear();
    keys(k, "\033[>1v");
    feed(t, "\033[?1h");
    keys(k, "\033[B");
    assert(input == "\033OB");
    input.clear();
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

    vt::HistoryProfiles profiles;
    const std::string profileIni =
        "[bank2]\r\nname=EDITOR\r\nup=send:\\x05\r\n"
        "alt_up=history_previous\r\non_select=A4:\\r\n\x1a ignored padding";
    profiles.beginLoad();
    for (uint8_t c : profileIni) profiles.feed(c);
    profiles.finishLoad();
    assert(profiles.errors() == 0);
    assert(std::string(profiles.get(2).name) == "EDITOR");
    assert(profiles.get(2).keys[vt::AppUp].kind == vt::BindingSend);
    assert(profiles.get(2).keys[vt::AppUp].length == 1);
    assert(profiles.get(2).keys[vt::AppUp].bytes[0] == 5);
    assert(profiles.get(2).keys[vt::AppAltUp].kind == vt::BindingHistoryPrevious);
    assert(profiles.get(2).selectLength == 4 && profiles.get(2).selectBytes[3] == 13);

    vt::LineHistory history;
    vt::LineHistory::Recall recalled;
    assert(history.activeBank() == 0 && !history.recall(true, recalled));
    history.select(1);
    for (uint8_t c : std::string("dir\rstat\r")) history.input(c);
    assert(history.recall(true, recalled));
    assert(recalled.erase == 0 && std::string(recalled.text, recalled.length) == "stat");
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
