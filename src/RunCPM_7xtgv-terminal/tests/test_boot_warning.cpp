#include "../src/terminal/boot_warning.h"
#include "../src/terminal/display_policy.h"
#include "../src/terminal/terminal.h"
#include <cassert>
#include <string>

int main() {
    std::string output;
    auto write = [&](const char *text) { output += text; };
    vt::writeBootWarning(write, nullptr, true);
    vt::writeBootWarning(write, "", false);
    assert(output.empty());
    const char *message = vt::displayHalDefaults().bootWarning;
    assert(message && std::string(message).size() < 52);
    vt::writeBootWarning(write, message, false);
    assert(output == std::string(message) + "\r\n"); // VT52: no leaked CSI
    output.clear();
    vt::writeBootWarning(write, message, true);
    vt::Terminal terminal;
    for (uint8_t c : output) terminal.feed(c);
    assert(terminal.x == 0 && terminal.y == 1);
    for (int col = 0; col < terminal.cols; ++col)
        assert((terminal.attrRAM[terminal.index(col, 0)] & 0x30) == 0x10);
    for (uint8_t c : std::string("A0>")) terminal.feed(c);
    for (int col = 0; col < 3; ++col)
        assert((terminal.attrRAM[terminal.index(col, 1)] & 0x30) == 0);
    output.clear();
    vt::writeBootWarning(write, "Unable to initialize SD card.", true);
    vt::writeBootWarning(write, "CPU halted.", true);
    vt::Terminal failed;
    for (uint8_t c : output) failed.feed(c);
    assert(failed.x == 0 && failed.y == 2);
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < failed.cols; ++col)
            assert((failed.attrRAM[failed.index(col, row)] & 0x30) == 0x10);
    failed.feed('X');
    assert((failed.attrRAM[failed.index(0, 2)] & 0x30) == 0);
    output.clear();
    vt::writeBootWarning(write, "Unable to initialize SD card.", false);
    vt::writeBootWarning(write, "CPU halted.", false);
    assert(output == "Unable to initialize SD card.\r\nCPU halted.\r\n");
}
