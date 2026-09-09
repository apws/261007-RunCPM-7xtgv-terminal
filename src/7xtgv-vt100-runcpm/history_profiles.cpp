#include "history_profiles.h"
#include <ctype.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>

namespace vt {
namespace {
struct DefaultProfile { const char *name; const char *keys[AppKeyCount]; };

// Values use the same grammar as the INI file.
static const DefaultProfile Defaults[HistoryProfiles::Banks] = {
    {"CP/M",      {"send:\x01", "send:\x06", "send:\x02", "send:\x17", "send:\x07", "send:\x16", "history_previous", "history_next", "send:\x1b[A", "send:\x1b[B"}},
    {"MBASIC",    {"none", "none", "none", "none", "none", "none", "history_previous", "history_next", "none", "none"}},
    {"TE",        {"send:\x13", "send:\x04", "send:\x11" "S", "send:\x11" "D", "send:\x07", "none", "send:\x05", "send:\x18", "history_previous", "history_next"}},
    {"TURBO",     {"send:\x1b[D", "send:\x1b[C", "send:\x1b[H", "send:\x1b[F", "send:\x1b[3~", "send:\x1b[2~", "send:\x1b[A", "send:\x1b[B", "history_previous", "history_next"}},
    {"COMAL",     {"send:\x1b[D", "send:\x1b[C", "send:\x1b[H", "send:\x1b[F", "send:\x1b[3~", "send:\x1b[2~", "send:\x1b[A", "send:\x1b[B", "history_previous", "history_next"}},
    {"BBC BASIC", {"send:\x1b[D", "send:\x1b[C", "send:\x1b[H", "send:\x1b[F", "send:\x1b[3~", "send:\x1b[2~", "send:\x1b[A", "send:\x1b[B", "history_previous", "history_next"}},
    {"DDT",       {"none", "none", "none", "none", "none", "none", "history_previous", "history_next", "none", "none"}},
    {"VTESTS",    {"none", "none", "none", "none", "none", "none", "history_previous", "history_next", "none", "none"}},
    {"ANSI APP",  {"send:\x1b[D", "send:\x1b[C", "send:\x1b[H", "send:\x1b[F", "send:\x1b[3~", "send:\x1b[2~", "send:\x1b[A", "send:\x1b[B", "history_previous", "history_next"}},
    {"GENERAL",   {"send:\x01", "send:\x06", "send:\x02", "send:\x17", "send:\x07", "send:\x16", "history_previous", "history_next", "send:\x1b[A", "send:\x1b[B"}},
};

static const char *KeyNames[AppKeyCount] = {
    "left", "right", "home", "end", "delete", "insert", "up", "down", "alt_up", "alt_down",
    "page_up", "page_down",
    "ctrl_left", "ctrl_right", "ctrl_up", "ctrl_down", "ctrl_home", "ctrl_end",
    "ctrl_delete", "ctrl_insert", "ctrl_page_up", "ctrl_page_down",
    "alt_left", "alt_right", "alt_home", "alt_end", "alt_delete", "alt_insert",
    "alt_page_up", "alt_page_down"
};

static char *trim(char *s) {
    while (isspace(static_cast<unsigned char>(*s))) ++s;
    char *end = s + strlen(s);
    while (end > s && isspace(static_cast<unsigned char>(end[-1]))) --end;
    *end = 0;
    return s;
}

static bool hexDigit(char c, uint8_t &value) {
    if (c >= '0' && c <= '9') value = uint8_t(c - '0');
    else if (c >= 'a' && c <= 'f') value = uint8_t(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') value = uint8_t(c - 'A' + 10);
    else return false;
    return true;
}

static bool decode(const char *source, uint8_t *target, uint8_t capacity, uint8_t &length) {
    length = 0;
    while (*source) {
        uint8_t value = uint8_t(*source++);
        if (value == '\\') {
            char escape = *source++;
            if (!escape) return false;
            if (escape == 'r') value = 13;
            else if (escape == 'n') value = 10;
            else if (escape == 't') value = 9;
            else if (escape == 'e') value = 27;
            else if (escape == '\\') value = '\\';
            else if (escape == 'x') {
                uint8_t high, low;
                if (!hexDigit(source[0], high) || !hexDigit(source[1], low)) return false;
                value = uint8_t((high << 4) | low);
                source += 2;
            } else return false;
        }
        if (length >= capacity) return false;
        target[length++] = value;
    }
    return true;
}

static bool parseBinding(const char *value, KeyBinding &result) {
    result = KeyBinding{};
    if (!strcmp(value, "none")) return true;
    if (!strcmp(value, "history_previous")) { result.kind = BindingHistoryPrevious; return true; }
    if (!strcmp(value, "history_next")) { result.kind = BindingHistoryNext; return true; }
    if (strncmp(value, "send:", 5)) return false;
    if (!decode(value + 5, result.bytes, KeyBinding::MaxBytes, result.length)) return false;
    result.kind = result.length ? BindingSend : BindingNone;
    return true;
}
} // namespace

HistoryProfiles::HistoryProfiles() { defaults(sets_[0]); defaults(sets_[1]); }

const HistoryProfile &HistoryProfiles::get(uint8_t bank) const {
    return sets_[active_.load(std::memory_order_acquire)].banks[bank < Banks ? bank : 0];
}

void HistoryProfiles::defaults(Set &set) {
    set = Set{};
    for (uint8_t bank = 0; bank < Banks; ++bank) {
        HistoryProfile &profile = set.banks[bank];
        strncpy(profile.name, Defaults[bank].name, HistoryProfile::MaxName);
        for (uint8_t key = 0; key < AppKeyCount; ++key)
            parseBinding(Defaults[bank].keys[key] ? Defaults[bank].keys[key] : "none", profile.keys[key]);
    }
    auto setKey = [&](uint8_t bank, AppKey key, const char *value) {
        parseBinding(value, set.banks[bank].keys[key]);
    };
    // RunCPM/GENERAL retain the useful extended CCP controls.
    for (uint8_t bank : {uint8_t(0), uint8_t(9)}) {
        setKey(bank, AppCtrlLeft, "send:\x11\x02");
        setKey(bank, AppCtrlRight, "send:\x11\x17");
        setKey(bank, AppCtrlHome, "send:\x11\x18");
        setKey(bank, AppCtrlEnd, "send:\x11\x0b");
        setKey(bank, AppCtrlDelete, "send:\x0b");
        setKey(bank, AppCtrlInsert, "send:\x11\x05");
    }
    // TE v1.11 WordStar bindings from its embedded help screen.
    setKey(2, AppPageUp, "send:\x03");
    setKey(2, AppPageDown, "send:\x12");
    setKey(2, AppCtrlLeft, "send:\x01");
    setKey(2, AppCtrlRight, "send:\x06");
    setKey(2, AppCtrlHome, "send:\x11" "R");
    setKey(2, AppCtrlEnd, "send:\x11" "C");
    // Modifier-bearing ANSI sequences for configurable VT applications.
    for (uint8_t bank : {uint8_t(3), uint8_t(4), uint8_t(5), uint8_t(8)}) {
        setKey(bank, AppPageUp, "send:\x1b[5~"); setKey(bank, AppPageDown, "send:\x1b[6~");
        setKey(bank, AppCtrlLeft, "send:\x1b[1;5D"); setKey(bank, AppCtrlRight, "send:\x1b[1;5C");
        setKey(bank, AppCtrlUp, "send:\x1b[1;5A"); setKey(bank, AppCtrlDown, "send:\x1b[1;5B");
        setKey(bank, AppCtrlHome, "send:\x1b[1;5H"); setKey(bank, AppCtrlEnd, "send:\x1b[1;5F");
        setKey(bank, AppCtrlDelete, "send:\x1b[3;5~"); setKey(bank, AppCtrlInsert, "send:\x1b[2;5~");
        setKey(bank, AppCtrlPageUp, "send:\x1b[5;5~"); setKey(bank, AppCtrlPageDown, "send:\x1b[6;5~");
        setKey(bank, AppAltLeft, "send:\x1b[1;3D"); setKey(bank, AppAltRight, "send:\x1b[1;3C");
        setKey(bank, AppAltHome, "send:\x1b[1;3H"); setKey(bank, AppAltEnd, "send:\x1b[1;3F");
        setKey(bank, AppAltDelete, "send:\x1b[3;3~"); setKey(bank, AppAltInsert, "send:\x1b[2;3~");
        setKey(bank, AppAltPageUp, "send:\x1b[5;3~"); setKey(bank, AppAltPageDown, "send:\x1b[6;3~");
    }
}

void HistoryProfiles::beginLoad() {
    loading_ = uint8_t(1 - active_.load(std::memory_order_acquire));
    defaults(sets_[loading_]);
    section_ = -1; errors_ = 0; length_ = 0; overflow_ = false; eof_ = false;
}

void HistoryProfiles::feed(uint8_t byte) {
    if (eof_) return;
    if (byte == 0x1a) {
        if (length_ || overflow_) parseLine();
        eof_ = true;
        return;
    }
    if (byte == '\n' || byte == '\r') {
        if (length_ || overflow_) parseLine();
        return;
    }
    if (overflow_) return;
    if (length_ + 1 >= sizeof(line_)) { overflow_ = true; return; }
    line_[length_++] = char(byte);
}

void HistoryProfiles::finishLoad() {
    if (length_ || overflow_) parseLine();
    active_.store(loading_, std::memory_order_release);
}

void HistoryProfiles::parseLine() {
    if (overflow_) { ++errors_; overflow_ = false; length_ = 0; return; }
    line_[length_] = 0; length_ = 0;
    char *text = trim(line_);
    if (!*text || *text == '#' || *text == ';') return;
    int bank = -1; char tail = 0;
    if (sscanf(text, "[bank%d]%c", &bank, &tail) == 1) {
        if (bank >= 0 && bank < Banks) section_ = bank;
        else { section_ = -1; ++errors_; }
        return;
    }
    char *equals = strchr(text, '=');
    if (section_ < 0 || !equals) { ++errors_; return; }
    *equals = 0;
    char *keyName = trim(text), *value = trim(equals + 1);
    HistoryProfile &profile = sets_[loading_].banks[section_];
    if (!strcmp(keyName, "name")) {
        strncpy(profile.name, value, HistoryProfile::MaxName);
        profile.name[HistoryProfile::MaxName] = 0;
        return;
    }
    if (!strcmp(keyName, "on_select")) {
        if (!decode(value, profile.selectBytes, HistoryProfile::MaxSelectBytes, profile.selectLength)) ++errors_;
        return;
    }
    for (uint8_t key = 0; key < AppKeyCount; ++key) {
        if (!strcmp(keyName, KeyNames[key])) {
            if (!parseBinding(value, profile.keys[key])) ++errors_;
            return;
        }
    }
    ++errors_;
}

} // namespace vt
