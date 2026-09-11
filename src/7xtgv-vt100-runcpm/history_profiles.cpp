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
    const bool cancel = !strncmp(value, "send_cancel:", 12);
    if (!cancel && strncmp(value, "send:", 5)) return false;
    if (!decode(value + (cancel ? 12 : 5), result.bytes, KeyBinding::MaxBytes, result.length)) return false;
    result.kind = result.length ? (cancel ? BindingSendCancel : BindingSend) : BindingNone;
    return true;
}

static bool parseBool(const char *value, bool &result) {
    if (!strcmp(value, "true") || !strcmp(value, "yes") || !strcmp(value, "1")) result = true;
    else if (!strcmp(value, "false") || !strcmp(value, "no") || !strcmp(value, "0")) result = false;
    else return false;
    return true;
}

static bool parseRGB(const char *value, uint8_t result[3]) {
    unsigned r, g, b; char tail;
    if (sscanf(value, "%u,%u,%u%c", &r, &g, &b, &tail) != 3 || r > 255 || g > 255 || b > 255)
        return false;
    result[0] = uint8_t(r); result[1] = uint8_t(g); result[2] = uint8_t(b);
    return true;
}

static bool parseGlobalKey(const char *value, GlobalKey &result) {
    if (!strcmp(value, "none")) result = GlobalKeyNone;
    else if (!strcmp(value, "lalt+ralt+escape")) result = GlobalKeyDualAltEscape;
    else if (!strcmp(value, "lalt+ralt+delete")) result = GlobalKeyDualAltDelete;
    else return false;
    return true;
}

static bool parseInputAdapter(const char *value, InputAdapter &result) {
    if (!strcmp(value, "none")) result = InputAdapterNone;
    else if (!strcmp(value, "mbasic_native")) result = InputAdapterMbasicNative;
    else if (!strcmp(value, "local_compose")) result = InputAdapterLocalCompose;
    else return false;
    return true;
}
} // namespace

HistoryProfiles::HistoryProfiles() { defaults(sets_[0]); defaults(sets_[1]); }

