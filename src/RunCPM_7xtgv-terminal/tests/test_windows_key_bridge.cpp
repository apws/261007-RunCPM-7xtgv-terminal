// Portable mapping proof. Native hook delivery still needs the Mintty probe.
#include "../tools-win/key_bridge_map.h"
#include <cassert>
#include <iostream>

int main() {
    using xtgv_keys::Key;
    using xtgv_keys::translate;
    Key key;
    key.czech = true;
    key.vk = '2'; key.fallback = "ě";
    assert(translate(key) == "\x1b[>70;1;" + std::to_string(vt::KeyNum2) + ";0;196155v");
    for (unsigned digit = 0; digit < 10; ++digit) {
        key.vk = '0' + digit;
        for (unsigned mods : {0u, unsigned(vt::KeyShift)}) {
            key.mods = mods;
            assert(translate(key) == xtgv_keys::event(vt::KeyNum0 + digit, mods, key.fallback));
        }
    }
    key.mods = 0; key.czech = false;
    assert(translate(key).empty());
    key.czech = true;
    key.altgr = true; assert(translate(key).empty()); key.altgr = false;
    key.win = true; assert(translate(key).empty()); key.win = false;
    key.composing = true; assert(translate(key).empty()); key.composing = false;
    key.fallback.clear(); assert(translate(key).empty());

    constexpr unsigned scans[] = {0x52,0x4f,0x50,0x51,0x4b,0x4c,0x4d,0x47,0x48,0x49};
    constexpr unsigned navigation[] = {0x2d,0x23,0x28,0x22,0x25,0x0c,0x27,0x24,0x26,0x21};
    constexpr unsigned levels[] = {0,4,7,11,14,18,21,25,28,31};
    for (unsigned digit = 0; digit < 10; ++digit) {
        for (unsigned location = 0; location < 3; ++location) {
            key = Key{};
            key.vk = location == 0 ? '0' + digit : location == 1 ? 0x60 + digit : navigation[digit];
            key.scan = location ? scans[digit] : 0;
            key.numlock = location != 0;
            key.mods = vt::KeyCtrl | (location == 2 ? 0 : vt::KeyShift);
            const auto brightness = translate(key);
            if (digit) assert(brightness == "\x1b[>63;" + std::to_string(levels[digit]) + "v");
            else assert(brightness.empty()); // CS+0 has no OFF meaning.
            key.mods |= vt::KeyAlt;
            if (!digit) assert(translate(key) == "\x1b[>63;0v");
            else if (digit <= 2) assert(translate(key) == "\x1b[>" + std::to_string(digit) + "v");
            else assert(translate(key).empty());
            key.altgr = true; assert(translate(key).empty()); key.altgr = false;
            if (location) {
                key.extended = true; assert(translate(key).empty()); // Dedicated navigation.
            }
        }
    }
    key = Key{}; key.vk = 0x6e; key.scan = 0x53; key.fallback = ",";
    assert(translate(key) == "\x1b[>70;1;" + std::to_string(vt::KeyKpDecimal) + ";0;044v");
    key.fallback = ".";
    assert(translate(key) == xtgv_keys::event(vt::KeyKpDecimal, 0, "."));
    key.vk = 0xbc; key.scan = 0x33; key.fallback = ","; // Ordinary punctuation.
    assert(translate(key).empty());
    key.vk = 0x2e; key.scan = 0x53; // NumLock OFF Delete remains Delete.
    assert(translate(key).empty());
    key = Key{}; key.vk = 0x61; key.scan = 0x4f; key.numlock = true; key.mods = vt::KeyCtrl;
    assert(translate(key).empty()); // Real Ctrl-only numeric input is not CS.
    std::cout << "Windows physical key mapping passed\n";
}
