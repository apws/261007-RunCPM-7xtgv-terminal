// Compile the production runtime against fake USB/GFX peripherals. Parser,
// transport, renderer and mirror code are the same as the RP2350 build.
// The real implementation is sketch-level because only the RunCPM core owns
// storage; this runtime-only test supplies the owner poll as a no-op.
bool terminalHistoryStoragePoll() { return true; }
bool terminalConfigStoragePoll() { return true; }
#include "../src/terminal/terminal_runtime.cpp"
#include <fstream>
#include <iostream>
#include <vector>
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
static void testBoot(const std::string &mode) {
    gfxBeginOK = mode != "display-failure";
    terminalSetup();
    assert(ready && backlight == 0 && xtgvBacklightGetRemembered() == 16);
    // Arbitrarily slow SD mount must not expose the compiled white palette.
    for (int n = 0; n < 300; ++n) { fakeTime += 10; terminalLoop(); }
    assert(backlight == 0);
    const bool configured = mode == "dark" || mode == "ini-errors" || mode == "ramp";
    if (configured) {
        terminalConfigBegin();
        std::string ini = "[global]\nmode_palette=1\nbacklight_level=7\n";
        if (mode == "ramp") ini += "crt_rampup_ms=70\n";
        if (mode == "ini-errors") ini += "not_a_setting=bad\n";
        for (uint8_t c : ini) terminalConfigFeed(c);
        assert(terminalConfigEnd() == (mode == "ini-errors" ? 1u : 0u));
        assert(backlight == 0 && xtgvBacklightGetRemembered() == 7);
    }
    // Same notification for missing INI, SD failure, or task creation failure.
    terminalBootConfigComplete();
    emit(configured ? "BOOT\r\n" : "Boot fallback/error\r\n");
    unsigned flushBefore = gfx->flushCount;
    bool checkedFrame = false;
    for (int n = 0; n < 500; ++n) {
        fakeTime++;
        terminalLoop();
        if (bootFrameReady && !checkedFrame) {
            checkedFrame = true;
            assert(backlight == 0);
            assert(!terminal.compositionDirty);
            for (int y = 0; y < terminal.visibleRows; ++y)
                for (int x = 0; x < terminal.visibleCols; ++x)
                    assert(!terminal.dirty[terminal.index(x + terminal.viewport,
                                                          y + terminal.rowViewport)]);
            assert(activePalette == (configured ? 1 : 0));
            assert(gfx->framebuffer[319 + 239 * 320] == rgb565(terminal.palette.paper[0]));
#if defined(GFX_BUFFERED_CANVAS) || defined(GFX_DEFERRED_FLUSH)
            assert(gfx->flushCount > flushBefore);
#else
            (void)flushBefore;
#endif
        }
        if (backlight != 0) {
            assert(checkedFrame && uint32_t(millis() - bootFrameFlushedAt) >= 33);
            break;
        }
    }
    if (mode == "ramp") {
        assert(xtgvBacklightRamp().active && backlight == xtgvBacklightDuty(1));
        assert(xtgvBacklightGet() == 7 && xtgvBacklightGetRemembered() == 7);
        fakeTime += 70;
        terminalLoop();
        assert(!xtgvBacklightRamp().active);
    }
    assert(backlight == (gfxBeginOK ? xtgvBacklightDuty(configured ? 7 : 16) : 0));
    assert(!configSavePending.load()); // startup must not rewrite the INI
    if (!gfxBeginOK) {
        Serial.online = true;
        settle();
        assert(!Serial.tx.empty()); // failed TFT leaves the PC mirror usable
    } else {
        xtgvBacklightSet(0);
        terminalBootConfigComplete(); // completion must never turn OFF back ON
        for (int n = 0; n < 100; ++n) { fakeTime++; terminalLoop(); }
        assert(backlight == 0);
        xtgvBacklightAdjust(1);
        assert(backlight == xtgvBacklightDuty(configured ? 7 : 16));
    }
    std::cout << "Boot test passed: " << mode << '\n';
}
static void testIdentity() {
    terminalSetup();
    terminalBootConfigComplete();
    Serial.online = true;
    settle();
    for (int i = 0; i < 300; ++i) terminalLoop();
    const std::vector<uint16_t> pixels(std::begin(gfx->framebuffer), std::end(gfx->framebuffer));
    const std::string expected = std::string("\033P7xtgv-id;1;") + xtgvFirmwareTarget() +
                                 ";" + xtgvFirmwareBuild() + "\033\\";
    for (bool raw : {false, true}) {
        rawInputMode = raw;
        Serial.tx.clear();
        Serial.capacity = Serial.chunk = 1;
        // An existing partial mirror write must finish before identity begins.
        mirrorLength = 3; mirrorSent = 1;
        memcpy(mirrorBuffer, "XYZ", 3);
        for (uint8_t c : std::string("\033[>62v")) Serial.rx.push_back(c);
        for (int n = 0; n < 10000 && Serial.tx.find(expected) == std::string::npos; ++n)
            terminalLoop();
        assert(Serial.tx.rfind("YZ" + expected, 0) == 0);
        assert(input.available() == 0 && rawInputMode == raw);
        assert(std::equal(pixels.begin(), pixels.end(), std::begin(gfx->framebuffer)));
        Serial.capacity = Serial.chunk = 64;
        settle();
    }
    std::cout << "Firmware identity normal/RAW/partial-write tests passed\n";
}
int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "identity") { testIdentity(); return 0; }
    if (argc == 2) { testBoot(argv[1]); return 0; }
    terminalSetup();
    assert(ready && backlight == 0 && displayOK);
    terminalBootConfigComplete();
    for (int n = 0; n < 300; ++n) { fakeTime++; terminalLoop(); }
    assert(ready && backlight == xtgvBacklightDuty(16) && displayOK);
    // Keep the original regression suite's blink-phase time origin.
    fakeTime = blinkAt = 0;
    cursorPhase = true;
    const int plotBaseX = 0, plotBaseY = 0; // physical panel origin, not text origin
    xtgvBacklightSet(0); assert(backlight == 0);
    xtgvBacklightAdjust(1);
    assert(xtgvBacklightGet() == 16 && backlight == xtgvBacklightDuty(16));
    xtgvBacklightSet(0);
    xtgvBacklightSet(1); assert(backlight == xtgvBacklightDuty(1));
    xtgvBacklightSet(32); assert(backlight == 255);
    // 32 states include OFF and 31 strictly increasing lit levels. Only the
    // final level is full-on; one step down skips the near-DC duty 254.
    assert(xtgvBacklightDuty(0) == 0);
    for (uint8_t level = 1; level <= XTGV_BACKLIGHT_MAX_LEVEL; ++level) {
        assert(xtgvBacklightDuty(level) > xtgvBacklightDuty(level - 1));
        xtgvBacklightSet(level);
        assert(backlight == xtgvBacklightDuty(level));
        if (level < XTGV_BACKLIGHT_MAX_LEVEL) assert(backlight <= 253);
    }
    xtgvBacklightAdjust(-1);
    assert(xtgvBacklightGet() == 30 && backlight == xtgvBacklightDuty(30));
    xtgvBacklightAdjust(1);
    assert(xtgvBacklightGet() == 31 && backlight == 255);
    for (int i = 0; i < 40; ++i) xtgvBacklightAdjust(1);
    assert(xtgvBacklightGet() == 31 && backlight == 255);
    terminalBacklight(32); // legacy protocol full ON
    assert(xtgvBacklightGet() == 31 && backlight == 255);
    for (int i = 0; i < 40; ++i) xtgvBacklightAdjust(-1);
    assert(xtgvBacklightGet() == 1 && backlight == xtgvBacklightDuty(1));
    xtgvBacklightSet(0);
    assert(backlight == 0 && xtgvBacklightGetRemembered() == 1);
    xtgvBacklightAdjust(-1);
    assert(xtgvBacklightGet() == 1 && backlight == xtgvBacklightDuty(1));
    assert(xtgvBacklightDuty(255) == 255);
    xtgvBacklightSet(16); assert(backlight == xtgvBacklightDuty(16));
    // CP/M's A0:REFLASH.COM output event is removed from the TFT terminal
    // stream and forwarded verbatim, between completed mirror updates only.
    Serial.online = true;
    settle();
    Serial.tx.clear();
    const uint8_t savedCell = terminal.charRAM[0];
    emit("\033[>64v");
    for (int i = 0; i < 20; ++i) terminalLoop();
    assert(Serial.tx.find("\033[>64v") != std::string::npos);
    assert(terminal.charRAM[0] == savedCell);
    Serial.online = false;
    // Fast owner-core disk operations are latched into a visible, fixed-pixel
    // activity pulse; expiry restores the terminal cell underneath it.
    terminalDiskActivity(true);
    terminalLoop();
    assert(gfx->framebuffer[1 * 320 + 1] == 0xf800);
    assert(gfx->framebuffer[6 * 320 + 6] == 0xf800);
    assert(gfx->framebuffer[7 * 320 + 7] != 0xf800);
    fakeTime += 76;
    for (int i = 0; i < 200; ++i) terminalLoop();
    assert(gfx->framebuffer[1 * 320 + 1] != 0xf800);
    // GCLS only arms the parser: the existing gutter stays untouched. A disk
    // pulse may still appear before plotting, but the first sample retires it.
    const uint16_t gutterUnderlying = gfx->framebuffer[1 * 320 + 1];
    terminalDiskActivity(true);
    terminalLoop();
    const uint16_t gutterBeforeGcls = gfx->framebuffer[1 * 320 + 1];
    const std::vector<uint16_t> screenBeforeArm(std::begin(gfx->framebuffer),
                                                 std::end(gfx->framebuffer));
    for (uint8_t c : std::string("\033[>4;2;20;20v")) terminal.feed(c);
    assert(std::equal(screenBeforeArm.begin(), screenBeforeArm.end(),
                      std::begin(gfx->framebuffer)));
    terminalLoop();
    assert(gfx->framebuffer[1 * 320 + 1] == gutterBeforeGcls);
    terminalDiskActivity(true);
    terminalLoop();
    assert(gfx->framebuffer[1 * 320 + 1] == 0xf800);
    for (uint8_t c : std::string("\033[38;5;196m1\r\n\033[0m")) terminal.feed(c);
    fakeTime += 76;
    terminalLoop();
    assert(gfx->framebuffer[1 * 320 + 1] == gutterUnderlying);
    terminal.reset();
    std::string historyText;
    auto appendHistoryByte = [](void *context, uint8_t byte) {
        static_cast<std::string *>(context)->push_back(char(byte));
        return true;
    };
    assert(terminalHistoryWrite(appendHistoryByte, &historyText));
    assert(historyText.rfind("# 7xtgv terminal history v2\n", 0) == 0);
    assert(historyText.size() >= 17 && historyText[historyText.size() - 17] == '#');
    uint32_t historyCRC = 0xffffffffUL;
    for (size_t i = 0; i < historyText.size() - 17; ++i)
        historyCRC = historyCRC32Byte(historyCRC, uint8_t(historyText[i]));
    char expectedFooter[24];
    snprintf(expectedFooter, sizeof(expectedFooter), "# crc32=%08lX\n",
             static_cast<unsigned long>(historyCRC ^ 0xffffffffUL));
    assert(historyText.compare(historyText.size() - 17, 17, expectedFooter) == 0);
    // An explicit SYNC request bypasses the lazy two-second delay, even when
    // no new generation has yet been observed by the owner-core poll.
    terminalHistorySaveComplete(true);
    assert(!terminalHistorySaveDue());
    terminalHistoryForceSave();
    assert(terminalHistorySaveDue());
    terminalHistorySaveComplete(true);
    assert(!terminalHistorySaveDue());
    // A host reconnect requests only a PC mirror snapshot.  It must not mark
    // TFT cells dirty, because a retained xterm256 canvas belongs underneath
    // that terminal surface.
    std::fill(terminal.dirty, terminal.dirty + vt::Cells, false);
    std::fill(terminal.mirrorDirty, terminal.mirrorDirty + vt::Rows, false);
    terminal.mirrorCursorDirty = false;
    mirrorReset = false;
    for (uint8_t c : std::string("\033[>62v")) keyboard.feed(c, fakeTime);
    for (bool dirty : terminal.dirty) assert(!dirty);
    for (bool dirty : terminal.mirrorDirty) assert(dirty);
    assert(terminal.mirrorCursorDirty && mirrorReset);
    terminal.reset();
    mirrorReset = false;
    // The same runtime callback used by the buffered S3 target must also draw
    // a direct 320x240 Arduino_GFX row without touching text RAM or the PC
    // mirror.  The test stub represents that RP2350-style direct path.
    const uint16_t screenBeforeGcls = gfx->framebuffer[0];
    for (uint8_t c : std::string("\033[>4v"))
        terminal.feed(c);
    assert(!plotCapture && gfx->framebuffer[0] == screenBeforeGcls);
    assert(!mirrorReset); // Arming GCLS does not reset the PC view either.
    for (uint8_t c : std::string("\033[38;5;196m#\033[38;5;21m[\r\n\033[0m"))
        terminal.feed(c);
    assert(gfx->framebuffer[plotBaseY * 320 + plotBaseX] == 0xf800 &&
           gfx->framebuffer[plotBaseY * 320 + plotBaseX + 1] == 0x001f);
    assert(terminal.charRAM[0] == ' ' && !terminal.plotting());
    // A partly off-screen scaled source sample is clipped rather than wrapped
    // or attempting an out-of-range display write.
    const std::string clippedGcls = "\033[>4;4;" + std::to_string(319 - plotBaseX) +
                                    ";" + std::to_string(239 - plotBaseY) +
                                    "v\033[38;5;196m#\033[0m";
    for (uint8_t c : clippedGcls)
        terminal.feed(c);
    assert(gfx->framebuffer[239 * 320 + 319] == 0xf800 &&
           gfx->framebuffer[239 * 320 + 318] == 0xffff &&
           gfx->framebuffer[238 * 320 + 319] == 0xffff);
    // The extended GCLS parameters scale source samples and place them in
    // physical pixels, without changing logical terminal geometry.
    terminal.reset();
    for (uint8_t c : std::string("\033[>4;2;10;100v"))
        terminal.feed(c);
    for (uint8_t c : std::string("\033[38;5;196m#\033[38;5;21m[\r\n\033[0m"))
        terminal.feed(c);
    assert(gfx->framebuffer[(plotBaseY + 100) * 320 + plotBaseX + 10] == 0xf800 &&
           gfx->framebuffer[(plotBaseY + 101) * 320 + plotBaseX + 11] == 0xf800);
    assert(gfx->framebuffer[(plotBaseY + 100) * 320 + plotBaseX + 12] == 0x001f &&
           gfx->framebuffer[(plotBaseY + 101) * 320 + plotBaseX + 13] == 0x001f);
    assert(gfx->framebuffer[(plotBaseY + 100) * 320 + plotBaseX + 9] == 0xffff &&
           gfx->framebuffer[(plotBaseY + 99) * 320 + plotBaseX + 10] == 0xffff);
    assert(plotEndCleanupPending);
    for (bool dirty : terminal.dirty) assert(!dirty);
    fakeTime += 100;
    plotEndCleanupPoll(fakeTime);
    assert(!plotEndCleanupPending);
    // The plot backlog is discarded, then only the logical status/current
    // rows are scheduled to remove any old active-line/cursor paint.
    for (int i = 0; i < 2 * vt::Cols; ++i) assert(terminal.dirty[i]);
    for (int i = 2 * vt::Cols; i < vt::Cells; ++i) assert(!terminal.dirty[i]);
    // The active-line paint state also belongs to the suppressed terminal
    // plane.  PlotEnd retires it and dirties only that old row, so a SUB-file
    // GCLS cannot leave a pale strip under the benchmark's status line.
    activeRow = 4;
    activeRowBank = 0;
    terminal.y = 7;
    std::fill(plotActiveRows, plotActiveRows + vt::MaxRows, false);
    plotActiveRows[2] = plotActiveRows[4] = true;
    std::fill(terminal.dirty, terminal.dirty + vt::Cells, false);
    terminalPlot(nullptr, vt::Terminal::PlotEnd, 0);
    assert(plotEndCleanupPending);
    fakeTime += 100;
    plotEndCleanupPoll(fakeTime);
    assert(activeRow == -1 && activeRowBank == 255 && !plotActiveRows[2] && !plotActiveRows[4]);
    for (int i = 0; i < vt::Cols; ++i) assert(terminal.dirty[2 * vt::Cols + i]);
    for (int i = 0; i < vt::Cols; ++i) assert(terminal.dirty[4 * vt::Cols + i]);
    for (int i = 0; i < vt::Cols; ++i) assert(terminal.dirty[6 * vt::Cols + i]);
    for (int i = 0; i < vt::Cols; ++i) assert(terminal.dirty[7 * vt::Cols + i]);
    terminal.x = 0;
    terminal.y = 1;
    // A USB mirror reconnect must refresh only the PC, never invalidate the
    // retained TFT plot beneath the terminal plane.
    // A reconnect after plotting has completed must invalidate the PC mirror
    // even though the TFT canvas remains retained.  Otherwise it transmits
    // only ESC[2J and leaves the host blank forever.
    std::fill(terminal.mirrorDirty, terminal.mirrorDirty + vt::Rows, false);
    terminal.mirrorCursorDirty = false;
    const uint16_t untouchedBeforeReconnect = gfx->framebuffer[0];
    Serial.capacity = 0; // inspect state before mirrorPump can consume a row
    Serial.online = false;
    terminalLoop();
    Serial.online = true;
    for (int i = 0; i < 4; ++i) terminalLoop();
    assert(gfx->framebuffer[(plotBaseY + 100) * 320 + plotBaseX + 10] == 0xf800 &&
           gfx->framebuffer[(plotBaseY + 100) * 320 + plotBaseX + 12] == 0x001f &&
           gfx->framebuffer[0] == untouchedBeforeReconnect);
    for (bool dirty : terminal.mirrorDirty) assert(dirty);
    assert(terminal.mirrorCursorDirty);
    Serial.capacity = 64;
    Serial.online = false; // preserve the later disconnected-mirror fixture
    terminalLoop();
    Serial.tx.clear();
    terminal.feed('D');
    assert(terminal.dirty[80]); // post-plot text still requests normal rendering
    terminal.reset(); // Restore the original test fixture after the GCLS transaction.
    for (int i = 0; i < 200; ++i) terminalLoop();
    const uint16_t paleGreen = rgb565({205, 255, 205});
    const uint16_t cursorGreen = rgb565({0, 150, 40});
    assert(gfx->framebuffer[5 * 320 + presentationGutter + 5 * 6 + 3] == paleGreen);
    assert(gfx->framebuffer[5 * 320 + presentationGutter + 3] == cursorGreen);
    // The dark cursor paper must retain readable active-line-green glyph ink.
    terminal.x = terminal.y = 0;
    terminal.charRAM[0] = 'A';
    cursorPhase = true;
    drawCell(0);
    bool cursorHasPaleInk = false;
    for (int pixel = 0; pixel < 6 * 10; ++pixel)
        cursorHasPaleInk |= gfx->framebuffer[(pixel / 6) * 320 + presentationGutter + pixel % 6] == paleGreen;
    assert(cursorHasPaleInk);
    selectedBank = 5;
    history->select(5);
    resetLineSession();
    terminal.reset();
    emit("A1>BBCBASIC");
    while (output.available()) terminalLoop();
    assert(history->activeBank() == 0); // CP/M prompt silently owns #0
    keyboardAction(nullptr, vt::Keyboard::HistoryBankShow);
    assert(std::string(reinterpret_cast<char *>(terminal.charRAM), 7) == "[#5 BBC");
    fakeTime += 1000;
    historyNoticePoll();
    emit("\r\nBBC BASIC (Z80)\r\n>");
    while (output.available()) terminalLoop();
    assert(history->activeBank() == 5 && lineSessionActive);
    terminal.reset();
    emit(">PRINT 12345");
    while (output.available()) terminalLoop();
    keyboardSend(nullptr, reinterpret_cast<const uint8_t *>("\r"), 1);
    uint8_t testByte;
    while (input.pop(testByte)) {}
    emit("\r\n 12345\r\n>");
    while (output.available()) terminalLoop();
    const std::string secondLine = "PRINT 67890\r";
    keyboardSend(nullptr, reinterpret_cast<const uint8_t *>(secondLine.data()), secondLine.size());
    while (input.pop(testByte)) {}
    emit("\r\n 67890\r\n>");
    while (output.available()) terminalLoop();
    profileKey(vt::AppUp);
    const std::string bbcClear = "\x06\x18";
    std::string bbcActual;
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == bbcClear && recallStagedValid);
    profileKey(vt::AppUp);
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual.empty() && recallPendingValid); // coalesced until BBC echoes
    fakeTime += 50;
    recallPoll();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == "PRINT 67890");
    terminal.reset();
    emit(">PRINT 67890");
    while (output.available()) terminalLoop();
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == bbcClear && recallStagedValid);
    fakeTime += 50;
    recallPoll();
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == "PRINT 12345");
    // An application-generated EDIT line becomes the current history line.
    keyboardSend(nullptr, reinterpret_cast<const uint8_t *>("\r"), 1);
    while (input.pop(testByte)) {}
    terminal.reset();
    resetLineSession(); // EDIT's numbered row must reactivate history itself.
    emit("   10 PRINT 42");
    while (output.available()) terminalLoop();
    assert(lineSessionActive);
    keyboardSend(nullptr, reinterpret_cast<const uint8_t *>("\r"), 1);
    while (input.pop(testByte)) {}
    emit("\r\n>"); // BBC returns to its immediate prompt after EDIT Enter.
    while (output.available()) terminalLoop();
    profileKey(vt::AppUp);
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == bbcClear && recallStagedValid);
    fakeTime += 50;
    recallPoll();
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == "10 PRINT 42");
    keyboardAction(nullptr, vt::Keyboard::HistoryDelete);
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == bbcClear); // native clear includes the EDIT suffix
    // A source operator near column zero is not a prompt. The numbered line
    // and suffix must survive a filtered completion at its inner cursor.
    terminal.reset();
    emit("   10 A>B");
    while (output.available()) terminalLoop();
    uint8_t sourceCursor;
    uint8_t sourceLength = readCursorRow(screenRowCapture, sizeof(screenRowCapture), sourceCursor);
    assert(std::string(screenRowCapture, sourceLength) == "10 A>B");
    assert(history->appendEntry(5, "for", 3));
    history->discardCurrent();
    resetLineSession();
    terminal.reset();
    emit("   10 f i = 1 TO 100\033[1;8H");
    while (output.available()) terminalLoop();
    assert(lineSessionActive);
    profileKey(vt::AppUp);
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == bbcClear && recallStagedValid);
    fakeTime += 100;
    recallPoll();
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == "10 for i = 1 TO 100");
    // MBASIC's EDIT command is deliberately adapted only while it is modal.
    history->select(1);
    selectedBank = 1;
    resetLineSession();
    terminal.reset();
    const std::string edit = "edit 10\r";
    keyboardSend(nullptr, reinterpret_cast<const uint8_t *>(edit.data()), edit.size());
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == "edit 10\r" && mbasicEditMode && mbasicEditStartPending);
    fakeTime += 100;
    mbasicEditPoll();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == "edit 10\rL" && !mbasicEditStartPending);
    emit("10 print 42");
    while (output.available()) terminalLoop();
    profileKey(vt::AppLeft);
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == "\b");
    profileKey(vt::AppDelete);
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == "1D");
    keyboardAction(nullptr, vt::Keyboard::ProfileBackspace);
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == "\b1D");
    keyboardSend(nullptr, reinterpret_cast<const uint8_t *>("\r"), 1);
    while (input.pop(testByte)) {}
    assert(!mbasicEditMode);
    history->discardCurrent();
    terminal.reset();
    lineModelAuthoritative = true;
    const std::string recalled = "abc";
    keyboardSend(nullptr, reinterpret_cast<const uint8_t *>(recalled.data()), recalled.size());
    while (input.pop(testByte)) {}
    profileKey(vt::AppLeft);
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == "\x01" && mbasicEditMode && mbasicEditStartPending);
    fakeTime += 100;
    mbasicEditPoll();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == "\x01  " && !mbasicEditStartPending);
    keyboardSend(nullptr, reinterpret_cast<const uint8_t *>("\r"), 1);
    while (input.pop(testByte)) {}
    history->discardCurrent();
    terminal.reset();
    lineModelAuthoritative = true;
    const std::string partial = "abc";
    keyboardSend(nullptr, reinterpret_cast<const uint8_t *>(partial.data()), partial.size());
    while (input.pop(testByte)) {}
    profileCommand(true);
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == "\b\b\b\rsystem\r");
    // The launcher row can remain visible after its CR. It must cease to be
    // effective CCP input and must not become MBASIC's first history line.
    history->select(1);
    selectedBank = 1;
    resetLineSession();
    terminal.reset();
    emit("A7>");
    while (output.available()) terminalLoop();
    assert(ccpPromptLive && history->activeBank() == 0);
    const std::string launch = "mbasic\r";
    keyboardSend(nullptr, reinterpret_cast<const uint8_t *>(launch.data()), launch.size());
    while (input.pop(testByte)) {}
    terminalLoop();
    assert(!ccpPromptLive && history->activeBank() == 1);
    emit("mbasic"); // stale launcher echo, still on the cursor row
    while (output.available()) terminalLoop();
    history->discardCurrent();
    lineModelAuthoritative = false;
    const uint8_t beforeAppCount = history->bankCount(1);
    const std::string appLine = "print 7\r";
    keyboardSend(nullptr, reinterpret_cast<const uint8_t *>(appLine.data()), appLine.size());
    while (input.pop(testByte)) {}
    char savedAppLine[vt::LineHistory::MaxLine]; uint8_t savedAppLength;
    assert(history->bankCount(1) == beforeAppCount + 1);
    assert(history->copyEntry(1, beforeAppCount, savedAppLine, savedAppLength));
    assert(std::string(savedAppLine, savedAppLength) == "print 7");
    history->select(0);
    selectedBank = 0;
    // All three internal-CCP prompt formats must activate the CCP history
    // bank. A drive-only prompt has no user digit, unlike the old detector.
    for (const char *ccpPrompt : {"A15>", "AF>", "A>"}) {
        terminal.reset();
        ccpPromptLive = false;
        emit(ccpPrompt);
        while (output.available()) terminalLoop();
        assert(ccpPromptLive && cpmPromptEndColumn() == int(strlen(ccpPrompt) - 1));
        assert(history->activeBank() == 0);
    }
    terminal.reset();
    ccpPromptLive = false;
    emit("AG>");
    while (output.available()) terminalLoop();
    assert(!ccpPromptLive && cpmPromptEndColumn() < 0);
    terminal.reset();
    emit("  A0>");
    while (output.available()) terminalLoop();
    assert(ccpPromptLive && history->activeBank() == 0);
    history->discardCurrent();
    lineModelAuthoritative = true;
    const std::string cpmLine = "abc";
    keyboardSend(nullptr, reinterpret_cast<const uint8_t *>(cpmLine.data()), cpmLine.size());
    while (input.pop(testByte)) {}
    profileKey(vt::AppHome);
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == "\x01\x01\x01" && history->currentCursor() == 0);
    profileKey(vt::AppEnd);
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == "\x06\x06\x06" && history->currentCursor() == 3);
    // CP/M recall moves to the tracked start then deletes every character in
    // place, so a suffix cannot survive a recall from a middle cursor.
    history->discardCurrent();
    const std::string recalledCpm = "OLD\r";
    keyboardSend(nullptr, reinterpret_cast<const uint8_t *>(recalledCpm.data()), recalledCpm.size());
    while (input.pop(testByte)) {}
    emit("\r\nA0>");
    while (output.available()) terminalLoop();
    const std::string partialCpm = "A NEW";
    keyboardSend(nullptr, reinterpret_cast<const uint8_t *>(partialCpm.data()), partialCpm.size());
    while (input.pop(testByte)) {}
    emit("A NEW");
    while (output.available()) terminalLoop();
    history->home(); history->right(); history->right(); // after the separating space
    profileKey(vt::AppUp);
    bbcActual.clear();
    while (input.pop(testByte)) bbcActual.push_back(char(testByte));
    assert(bbcActual == std::string("\x01\x01\x07\x07\x07\x07\x07OLD"));
    history->discardCurrent();
    profileKey(vt::AppHome);
    profileKey(vt::AppEnd);
    assert(!input.pop(testByte)); // blank Home/End never invoke native ^B/^W
    terminal.reset();
    for (uint8_t c : std::string("EDIT IN PROGRESS"))
        terminal.feed(c);
    const int savedX = terminal.x;
    keyboardAction(nullptr, vt::Keyboard::HistoryBankShow);
    assert(history->activeBank() == 0);
    assert(std::string(reinterpret_cast<char *>(terminal.charRAM), 6) == "[#0 CP");
    fakeTime += 1000;
    historyNoticePoll();
    assert(!historyNoticeActive && terminal.x == savedX);
    assert(std::string(reinterpret_cast<char *>(terminal.charRAM), 16) == "EDIT IN PROGRESS");
    // Selecting a bank with empty on_select must show its label temporarily,
    // then restore the prompt rather than clearing its row permanently.
    terminal.reset();
    for (uint8_t c : std::string("A0>")) terminal.feed(c);
    keyboardAction(nullptr, vt::Keyboard::Action(vt::Keyboard::HistoryBank0 + 2));
    assert(selectedBank == 2 && historyNoticeActive && !historyNoticeSendOnSelect);
    fakeTime += 1000;
    historyNoticePoll();
    assert(!historyNoticeActive && terminal.x == 3);
    assert(std::string(reinterpret_cast<char *>(terminal.charRAM), 3) == "A0>");
    assert(!input.pop(testByte));
    // A profile with on_select may still be chosen while editing inside an
    // application. Neither its command nor history-model backspaces may reach
    // the guest, and the notice must restore the exact row and cursor.
    terminalConfigBegin();
    for (uint8_t c : std::string("[bank3]\non_select=A2:\\r\n")) terminalConfigFeed(c);
    assert(terminalConfigEnd() == 0);
    terminal.reset();
    ccpPromptLive = false;
    for (uint8_t c : std::string("\033[4;6Hsource line")) terminal.feed(c);
    const int appX = terminal.x, appY = terminal.y;
    const int appFirst = terminal.index(0, appY);
    const std::vector<uint8_t> appChars(terminal.charRAM + appFirst,
                                        terminal.charRAM + appFirst + terminal.cols);
    const std::vector<uint8_t> appPaper(terminal.paperRAM + appFirst,
                                        terminal.paperRAM + appFirst + terminal.cols);
    const std::vector<uint8_t> appAttrs(terminal.attrRAM + appFirst,
                                        terminal.attrRAM + appFirst + terminal.cols);
    const std::vector<uint8_t> appFlags(terminal.flags + appFirst,
                                        terminal.flags + appFirst + terminal.cols);
    history->input('x'); // a tracked editor line must not become guest BS
    keyboardAction(nullptr, vt::Keyboard::Action(vt::Keyboard::HistoryBank0 + 3));
    assert(selectedBank == 3 && historyNoticeActive && !historyNoticeSendOnSelect);
    assert(terminal.x == appX && terminal.y == appY);
    assert(!input.pop(testByte));
    fakeTime += 1000;
    historyNoticePoll();
    assert(!historyNoticeActive && terminal.x == appX && terminal.y == appY);
    assert(std::equal(appChars.begin(), appChars.end(), terminal.charRAM + appFirst));
    assert(std::equal(appPaper.begin(), appPaper.end(), terminal.paperRAM + appFirst));
    assert(std::equal(appAttrs.begin(), appAttrs.end(), terminal.attrRAM + appFirst));
    assert(std::equal(appFlags.begin(), appFlags.end(), terminal.flags + appFirst));
    assert(!input.pop(testByte));
    // A stale launcher prompt can remain painted after the app has begun.
    // Its shape alone must not authorize on_select.
    terminal.reset();
    for (uint8_t c : std::string("A0>")) terminal.feed(c);
    ccpPromptLive = false;
    keyboardAction(nullptr, vt::Keyboard::Action(vt::Keyboard::HistoryBank0 + 3));
    assert(!historyNoticeSendOnSelect);
    fakeTime += 1000;
    historyNoticePoll();
    assert(std::string(reinterpret_cast<char *>(terminal.charRAM), 3) == "A0>");
    assert(terminal.x == 3 && terminal.y == 0 && !input.pop(testByte));
    // At a freshly emitted CCP prompt, the same profile still runs on_select
    // after restoring its notice; quick profile cycling must not hide CCP.
    terminal.reset();
    emit("A0>");
    while (output.available()) terminalLoop();
    assert(ccpPromptLive);
    keyboardAction(nullptr, vt::Keyboard::Action(vt::Keyboard::HistoryBank0 + 3));
    assert(historyNoticeSendOnSelect && terminal.x == 3 && terminal.y == 0);
    keyboardAction(nullptr, vt::Keyboard::Action(vt::Keyboard::HistoryBank0 + 2));
    keyboardAction(nullptr, vt::Keyboard::Action(vt::Keyboard::HistoryBank0 + 3));
    assert(historyNoticeSendOnSelect && terminal.x == 3 && terminal.y == 0);
    fakeTime += 1000;
    historyNoticePoll();
    assert(std::string(reinterpret_cast<char *>(terminal.charRAM), 3) == "A0>");
    std::string selectedCommand;
    while (input.pop(testByte)) selectedCommand.push_back(char(testByte));
    assert(selectedCommand == "A2:\r");
    // Transfer host uses Alt+Space to save selected bank, not CCP's effective
    // bank 0. Restoring bank + RAW-state peek in one command burst must cancel
    // its pending on_select (A2:) and leave the current A7 DU untouched.
    for (bool restoreRaw : {false, true}) {
        terminal.reset();
        emit("A7>");
        while (output.available()) terminalLoop();
        selectedBank = 3;
        history->select(0);
        rawInputMode = false;
        keyboardAction(nullptr, vt::Keyboard::HistoryBankShow);
        assert(std::string(reinterpret_cast<char *>(terminal.charRAM), 3) == "[#3");
        const auto hostControls = [&](const std::string &bytes) {
            for (uint8_t c : bytes) {
                if (rawInputMode) rawFeed(c, fakeTime);
                else keyboard.feed(c, fakeTime);
            }
        };
        hostControls("\033[>29v\033[>16v");
        assert(selectedBank == 9 && rawInputMode && !historyNoticeSendOnSelect);
        fakeTime += 1100;
        historyNoticePoll();
        hostControls(std::string("\033[>23v") + (restoreRaw ? "\033[>16v" : "\033[>17v"));
        assert(selectedBank == 3 && rawInputMode == restoreRaw);
        assert(!historyNoticeSendOnSelect);
        fakeTime += 1100;
        historyNoticePoll();
        assert(!input.pop(testByte));
        assert(std::string(reinterpret_cast<char *>(terminal.charRAM), 3) == "A7>");
    }
    rawInputMode = false;
    terminalConfigBegin();
    assert(terminalConfigEnd() == 0);
    selectedBank = 0;
    history->select(0);
    resetLineSession();
    for (uint8_t c : std::string("dir\r")) history->input(c);
    setRawInputMode(true);
    assert(rawInputMode && history->activeBank() == 0);
    assert(std::string(reinterpret_cast<char *>(terminal.charRAM), 13) == "[## RAW / #0]");
    const std::string exact("\0\n\177A\033X\r", 7);
    for (uint8_t c : exact) rawFeed(c, fakeTime);
    std::string received;
    uint8_t rawByte;
    while (input.pop(rawByte)) received.push_back(char(rawByte));
    assert(received == exact);
    // RAW bypasses profile remapping, but the host's private key envelope
    // must not leak into a CP/M transfer; send its original byte instead.
    const std::string rawKey = "\033[>70;1;" + std::to_string(vt::KeyNum1) + ";0;043v";
    for (uint8_t c : rawKey) rawFeed(c, fakeTime);
    assert(input.pop(rawByte) && rawByte == '+' && !input.pop(rawByte));
    assert(rawInputMode);
    for (uint8_t c : std::string("\033[>17v")) rawFeed(c, fakeTime);
    assert(!rawInputMode && history->activeBank() == 0);
    vt::LineHistory::Recall rawRecall;
    assert(history->recall(true, rawRecall));
    assert(std::string(rawRecall.text, rawRecall.length) == "dir");
    setRawInputMode(true);
    for (uint8_t c : std::string("\033[>23v")) rawFeed(c, fakeTime);
    assert(!rawInputMode && history->activeBank() == 3);
    fakeTime += 1000;
    historyNoticePoll();
    history->select(0);
    terminal.reset();
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
    assert(output.available() == 0 && terminal.charRAM[0] == 'S' &&
           backlight == xtgvBacklightDuty(16));
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
    // Exact host key path: PgUp/PgDn use private 33/34. From off, the first
    // adjustment restores the saved level; later presses move every time.
    xtgvBacklightSet(16);
    for (uint8_t b : std::string("\033[>63;33v")) Serial.rx.push_back(b);
    terminalLoop();
    assert(xtgvBacklightGet() == 17 && backlight == xtgvBacklightDuty(17));
    for (uint8_t b : std::string("\033[>63;34v")) Serial.rx.push_back(b);
    terminalLoop();
    assert(xtgvBacklightGet() == 16 && backlight == xtgvBacklightDuty(16));
    for (uint8_t b : std::string("\033[>63;0v\033[>63;33v")) Serial.rx.push_back(b);
    terminalLoop();
    assert(xtgvBacklightGet() == 16 && backlight == xtgvBacklightDuty(16));
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
    // Matching display INI data is applied by the display core after the
    // storage-owner core publishes it.  Zoomed pixels are exact 2x2 repeats.
    terminalConfigBegin();
    for (uint8_t c : std::string("[display:rp2_2]\nview_zoom=2\nzoom2_terminal=40x12\n"))
        terminalConfigFeed(c);
    assert(terminalConfigEnd() == 0);
    uint8_t savedBacklight = 0, savedZoom = 0;
    assert(!terminalConfigSaveDue(savedBacklight, savedZoom));
    assert(!displayProfileApplied.load());
    terminalLoop();
    assert(displayProfileApplied.load());
    assert(terminal.zoom == 2 && terminal.cols == 40 && terminal.rows == 12 &&
           terminal.visibleCols == 26 && terminal.viewport == 0);
    // Shift a line that begins outside the 2x viewport. Its text, paper and
    // framebuffer pixels must all appear at the panned destination row.
    terminal.reset();
    terminal.pan(10);
    for (uint8_t byte : std::string("\033[2J\033[3;11H\033[41m P\033[0m\033[3;11H\033[L"))
        terminal.feed(byte);
    for (int i = 0; i < 100; ++i) terminalLoop();
    assert(terminal.x == 0 && terminal.y == 2 && terminal.viewport == 10);
    assert(terminal.charRAM[terminal.index(11, 3)] == 'P');
    assert((terminal.attrRAM[terminal.index(10, 3)] >> 4) == 1);
    assert(gfx->framebuffer[(GFX_OFS_Y + 3 * 20 + 5) * 320 + GFX_OFS_X + 5] ==
           rgb565(vt::Paper[1]));
    // Rapid zoom keys advance from the pending view but compose only once.
    requestZoom(1);
    requestZoom(1);
    terminalLoop();
    assert(terminal.zoom == 2);
    fakeTime += 150;
    terminalLoop();
    assert(terminal.zoom == 4 && terminal.cols == 20 && terminal.rows == 6);
    requestZoom(-1);
    fakeTime += 150;
    terminalLoop();
    assert(terminal.zoom == 3 && terminal.cols == 26 && terminal.rows == 8);
    assert(!terminalConfigSaveDue(savedBacklight, savedZoom));
    fakeTime += 1500;
    assert(terminalConfigSaveDue(savedBacklight, savedZoom));
    assert(savedZoom == 3 && savedBacklight == xtgvBacklightGetRemembered());
    terminalConfigSaveComplete(true);
    assert(!terminalConfigSaveDue(savedBacklight, savedZoom));
    // SYNC/REFLASH bypass the lazy INI delay only for a pending change. A
    // failed forced save resumes the normal delay rather than retrying hot.
    terminalConfigForceSave();
    configSaveBacklight.store(7);
    configSaveZoom.store(2);
    configChangedAt.store(millis());
    configSavePending.store(true);
    assert(!terminalConfigSaveDue(savedBacklight, savedZoom));
    terminalConfigForceSave();
    assert(terminalConfigSaveDue(savedBacklight, savedZoom));
    assert(savedBacklight == 7 && savedZoom == 2);
    terminalConfigSaveComplete(false);
    assert(!terminalConfigSaveDue(savedBacklight, savedZoom));
    terminalConfigForceSave();
    assert(terminalConfigSaveDue(savedBacklight, savedZoom));
    terminalConfigSaveComplete(true);
    assert(!terminalConfigSaveDue(savedBacklight, savedZoom));
    // A long CCP-style output wraps and scrolls a tiny view without touching
    // backing cells or mirror rows beyond its active geometry.
    emit(std::string(200, 'X'));
    settle();
    assert(terminal.x >= 0 && terminal.x < terminal.cols && terminal.y >= 0 &&
           terminal.y < terminal.rows);
    requestZoom(-1);
    fakeTime += 150;
    terminalLoop();
    assert(terminal.zoom == 2);
    terminal.cursorVisible = false;
    terminal.charRAM[terminal.index(0, 0)] = 'A';
    terminal.attrRAM[terminal.index(0, 0)] = 8;
    terminal.flags[terminal.index(0, 0)] = 0;
    drawCell(terminal.index(0, 0));
    for (int y = 0; y < 20; y += 2)
        for (int x = 0; x < 12; x += 2) {
            const uint16_t pixel = gfx->framebuffer[y * 320 + x];
            assert(gfx->framebuffer[y * 320 + x + 1] == pixel);
            assert(gfx->framebuffer[(y + 1) * 320 + x] == pixel);
            assert(gfx->framebuffer[(y + 1) * 320 + x + 1] == pixel);
        }
    // A logical height larger than the panel remains intact and can be
    // panned independently of the full-plane PC mirror.
    assert(terminal.setGeometry(40, 24, 26, 12, 2));
    terminal.reset();
    const int lowerCell = terminal.index(0, 12);
    terminal.attrRAM[lowerCell] = 0x10;
    terminal.dirty[lowerCell] = true;
    terminal.panVertical(1);
    for (int i = 0; i < 1000; ++i) terminalLoop();
    assert(terminal.rowViewport == 12);
    assert(gfx->framebuffer[(GFX_OFS_Y + 5) * 320 + presentationGutter +
                            GFX_OFS_X + 5] == rgb565(terminal.palette.paper[1]));
    keyboardKeyEvent(nullptr, {vt::KeyHome, uint8_t(vt::KeyCtrl | vt::KeyShift |
                                                    vt::KeyAlt)}, nullptr, 0);
    for (int i = 0; i < 1000; ++i) terminalLoop();
    assert(terminal.rowViewport == 0);
    assert(terminal.setGeometry(40, 12, 26, 12, 2));
    terminal.reset();
    // A selected profile can narrow the logical grid without expanding the
    // fixed physical gutter; the unused panel area remains default paper.
    selectedBank = 0;
    history->select(0);
    terminalConfigBegin();
    const std::string legacyConfig =
        "[global]\nmode=vt52\nconsole_bits=8\nnum1=send:9\n"
        "[display:RP2_2]\nview_zoom=1\n"
        "[bank2]\nmode=ansi\ninput=wordstar\nzoom1_terminal=40x12\n"
        "num1=send:7\nkp_decimal=send:,\n"
        "[bank3]\nmode=vt52\ninput=vt52\nzoom1_terminal=20x12\n";
    for (uint8_t c : legacyConfig) terminalConfigFeed(c);
    assert(terminalConfigEnd() == 0 && terminalConfiguredConsoleBits() == 8);
    terminalLoop();
    assert(!terminal.ansi && terminal.cols == 80 && terminal.rows == 24);
    keyboardAction(nullptr, vt::Keyboard::Action(vt::Keyboard::HistoryBank0 + 2));
    assert(terminal.ansi && selectedBank == 2 && terminal.cols == 40 &&
           terminal.rows == 12 && terminal.visibleCols == 40 &&
           presentationGutter == 8);
    fakeTime += 1100;
    terminalLoop();
    uint8_t presetByte;
    while (input.pop(presetByte)) {}
    for (uint8_t c : std::string("\033[>70;1;28;0;043v")) keyboard.feed(c, fakeTime++);
    assert(input.pop(presetByte) && presetByte == '7' && !input.pop(presetByte));
    keyboardKeyEvent(nullptr, {vt::KeyNum2, 0}, reinterpret_cast<const uint8_t *>("ě"), 2);
    assert(input.pop(presetByte) && presetByte == '2' && !input.pop(presetByte));
    keyboardKeyEvent(nullptr, {vt::KeyKpDecimal, 0}, reinterpret_cast<const uint8_t *>(","), 1);
    assert(input.pop(presetByte) && presetByte == ',' && !input.pop(presetByte));
    keyboardKeyEvent(nullptr, {vt::KeyKpDelete, 0}, reinterpret_cast<const uint8_t *>("\033[3~"), 4);
    assert(input.pop(presetByte) && presetByte == 0x07 && !input.pop(presetByte));
    profileKey(vt::AppUp);
    assert(input.pop(presetByte) && presetByte == 0x05 && !input.pop(presetByte));
    for (int i = 0; i < 1000; ++i) terminalLoop();
    const uint16_t defaultPaper = rgb565(vt::Paper[0]);
    assert(gfx->framebuffer[100 * 320 + 300] == defaultPaper);
    gfx->framebuffer[100 * 320 + 300] = 0xf800;
    for (uint8_t c : std::string("\033[2J")) terminal.feed(c);
    for (int i = 0; i < 1000; ++i) terminalLoop();
    assert(gfx->framebuffer[100 * 320 + 300] == defaultPaper);
    keyboardAction(nullptr, vt::Keyboard::Action(vt::Keyboard::HistoryBank0 + 3));
    assert(!terminal.ansi && selectedBank == 3 && terminal.cols == 20 &&
           terminal.rows == 12 && presentationGutter == 8);
    fakeTime += 1100;
    terminalLoop();
    keyboardKeyEvent(nullptr, {vt::KeyNum1, 0}, reinterpret_cast<const uint8_t *>("+"), 1);
    assert(input.pop(presetByte) && presetByte == '9' && !input.pop(presetByte));
    keyboardKeyEvent(nullptr, {vt::KeyKpDecimal, 0}, reinterpret_cast<const uint8_t *>(","), 1);
    assert(input.pop(presetByte) && presetByte == '.' && !input.pop(presetByte));
    profileKey(vt::AppLeft);
    assert(input.pop(presetByte) && presetByte == 27);
    assert(input.pop(presetByte) && presetByte == 'D' && !input.pop(presetByte));
    for (uint8_t c : std::string("\033<")) terminal.feed(c);
    assert(terminal.ansi);
    selectEffectiveBank(0); // CCP history heuristic must not switch output.
    assert(terminal.ansi && selectedBank == 3 && history->activeBank() == 0);
    for (uint8_t c : std::string("\033[?2l")) terminal.feed(c);
    assert(!terminal.ansi);
    keyboardAction(nullptr, vt::Keyboard::Action(vt::Keyboard::HistoryBank0 + 2));
    assert(terminal.ansi); // explicit selection reapplies bank 2's default.
    // Palette startup, TFT/mirror colors, live selection and lazy save.
    terminalConfigBegin();
    const char *themeIni = "[global]\nmode_palette=1\n"
        "[palette1]\npaper0=8,16,24\nink00=120,200,120\n"
        "active_line_rgb=16,32,48\ncursor_rgb=80,96,112\nviewport_gutter_rgb=40,48,56\n"
        "[palette2]\npaper0=24,16,8\nink00=200,120,80\n"
        "[bank0]\nactive_line=false\n";
    for (const char *s=themeIni; *s; ++s) terminalConfigFeed(uint8_t(*s));
    assert(terminalConfigEnd()==0);
    terminalLoop();
    assert(activePalette==1 && terminal.palette.paper[0].r==8);
    terminal.reset(); activeRow=-1; terminal.cursorVisible=false;
    selectEffectiveBank(0);
    emit("\033[2J\033[HPALETTE"); settle();
    for (int n=0;n<1000;++n) terminalLoop();
    assert(Serial.tx.find("38;2;120;200;120;48;2;8;16;24m")!=std::string::npos);
    assert(gfx->framebuffer[200*320+100]==rgb565(terminal.palette.paper[0]));
    const int oldX=terminal.x, oldY=terminal.y;
    const uint8_t oldChar=terminal.charRAM[0];
    configSavePending=false;
    emit("\033[>69;2v"); settle(); terminalLoop(); settle();
    assert(activePalette==2 && terminal.x==oldX && terminal.y==oldY && terminal.charRAM[0]==oldChar);
    uint8_t savedLight=0, themeZoom=0, savedPalette=0;
    assert(!terminalConfigSaveDue(savedLight,themeZoom,&savedPalette));
    fakeTime+=1501;
    assert(terminalConfigSaveDue(savedLight,themeZoom,&savedPalette) && savedPalette==2);
    terminalConfigSaveComplete(true);
    // Theme must survive an independently requested zoom/profile geometry change.
    requestZoomLevel(2); fakeTime+=200; terminalLoop();
    assert(activePalette==2);
    terminal.beginPlot();
    terminalControl(nullptr,vt::Terminal::Palette,1); terminalLoop();
    assert(activePalette==2 && requestedPalette==1);
    terminal.abortPlot(); fakeTime+=200; terminalLoop(); terminalLoop();
    assert(activePalette==1);
    std::cout << "runtime render/mirror, slow/disconnected USB, queue saturation, and reconnect "
                 "tests passed\n";
}