const HistoryProfile &HistoryProfiles::get(uint8_t bank) const {
    return sets_[active_.load(std::memory_order_acquire)].banks[bank < Banks ? bank : 0];
}
const GlobalProfile &HistoryProfiles::global() const {
    return sets_[active_.load(std::memory_order_acquire)].global;
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
    // Exact table embedded in the BBC BASIC 5.00 package's DIST.Z80.
    setKey(5, AppLeft, "send:\x0a");
    setKey(5, AppRight, "send:\x0c");
    setKey(5, AppHome, "send:\x06");
    setKey(5, AppEnd, "send:\x0e");
    setKey(5, AppDelete, "send:\x12");
    setKey(5, AppInsert, "send:\x11");
    setKey(5, AppUp, "history_previous");
    setKey(5, AppDown, "history_next");
    setKey(5, AppAltUp, "send:\x07");
    setKey(5, AppAltDown, "send:\x0f");
    decode("\\x06\\x18", set.banks[5].historyClearBytes, KeyBinding::MaxBytes,
           set.banks[5].historyClearLength);
    set.banks[5].historyRetypeDelayMs = 50;
    // Screen-derived input handles BASIC lines produced by EDIT as well as
    // characters typed through this terminal.
    set.banks[1].screenLine = true;
    set.banks[1].inputAdapter = InputAdapterMbasicNative;
    set.banks[1].commandNewLine = true;
    set.banks[5].screenLine = true;
    set.banks[5].trimLeft = true;
    decode(">", set.banks[5].linePrefixBytes, HistoryProfile::MaxPrefixBytes,
           set.banks[5].linePrefixLength);
    decode(">", set.banks[5].activatePrefixBytes, HistoryProfile::MaxPrefixBytes,
           set.banks[5].activatePrefixLength);
    decode("system\\r", set.banks[1].exitBytes, HistoryProfile::MaxCommandBytes,
           set.banks[1].exitLength);
    decode("print chr$(27);\"[2J\";chr$(27);\"[H\"\\r", set.banks[1].clearBytes, HistoryProfile::MaxCommandBytes,
           set.banks[1].clearLength);
    decode("BYE\\r", set.banks[4].exitBytes, HistoryProfile::MaxCommandBytes,
           set.banks[4].exitLength);
    decode("PAGE\\r", set.banks[4].clearBytes, HistoryProfile::MaxCommandBytes,
           set.banks[4].clearLength);
    decode("QUIT\\r", set.banks[5].exitBytes, HistoryProfile::MaxCommandBytes,
           set.banks[5].exitLength);
    decode("PRINT CHR$(27);\"[2J\";CHR$(27);\"[H\"\\r", set.banks[5].clearBytes,
           HistoryProfile::MaxCommandBytes, set.banks[5].clearLength);
    for (uint8_t bank : {uint8_t(0), uint8_t(9)})
        decode("CLS\\r", set.banks[bank].clearBytes, HistoryProfile::MaxCommandBytes,
               set.banks[bank].clearLength);
    // VTESTS is an MBASIC test suite, so it uses the same local composer,
    // history behavior, and unified commands while keeping its own bank.
    set.banks[7] = set.banks[1];
    strncpy(set.banks[7].name, "VTESTS MBASIC", HistoryProfile::MaxName);
    set.banks[7].name[HistoryProfile::MaxName] = 0;
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
    if (!strcmp(text, "[global]")) { section_ = -2; return; }
    int bank = -1; char tail = 0;
    if (sscanf(text, "[bank%d]%c", &bank, &tail) == 1) {
        if (bank >= 0 && bank < Banks) section_ = bank;
        else { section_ = -1; ++errors_; }
        return;
    }
    char *equals = strchr(text, '=');
    if (!equals) { ++errors_; return; }
    *equals = 0;
    char *keyName = trim(text), *value = trim(equals + 1);
    if (section_ == -2) {
        if (!strcmp(keyName, "exit_key")) {
            if (!parseGlobalKey(value, sets_[loading_].global.exitKey)) ++errors_;
        } else if (!strcmp(keyName, "clear_key")) {
            if (!parseGlobalKey(value, sets_[loading_].global.clearKey)) ++errors_;
        } else ++errors_;
        return;
    }
    if (section_ < 0) { ++errors_; return; }
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
    if (!strcmp(keyName, "history_clear")) {
        if (!decode(value, profile.historyClearBytes, KeyBinding::MaxBytes,
                    profile.historyClearLength)) ++errors_;
        return;
    }
    if (!strcmp(keyName, "history_retype_delay_ms")) {
        char tail = 0;
        unsigned delay = 0;
        if (sscanf(value, "%u%c", &delay, &tail) != 1 || delay > 1000)
            ++errors_;
        else
            profile.historyRetypeDelayMs = uint16_t(delay);
        return;
    }
    if (!strcmp(keyName, "line_prefix")) {
        if (!decode(value, profile.linePrefixBytes, HistoryProfile::MaxPrefixBytes,
                    profile.linePrefixLength)) ++errors_;
        return;
    }
    if (!strcmp(keyName, "activate_prefix")) {
        if (!decode(value, profile.activatePrefixBytes, HistoryProfile::MaxPrefixBytes,
                    profile.activatePrefixLength)) ++errors_;
        return;
    }
    if (!strcmp(keyName, "exit_command") || !strcmp(keyName, "clear_command")) {
        uint8_t *bytes = !strcmp(keyName, "exit_command") ? profile.exitBytes : profile.clearBytes;
        uint8_t &length = !strcmp(keyName, "exit_command") ? profile.exitLength : profile.clearLength;
        if (!decode(value, bytes, HistoryProfile::MaxCommandBytes, length)) ++errors_;
        return;
    }
    if (!strcmp(keyName, "screen_line")) {
        if (!parseBool(value, profile.screenLine)) ++errors_;
        return;
    }
    if (!strcmp(keyName, "input_adapter")) {
        if (!parseInputAdapter(value, profile.inputAdapter)) ++errors_;
        return;
    }
    if (!strcmp(keyName, "command_new_line")) {
        if (!parseBool(value, profile.commandNewLine)) ++errors_;
        return;
    }
    if (!strcmp(keyName, "trim_left")) {
        if (!parseBool(value, profile.trimLeft)) ++errors_;
        return;
    }
    if (!strcmp(keyName, "escape_cancels")) {
        if (!parseBool(value, profile.escapeCancels)) ++errors_;
        return;
    }
    if (!strcmp(keyName, "active_line")) {
        if (!parseBool(value, profile.activeLine)) ++errors_;
        return;
    }
    if (!strcmp(keyName, "active_line_rgb") || !strcmp(keyName, "cursor_rgb")) {
        if (!parseRGB(value, !strcmp(keyName, "active_line_rgb") ? profile.activeLineRGB
                                                                  : profile.cursorRGB)) ++errors_;
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
