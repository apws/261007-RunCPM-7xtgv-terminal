#pragma once
#include <array>
#include <string>
#include "../src/terminal/key_chords.h"

namespace xtgv_keys {
inline bool decodeNumberRow(const std::wstring &encoded, std::array<unsigned, 10> &row) {
    row.fill(0);
    if (encoded == L"-") return true;
    if (encoded.size() != 40) return false;
    std::array<unsigned, 10> decoded{};
    for (unsigned i = 0; i < encoded.size(); ++i) {
        const auto c = encoded[i];
        const unsigned digit = c >= L'0' && c <= L'9' ? unsigned(c - L'0') :
            c >= L'a' && c <= L'f' ? unsigned(c - L'a' + 10) : 16;
        if (digit == 16) return false;
        decoded[i / 4] = decoded[i / 4] * 16 + digit;
    }
    for (unsigned c : decoded)
        if (c < 32 || (c >= 0xd800 && c <= 0xdfff)) return false;
    row = decoded;
    return true;
}
struct Key {
    unsigned vk = 0, scan = 0, mods = 0;
    bool extended = false, numlock = false, altgr = false, win = false;
    bool rowMatches = false, composing = false;
    std::string fallback;
};
inline int topRowDigit(unsigned scan) {
    return scan >= 0x02 && scan <= 0x0b ? int((scan - 1) % 10) : -1;
}
inline bool numberRowMatches(const std::array<unsigned, 10> &row,
                             const std::array<unsigned, 10> &configured) {
    // Empty configuration disables only plain/Shift number-row conversion.
    return configured[0] != 0 && row == configured;
}
inline int keypadDigit(unsigned scan) {
    switch (scan) {
    case 0x52: return 0; case 0x4f: return 1; case 0x50: return 2;
    case 0x51: return 3; case 0x4b: return 4; case 0x4c: return 5;
    case 0x4d: return 6; case 0x47: return 7; case 0x48: return 8;
    case 0x49: return 9; default: return -1;
    }
}
inline std::string event(unsigned base, unsigned mods, const std::string &fallback) {
    std::string out = "\x1b[>70;1;" + std::to_string(base) + ";" + std::to_string(mods) + ";";
    for (unsigned char c : fallback) {
        out += char('0' + c / 100);
        out += char('0' + c / 10 % 10);
        out += char('0' + c % 10);
    }
    return out + "v";
}
// Empty means leave the original Windows message to Mintty.
inline std::string translate(Key key) {
    if (key.altgr || key.win || key.composing) return {};
    const int row = key.extended ? -1 : topRowDigit(key.scan);
    const bool top = row >= 0;
    const int pad = key.extended ? -1 : keypadDigit(key.scan);
    const int digit = top ? row : pad;
    if (pad >= 0 && key.numlock && !(key.vk >= 0x60 && key.vk <= 0x69))
        key.mods |= vt::KeyShift; // Windows converted NumLock+Shift to navigation.
    if (digit >= 0) {
        if (key.mods == (vt::KeyCtrl | vt::KeyShift) && digit) {
            constexpr int levels[] = {0, 4, 7, 11, 14, 18, 21, 25, 28, 31};
            return "\x1b[>63;" + std::to_string(levels[digit]) + "v";
        }
        if (key.mods == (vt::KeyCtrl | vt::KeyShift | vt::KeyAlt)) {
            if (!digit) return "\x1b[>63;0v";
            if (digit <= 2) return "\x1b[>" + std::to_string(digit) + "v";
        }
        if (top && key.rowMatches && (key.mods == 0 || key.mods == vt::KeyShift) && !key.fallback.empty())
            return event(vt::KeyNum0 + digit, key.mods, key.fallback);
    }
    if (!key.extended && key.vk == 0x6e && !key.mods && !key.fallback.empty())
        return event(vt::KeyKpDecimal, 0, key.fallback);
    return {};
}
}
