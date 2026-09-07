// Compile the production runtime against fake USB/GFX peripherals. Parser,
// transport, renderer and mirror code are the same as the RP2350 build.
#include "../terminal_runtime.cpp"
#include <fstream>
#include <iostream>
static void emit(const std::string &s) {
    for (uint8_t c : s) {
        while (!output.push(c))
            terminalLoop();
    }
}
static bool mirrorIdle() {
    for (bool b : terminal.mirrorDirty)
        if (b)
            return false;
    return mirrorSent == mirrorLength && !mirrorReset && !terminal.mirrorCursorDirty;
}
static void settle(int limit = 100000) {
    while (limit-- && (!mirrorIdle() || output.available())) {
        fakeTime++;
        terminalLoop();
    }
    assert(limit > 0);
}
static void screenshot(const char *path) {
    for (int i = 0; i < 200; i++)
        terminalLoop();
    std::ofstream f(path, std::ios::binary);
    f << "P6\n320 240\n255\n";
    for (uint16_t p : gfx->framebuffer) {
        char rgb[3] = {char(((p >> 11) & 31) * 255 / 31), char(((p >> 5) & 63) * 255 / 63),
                       char((p & 31) * 255 / 31)};
        f.write(rgb, 3);
    }
}
int main() {
    terminalSetup();
    assert(ready && backlight == 150 && displayOK);
    emit("CP/M without USB\r\nA0>");
    for (int i = 0; i < 150; i++)
        terminalLoop();
    assert(terminal.charRAM[80] == 'A' && Serial.tx.empty());
    Serial.online = true;
    settle();
    assert(!Serial.tx.empty());
    // A partially transmitted row finishes while later output coalesces.
    Serial.capacity = 64;
    Serial.chunk = 1;
    Serial.tx.clear();
    terminal.invalidate();
    for (int i = 0; i < 30; i++)
        terminalLoop();
    emit("\033[2J\033[HHELLO\033[24;80HZ");
    settle();
    vt::Terminal pc;
    for (uint8_t c : Serial.tx)
        pc.feed(c);
    assert(pc.charRAM[0] == 'H' && pc.charRAM[1919] == 'Z');
    // A stalled PC mirror must not stop output consumption or LCD redraw.
    Serial.capacity = 0;
    for (int i = 0; i < 4096; i++)
        assert(input.push(uint8_t(i)));
    emit("\033[HSTALLED PC STILL DRAWS");
    for (int i = 0; i < 200; i++)
        terminalLoop();
    assert(output.available() == 0 && terminal.charRAM[0] == 'S' && backlight == 150);
    // Deliberately hostile report flood: replies bounded, output still drains.
    for (int i = 0; i < 400; i++)
        emit("\033[6n");
    for (int i = 0; i < 100; i++)
        terminalLoop();
    assert(replyOverruns > 0 && output.available() == 0);
    uint8_t c;
    while (input.pop(c)) {
    }
    while (replies.pop(c)) {
    }
    Serial.capacity = 64;
    Serial.chunk = 64;
    settle();
    // The mirror must never ask the PC to answer a terminal query.
    assert(Serial.tx.find("\033[6n") == std::string::npos);
    assert(Serial.tx.find("\033[c") == std::string::npos);
    Serial.online = false;
    terminalLoop();
    emit("\033[HRECONNECTED");
    for (int i = 0; i < 100; i++)
        terminalLoop();
    Serial.tx.clear();
    Serial.online = true;
    settle();
    vt::Terminal reconnected;
    for (uint8_t b : Serial.tx)
        reconnected.feed(b);
    assert(reconnected.charRAM[0] == 'R' && reconnected.charRAM[1] == 'E');
    // Pan command reaches terminal, not CP/M. Input echo returns via the output link.
    for (uint8_t b : std::string("\033[1;3F"))
        Serial.rx.push_back(b);
    terminalLoop();
    assert(terminal.viewport == 28 && input.available() == 0);
    Serial.rx.push_back('D');
    terminalLoop();
    assert(virtualRead() == 'D');
    // Worst-case per-cell color transitions must fit the fixed mirror row buffer.
    terminal.reset();
    for (int i = 0; i < vt::Cells; i++) {
        terminal.charRAM[i] = 'X';
        terminal.attrRAM[i] = i % 64;
        terminal.flags[i] = i % 4;
    }
    terminal.invalidate();
    settle();
    assert(mirrorLength < sizeof(mirrorBuffer));
    terminal.reset();
    emit("\033[H7xtgv VT100 + RunCPM\r\n80x24 screen / 52x24 viewport\r\n\r\n");
    for (int color = 30; color <= 37; color++)
        emit("\033[" + std::to_string(color) + "mColor ");
    emit("\033[0m\r\n\033[4mUnderline\033[0m  \033[7mReverse\033[0m\r\n\r\n");
    emit("\033(0l" + std::string(78, 'q') + "k\033(B");
    emit("\033[9;1H\033(0x\033[9;80Hx\033(B\033[10;1H\033(0m" + std::string(78, 'q') + "j\033(B");
    emit("\033[12;1HLEFT EDGE\033[12;71HRIGHT EDGE\033[15;1HA0>");
    settle();
    terminal.pan(0);
    screenshot("/tmp/7xtgv-terminal-left.ppm");
    terminal.pan(28);
    screenshot("/tmp/7xtgv-terminal-right.ppm");
    std::cout << "runtime render/mirror, slow/disconnected USB, queue saturation, and reconnect "
                 "tests passed\n";
}
