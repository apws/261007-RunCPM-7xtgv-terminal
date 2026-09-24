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

static const char *BaseKeyNames[] = {
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
static_assert(sizeof(BaseKeyNames) / sizeof(BaseKeyNames[0]) == KeyZ + 1,
              "key names must match the private keyboard protocol IDs");

static const KeyChord LegacyChords[AppKeyCount] = {
    {KeyLeft, 0}, {KeyRight, 0}, {KeyHome, 0}, {KeyEnd, 0},
    {KeyDelete, 0}, {KeyInsert, 0}, {KeyUp, 0}, {KeyDown, 0},
    {KeyUp, KeyAlt}, {KeyDown, KeyAlt},
    {KeyPageUp, 0}, {KeyPageDown, 0},
    {KeyLeft, KeyCtrl}, {KeyRight, KeyCtrl}, {KeyUp, KeyCtrl}, {KeyDown, KeyCtrl},
    {KeyHome, KeyCtrl}, {KeyEnd, KeyCtrl}, {KeyDelete, KeyCtrl}, {KeyInsert, KeyCtrl},
    {KeyPageUp, KeyCtrl}, {KeyPageDown, KeyCtrl},
    {KeyLeft, KeyAlt}, {KeyRight, KeyAlt}, {KeyHome, KeyAlt}, {KeyEnd, KeyAlt},
    {KeyDelete, KeyAlt}, {KeyInsert, KeyAlt}, {KeyPageUp, KeyAlt}, {KeyPageDown, KeyAlt}
};
static_assert(sizeof(LegacyChords) / sizeof(LegacyChords[0]) == AppKeyCount,
              "all existing profile keys need a chord");

static char *trim(char *s) {
    while (isspace(static_cast<unsigned char>(*s))) ++s;
    char *end = s + strlen(s);
    while (end > s && isspace(static_cast<unsigned char>(end[-1]))) --end;
    *end = 0;
    return s;
}
static bool equalNoCase(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower(static_cast<unsigned char>(*a++)) != tolower(static_cast<unsigned char>(*b++)))
            return false;
    }
    return !*a && !*b;
}
static bool startsNoCase(const char *text, const char *prefix) {
    while (*prefix) {
        if (!*text || tolower(static_cast<unsigned char>(*text++)) !=
                          tolower(static_cast<unsigned char>(*prefix++)))
            return false;
    }
    return true;
}
} // namespace

KeyChord chordForAppKey(AppKey key) {
    return key < AppKeyCount ? LegacyChords[key] : KeyChord{};
}

int appKeyForChord(KeyChord chord) {
    for (uint8_t i = 0; i < AppKeyCount; ++i)
        if (LegacyChords[i] == chord)
            return i;
    return -1;
}

bool parseKeyChordName(const char *name, KeyChord &result) {
    for (uint8_t i = KeyLeft; i <= KeyZ; ++i) {
        const char *suffix = name;
        const char *base = BaseKeyNames[i];
        while (*base && *suffix &&
               tolower(static_cast<unsigned char>(*base)) ==
                   tolower(static_cast<unsigned char>(*suffix))) {
            ++base;
            ++suffix;
        }
        if (*base) continue;

        KeyChord parsed{i, 0};
        if (startsNoCase(suffix, "_ctrl")) {
            parsed.modifiers |= KeyCtrl;
            suffix += 5;
        }
        if (startsNoCase(suffix, "_shift")) {
            parsed.modifiers |= KeyShift;
            suffix += 6;
        }
        if (startsNoCase(suffix, "_alt")) {
            parsed.modifiers |= KeyAlt;
            suffix += 4;
        }
        if (*suffix || !validKeyChord(parsed)) continue;
        result = parsed;
        return true;
    }
    return false;
}

namespace {
static bool parseGeometry(const char *value, Geometry &result) {
    unsigned cols = 0, rows = 0; char tail = 0;
    if (sscanf(value, "%ux%u%c", &cols, &rows, &tail) != 2 || cols > 65535 || rows > 65535)
        return false;
    result = {uint16_t(cols), uint16_t(rows)};
    return true;
}

static bool geometryFitsDisplay(const Geometry &geometry, uint8_t zoom,
                                const DisplayHalDefaults &hal) {
    return geometry.cols >= 2 && geometry.rows >= 1 && geometry.cols <= hal.maxCols &&
           geometry.rows <= hal.maxRows && geometry.rows * 10u * zoom <= hal.physicalHeight;
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
            else if (escape == '^') {
                const char control = *source++;
                if (control >= '@' && control <= '_') value = uint8_t(control & 0x1f);
                else if (control >= 'a' && control <= 'z') value = uint8_t(control - 'a' + 1);
                else if (control == '?') value = 127;
                else return false;
            }
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
    KeyBinding parsed{};
    if (!strcmp(value, "none")) { result = parsed; return true; }
    if (!strcmp(value, "history_previous")) {
        parsed.kind = BindingHistoryPrevious; result = parsed; return true;
    }
    if (!strcmp(value, "history_next")) {
        parsed.kind = BindingHistoryNext; result = parsed; return true;
    }
    const bool cancel = !strncmp(value, "send_cancel:", 12);
    if (!cancel && strncmp(value, "send:", 5)) return false;
    if (!decode(value + (cancel ? 12 : 5), parsed.bytes, KeyBinding::MaxBytes, parsed.length))
        return false;
    parsed.kind = parsed.length ? (cancel ? BindingSendCancel : BindingSend) : BindingNone;
    result = parsed;
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
static bool parseOutputMode(const char *value, OutputMode &result) {
    if (!strcmp(value, "ansi")) result = OutputAnsi;
    else if (!strcmp(value, "vt52")) result = OutputVT52;
    else return false;
    return true;
}
static bool parseInputPreset(const char *value, InputPreset &result) {
    if (!strcmp(value, "custom")) result = InputCustom;
    else if (!strcmp(value, "ansi")) result = InputAnsi;
    else if (!strcmp(value, "vt52")) result = InputVT52;
    else if (!strcmp(value, "wordstar")) result = InputWordStar;
    else return false;
    return true;
}
static void applyInputPreset(HistoryProfile &profile, uint32_t explicitKeys) {
    if (profile.input == InputCustom)
        return;
    auto bind = [&](AppKey key, const char *value) {
        if (!(explicitKeys & (uint32_t(1) << key)))
            parseBinding(value, profile.keys[key]);
    };
    for (uint8_t key = 0; key < AppKeyCount; ++key)
        bind(AppKey(key), "none");
    // Alt+arrows keep history available even when the ordinary cursor keys
    // belong to a legacy application. Explicit INI bindings can replace them.
    bind(AppAltUp, "history_previous");
    bind(AppAltDown, "history_next");
    if (profile.input == InputVT52) {
        bind(AppUp, "send:\x1b" "A"); bind(AppDown, "send:\x1b" "B");
        bind(AppRight, "send:\x1b" "C"); bind(AppLeft, "send:\x1b" "D");
    } else if (profile.input == InputWordStar) {
        bind(AppUp, "send:\x05"); bind(AppDown, "send:\x18");
        bind(AppLeft, "send:\x13"); bind(AppRight, "send:\x04");
        bind(AppHome, "send:\x11\x13"); bind(AppEnd, "send:\x11\x04");
        bind(AppPageUp, "send:\x12"); bind(AppPageDown, "send:\x03");
        bind(AppInsert, "send:\x16"); bind(AppDelete, "send:\x07");
    } else {
        bind(AppUp, "send:\x1b[A"); bind(AppDown, "send:\x1b[B");
        bind(AppLeft, "send:\x1b[D"); bind(AppRight, "send:\x1b[C");
        bind(AppHome, "send:\x1b[H"); bind(AppEnd, "send:\x1b[F");
        bind(AppPageUp, "send:\x1b[5~"); bind(AppPageDown, "send:\x1b[6~");
        bind(AppInsert, "send:\x1b[2~"); bind(AppDelete, "send:\x1b[3~");
        bind(AppCtrlLeft, "send:\x1b[1;5D"); bind(AppCtrlRight, "send:\x1b[1;5C");
        bind(AppCtrlUp, "send:\x1b[1;5A"); bind(AppCtrlDown, "send:\x1b[1;5B");
        bind(AppCtrlHome, "send:\x1b[1;5H"); bind(AppCtrlEnd, "send:\x1b[1;5F");
        bind(AppCtrlDelete, "send:\x1b[3;5~"); bind(AppCtrlInsert, "send:\x1b[2;5~");
        bind(AppCtrlPageUp, "send:\x1b[5;5~"); bind(AppCtrlPageDown, "send:\x1b[6;5~");
        bind(AppAltLeft, "send:\x1b[1;3D"); bind(AppAltRight, "send:\x1b[1;3C");
        bind(AppAltHome, "send:\x1b[1;3H"); bind(AppAltEnd, "send:\x1b[1;3F");
        bind(AppAltDelete, "send:\x1b[3;3~"); bind(AppAltInsert, "send:\x1b[2;3~");
        bind(AppAltPageUp, "send:\x1b[5;3~"); bind(AppAltPageDown, "send:\x1b[6;3~");
    }
}
} // namespace

HistoryProfiles::HistoryProfiles() {
    hal_ = &displayHalDefaults();
    defaults(sets_[0]); defaults(sets_[1]);
    displayDefaults(sets_[0]); displayDefaults(sets_[1]);
}

const HistoryProfile &HistoryProfiles::get(uint8_t bank) const {
    return sets_[active_.load(std::memory_order_acquire)].banks[bank < Banks ? bank : 0];
}
const GlobalProfile &HistoryProfiles::global() const {
    return sets_[active_.load(std::memory_order_acquire)].global;
}
const TextPalette &HistoryProfiles::palette(uint8_t index) const {
    return sets_[active_.load(std::memory_order_acquire)].palettes[index < Palettes ? index : 0];
}
const DisplayProfile &HistoryProfiles::display() const {
    return sets_[active_.load(std::memory_order_acquire)].display;
}
OutputMode HistoryProfiles::modeFor(uint8_t bank) const {
    const Set &set = sets_[active_.load(std::memory_order_acquire)];
    const HistoryProfile &profile = set.banks[bank < Banks ? bank : 0];
    return profile.modeOverride ? profile.mode : set.global.mode;
}
Geometry HistoryProfiles::geometryFor(uint8_t bank, uint8_t zoom) const {
    const Set &set = sets_[active_.load(std::memory_order_acquire)];
    const unsigned index = zoom >= 1 && zoom <= 4 ? zoom - 1 : 0;
    const HistoryProfile &profile = set.banks[bank < Banks ? bank : 0];
    return profile.geometryOverride[index] ? profile.geometry[index] : set.display.geometry[index];
}
Geometry HistoryProfiles::geometryFor(uint8_t bank, uint8_t zoom, Geometry displayGeometry) const {
    const Set &set = sets_[active_.load(std::memory_order_acquire)];
    const unsigned index = zoom >= 1 && zoom <= 4 ? zoom - 1 : 0;
    const HistoryProfile &profile = set.banks[bank < Banks ? bank : 0];
    return profile.geometryOverride[index] ? profile.geometry[index] : displayGeometry;
}

const KeyBinding *HistoryProfiles::bindingFor(uint8_t bank, KeyChord chord) const {
    if (!validKeyChord(chord)) return nullptr;
    const Set &set = sets_[active_.load(std::memory_order_acquire)];
    if (bank >= Banks) bank = 0;
    const int legacy = appKeyForChord(chord);
    if (legacy >= 0 && (set.explicitKeys[bank] & (uint32_t(1) << legacy)))
        return &set.banks[bank].keys[legacy];
    for (uint8_t i = 0; i < set.overrideCount; ++i)
        if (set.overrides[i].bank == bank && set.overrides[i].chord == chord)
            return &set.overrides[i].binding;
    for (uint8_t i = 0; i < set.overrideCount; ++i)
        if (set.overrides[i].bank == Banks && set.overrides[i].chord == chord)
            return &set.overrides[i].binding;
    return legacy >= 0 ? &set.banks[bank].keys[legacy] : nullptr;
}

bool HistoryProfiles::addOverride(Set &set, uint8_t bank, KeyChord chord,
                                  const KeyBinding &binding) {
    for (uint8_t i = 0; i < set.overrideCount; ++i)
        if (set.overrides[i].bank == bank && set.overrides[i].chord == chord)
            return false;
    if (set.overrideCount >= MaxOverrides) return false;
    ScopedBinding &entry = set.overrides[set.overrideCount++];
    entry.bank = bank;
    entry.chord = chord;
    entry.binding = binding;
    return true;
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
    set.banks[2].editingSurface = EditingSurfaceScreen;
    set.banks[3].editingSurface = EditingSurfaceScreen;
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
    set.banks[5].activateNumberedLine = true;
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

void HistoryProfiles::displayDefaults(Set &set) {
    const DisplayHalDefaults &hal = *hal_;
    set.display.viewZoom = hal.defaultZoom;
    set.display.renderFlushHz = hal.defaultRenderFlushHz;
    for (int i = 0; i < 4; ++i)
        set.display.geometry[i] = hal.zoomGeometry[i];
}

void HistoryProfiles::validateDisplay(Set &set) {
    const DisplayHalDefaults &hal = *hal_;
    if (set.display.viewZoom < 1 || set.display.viewZoom > 4) {
        ++errors_;
        set.display.viewZoom = hal.defaultZoom;
    }
    if (set.display.renderFlushHz > 60) {
        ++errors_;
        set.display.renderFlushHz = hal.defaultRenderFlushHz;
    }
    for (int i = 0; i < 4; ++i) {
        Geometry &geometry = set.display.geometry[i];
        const int zoom = i + 1;
        bool valid = geometryFitsDisplay(geometry, zoom, hal);
        if (!valid) {
            ++errors_;
            geometry = hal.zoomGeometry[i];
            continue;
        }
        // A visible boundary is deliberately even.  Active terminal widths
        // follow the same rule; make an otherwise valid explicit odd width
        // deterministic without increasing the malformed-config error count.
        geometry.cols &= ~1u;
        if (geometry.cols < 2) {
            ++errors_;
            geometry = hal.zoomGeometry[i];
        }
    }
}

void HistoryProfiles::validateBankGeometry(Set &set) {
    const DisplayHalDefaults &hal = *hal_;
    for (uint8_t bank = 0; bank < Banks; ++bank)
        for (int i = 0; i < 4; ++i) {
            HistoryProfile &profile = set.banks[bank];
            if (!profile.geometryOverride[i]) continue;
            Geometry &geometry = profile.geometry[i];
            const int zoom = i + 1;
            if (!geometryFitsDisplay(geometry, zoom, hal)) {
                ++errors_;
                profile.geometryOverride[i] = false;
            } else {
                geometry.cols &= ~1u;
            }
        }
}
void HistoryProfiles::resolveInputPresets(Set &set) {
    for (uint8_t bank = 0; bank < Banks; ++bank)
        applyInputPreset(set.banks[bank], set.explicitKeys[bank]);
}

void HistoryProfiles::beginLoad(const DisplayHalDefaults &hal) {
    hal_ = &hal;
    loading_ = uint8_t(1 - active_.load(std::memory_order_acquire));
    defaults(sets_[loading_]);
    displayDefaults(sets_[loading_]);
    section_ = -1; errors_ = 0; errorLineCount_ = 0; lineNumber_ = 1;
    length_ = 0; overflow_ = false; eof_ = false; previousWasCR_ = false;
    strcpy(sectionLabel_, "[none]");
}

void HistoryProfiles::feed(uint8_t byte) {
    if (eof_) return;
    // 7XTGVTRM.INI is intentionally CP/M-editor friendly: CR, LF, and CR/LF
    // all terminate a line, while the usual CP/M Ctrl+Z marks logical EOF.
    // This parser is deliberately separate from the checksummed history file.
    if (byte == 0x1a) {
        if (length_ || overflow_) parseLine();
        eof_ = true;
        return;
    }
    if (byte == '\r') {
        if (length_ || overflow_) parseLine();
        ++lineNumber_;
        previousWasCR_ = true;
        return;
    }
    if (byte == '\n') {
        if (previousWasCR_) {
            previousWasCR_ = false;
            return;
        }
        if (length_ || overflow_) parseLine();
        ++lineNumber_;
        return;
    }
    previousWasCR_ = false;
    if (overflow_) return;
    if (length_ + 1 >= sizeof(line_)) { overflow_ = true; return; }
    line_[length_++] = char(byte);
}

void HistoryProfiles::setSectionLabel(const char *text) {
    strncpy(sectionLabel_, text, sizeof(sectionLabel_) - 1);
    sectionLabel_[sizeof(sectionLabel_) - 1] = 0;
}

void HistoryProfiles::recordError(const char *text) {
    ++errors_;
    if (errorLineCount_ >= MaxErrorLines)
        return;
    ErrorLine &entry = errorLines_[errorLineCount_++];
    entry.number = lineNumber_;
    strncpy(entry.section, sectionLabel_, sizeof(entry.section) - 1);
    entry.section[sizeof(entry.section) - 1] = 0;
    strncpy(entry.text, text, MaxErrorText);
    entry.text[MaxErrorText] = 0;
}

void HistoryProfiles::finishLoad() {
    if (length_ || overflow_) parseLine();
    validateDisplay(sets_[loading_]);
    validateBankGeometry(sets_[loading_]);
    resolveInputPresets(sets_[loading_]);
    active_.store(loading_, std::memory_order_release);
}

void HistoryProfiles::parseLine() {
    if (overflow_) {
        line_[length_] = 0;
        const char *heading = line_;
        while (*heading == ' ' || *heading == '\t') ++heading;
        if (*heading == '[') { section_ = -1; setSectionLabel("[invalid]"); }
        recordError(line_);
        overflow_ = false;
        length_ = 0;
        return;
    }
    line_[length_] = 0; length_ = 0;
    char raw[sizeof(line_)];
    strncpy(raw, line_, sizeof(raw));
    raw[sizeof(raw) - 1] = 0;
    const auto fail = [&] { recordError(raw); };
    char *text = trim(line_);
    if (!*text || *text == '#' || *text == ';') return;
    if (equalNoCase(text, "[global]")) { section_ = -2; setSectionLabel("[global]"); return; }
    if (startsNoCase(text, "[palette")) {
        if (strlen(text) == 10 && text[8] >= '0' && text[8] <= '9' && text[9] == ']') {
            section_ = Banks + text[8] - '0';
            setSectionLabel(raw);
        } else { section_ = -1; setSectionLabel("[invalid]"); fail(); }
        return;
    }
    if (startsNoCase(text, "[display:") && text[strlen(text) - 1] == ']') {
        text[strlen(text) - 1] = 0;
        section_ = equalNoCase(text + 9, hal_->id) ? -3 : -4;
        setSectionLabel(raw);
        return;
    }
    int bank = -1, headingLength = -1;
    if (sscanf(text, "[bank%d]%n", &bank, &headingLength) == 1 &&
        headingLength > 0 && !text[headingLength]) {
        if (bank >= 0 && bank < Banks) { section_ = bank; setSectionLabel(raw); }
        else { section_ = -1; setSectionLabel("[invalid]"); fail(); }
        return;
    }
    // An unrecognized/malformed heading must not leave the previous section
    // active: following settings belong to neither that bank nor that display.
    if (*text == '[') { section_ = -1; setSectionLabel("[invalid]"); fail(); return; }
    char *equals = strchr(text, '=');
    if (!equals) { fail(); return; }
    *equals = 0;
    char *keyName = trim(text), *value = trim(equals + 1);
    if (section_ == -2) {
        if (!strcmp(keyName, "mode_palette")) {
            if (strlen(value) == 1 && value[0] >= '0' && value[0] <= '9')
                sets_[loading_].global.modePalette = uint8_t(value[0] - '0');
            else fail();
        } else if (!strcmp(keyName, "mode")) {
            if (!parseOutputMode(value, sets_[loading_].global.mode)) fail();
        } else if (!strcmp(keyName, "console_bits")) {
            if (!strcmp(value, "7")) sets_[loading_].global.consoleBits = 7;
            else if (!strcmp(value, "8")) sets_[loading_].global.consoleBits = 8;
            else fail();
        } else if (!strcmp(keyName, "exit_key")) {
            if (!parseGlobalKey(value, sets_[loading_].global.exitKey)) fail();
        } else if (!strcmp(keyName, "clear_key")) {
            if (!parseGlobalKey(value, sets_[loading_].global.clearKey)) fail();
        } else if (!strcmp(keyName, "cursor_blink_ms")) {
            char tail = 0;
            unsigned period = 0;
            if (sscanf(value, "%u%c", &period, &tail) != 1 || period < 1 || period > 5000)
                fail();
            else
                sets_[loading_].global.cursorBlinkMs = uint16_t(period);
        } else if (!strcmp(keyName, "backlight_level")) {
            char tail = 0;
            unsigned level = 0;
            if (sscanf(value, "%u%c", &level, &tail) != 1 || level < 1 || level > 32)
                fail();
            else
                // Legacy full-on value 32 remains accepted, canonicalized to 31.
                sets_[loading_].global.backlightLevel = uint8_t(level > 31 ? 31 : level);
        } else if (!strcmp(keyName, "crt_rampup_ms")) {
            char tail = 0;
            unsigned duration = 0;
            if (sscanf(value, "%u%c", &duration, &tail) != 1 || duration > 60000)
                fail();
            else
                sets_[loading_].global.crtRampupMs = uint16_t(duration);
        } else if (!strcmp(keyName, "viewport_gutter_rgb")) {
            if (!strcmp(value, "palette")) sets_[loading_].global.gutterOverride = false;
            else if (parseRGB(value, sets_[loading_].global.viewportGutterRGB))
                sets_[loading_].global.gutterOverride = true;
            else fail();
        } else if (!strcmp(keyName, "viewport_active_gutter_rgb")) {
            if (!strcmp(value, "palette")) {
                sets_[loading_].global.activeGutterOverride = false;
            } else if (!strcmp(value, "active_line")) {
                sets_[loading_].global.activeGutterOverride = true;
                sets_[loading_].global.viewportActiveGutterFollowsLine = true;
            } else if (parseRGB(value, sets_[loading_].global.viewportActiveGutterRGB)) {
                sets_[loading_].global.activeGutterOverride = true;
                sets_[loading_].global.viewportActiveGutterFollowsLine = false;
            } else {
                fail();
            }
        } else {
            KeyChord chord;
            KeyBinding binding;
            if (!parseKeyChordName(keyName, chord) || !parseBinding(value, binding) ||
                !addOverride(sets_[loading_], Banks, chord, binding))
                fail();
        }
        return;
    }
    if (section_ >= Banks) {
        TextPalette &palette = sets_[loading_].palettes[section_ - Banks];
        mixworx::RGB *color = nullptr;
        const size_t keyLength = strlen(keyName);
        if (keyLength == 6 && !strncmp(keyName, "paper", 5) && keyName[5] >= '0' && keyName[5] <= '3')
            color = &palette.paper[keyName[5] - '0'];
        else if (keyLength == 5 && !strncmp(keyName, "ink", 3) &&
                 keyName[3] >= '0' && keyName[3] <= '1' && keyName[4] >= '0' && keyName[4] <= '9') {
            const unsigned index = (keyName[3] - '0') * 10 + keyName[4] - '0';
            if (index < 16) color = &palette.ink[index];
        } else if (!strcmp(keyName, "active_line_rgb")) color = &palette.activeLine;
        else if (!strcmp(keyName, "cursor_rgb")) color = &palette.cursor;
        else if (!strcmp(keyName, "viewport_gutter_rgb")) color = &palette.gutter;
        else if (!strcmp(keyName, "viewport_active_gutter_rgb")) {
            if (!strcmp(value, "active_line")) { palette.activeGutterFollowsLine = true; return; }
            color = &palette.activeGutter;
        }
        uint8_t rgb[3];
        if (!color || !parseRGB(value, rgb)) fail();
        else {
            *color = {rgb[0], rgb[1], rgb[2]};
            if (color == &palette.activeGutter) palette.activeGutterFollowsLine = false;
        }
        return;
    }
    if (section_ == -3) {
        DisplayProfile &display = sets_[loading_].display;
        if (!strcmp(keyName, "view_zoom")) {
            unsigned parsed = 0; char tail = 0;
            if (sscanf(value, "%u%c", &parsed, &tail) != 1 || parsed < 1 || parsed > 4) fail();
            else display.viewZoom = uint8_t(parsed);
        } else if (!strncmp(keyName, "zoom", 4) && keyName[4] >= '1' && keyName[4] <= '4' &&
                   !strcmp(keyName + 5, "_terminal")) {
            Geometry geometry;
            const uint8_t index = keyName[4] - '1';
            if (!parseGeometry(value, geometry) || !geometryFitsDisplay(geometry, index + 1, *hal_)) fail();
            else {
                geometry.cols &= ~1u;
                if (geometry.cols < 2) fail();
                else display.geometry[index] = geometry;
            }
        } else if (!strcmp(keyName, "render_flush_hz")) {
            unsigned parsed = 0; char tail = 0;
            if (sscanf(value, "%u%c", &parsed, &tail) != 1 || parsed > 60) fail();
            else display.renderFlushHz = uint16_t(parsed);
        } else if (!strcmp(keyName, "rgb_refresh_hz")) {
            // Saved for forward-compatible shared cards.  Physical RGB timing
            // is intentionally compile-time until its SD/display boot order is redesigned.
        } else fail();
        return;
    }
    if (section_ == -4)
        return;
    if (section_ < 0) { fail(); return; }
    HistoryProfile &profile = sets_[loading_].banks[section_];
    if (!strcmp(keyName, "mode")) {
        if (parseOutputMode(value, profile.mode)) profile.modeOverride = true;
        else fail();
        return;
    }
    if (!strcmp(keyName, "input")) {
        if (!parseInputPreset(value, profile.input)) fail();
        return;
    }
    if (!strncmp(keyName, "zoom", 4) && keyName[4] >= '1' && keyName[4] <= '4' &&
        !strcmp(keyName + 5, "_terminal")) {
        Geometry geometry;
        const int zoomIndex = keyName[4] - '1';
        if (!parseGeometry(value, geometry) || !geometryFitsDisplay(geometry, zoomIndex + 1, *hal_)) fail();
        else {
            geometry.cols &= ~1u;
            if (geometry.cols < 2) { fail(); return; }
            profile.geometry[zoomIndex] = geometry;
            profile.geometryOverride[zoomIndex] = true;
        }
        return;
    }
    if (!strcmp(keyName, "name")) {
        strncpy(profile.name, value, HistoryProfile::MaxName);
        profile.name[HistoryProfile::MaxName] = 0;
        return;
    }
    if (!strcmp(keyName, "on_select")) {
        if (!decode(value, profile.selectBytes, HistoryProfile::MaxSelectBytes, profile.selectLength)) fail();
        return;
    }
    if (!strcmp(keyName, "history_clear")) {
        if (!decode(value, profile.historyClearBytes, KeyBinding::MaxBytes,
                    profile.historyClearLength)) fail();
        return;
    }
    if (!strcmp(keyName, "history_retype_delay_ms")) {
        char tail = 0;
        unsigned delay = 0;
        if (sscanf(value, "%u%c", &delay, &tail) != 1 || delay > 1000)
            fail();
        else
            profile.historyRetypeDelayMs = uint16_t(delay);
        return;
    }
    if (!strcmp(keyName, "line_prefix")) {
        if (!decode(value, profile.linePrefixBytes, HistoryProfile::MaxPrefixBytes,
                    profile.linePrefixLength)) fail();
        return;
    }
    if (!strcmp(keyName, "activate_prefix")) {
        if (!decode(value, profile.activatePrefixBytes, HistoryProfile::MaxPrefixBytes,
                    profile.activatePrefixLength)) fail();
        return;
    }
    if (!strcmp(keyName, "exit_command") || !strcmp(keyName, "clear_command")) {
        uint8_t *bytes = !strcmp(keyName, "exit_command") ? profile.exitBytes : profile.clearBytes;
        uint8_t &length = !strcmp(keyName, "exit_command") ? profile.exitLength : profile.clearLength;
        if (!decode(value, bytes, HistoryProfile::MaxCommandBytes, length)) fail();
        return;
    }
    if (!strcmp(keyName, "screen_line")) {
        if (!parseBool(value, profile.screenLine)) fail();
        return;
    }
    if (!strcmp(keyName, "editing_surface")) {
        if (!strcmp(value, "line")) profile.editingSurface = EditingSurfaceLine;
        else if (!strcmp(value, "screen")) profile.editingSurface = EditingSurfaceScreen;
        else fail();
        return;
    }
    if (!strcmp(keyName, "activate_numbered_line")) {
        if (!parseBool(value, profile.activateNumberedLine)) fail();
        return;
    }
    if (!strcmp(keyName, "input_adapter")) {
        if (!parseInputAdapter(value, profile.inputAdapter)) fail();
        return;
    }
    if (!strcmp(keyName, "command_new_line")) {
        if (!parseBool(value, profile.commandNewLine)) fail();
        return;
    }
    if (!strcmp(keyName, "trim_left")) {
        if (!parseBool(value, profile.trimLeft)) fail();
        return;
    }
    if (!strcmp(keyName, "escape_cancels")) {
        if (!parseBool(value, profile.escapeCancels)) fail();
        return;
    }
    if (!strcmp(keyName, "active_line")) {
        if (!parseBool(value, profile.activeLine)) fail();
        return;
    }
    if (!strcmp(keyName, "active_line_rgb") || !strcmp(keyName, "cursor_rgb")) {
        const bool lineColor = !strcmp(keyName, "active_line_rgb");
        bool &overrideColor = lineColor ? profile.activeLineOverride : profile.cursorOverride;
        if (!strcmp(value, "palette")) overrideColor = false;
        else if (parseRGB(value, lineColor ? profile.activeLineRGB : profile.cursorRGB)) overrideColor = true;
        else fail();
        return;
    }
    KeyChord chord;
    KeyBinding binding;
    if (!parseKeyChordName(keyName, chord) || !parseBinding(value, binding)) {
        fail();
        return;
    }
    const int key = appKeyForChord(chord);
    if (key >= 0) {
        const uint32_t bit = uint32_t(1) << key;
        if (sets_[loading_].explicitKeys[section_] & bit) fail();
        else {
            profile.keys[key] = binding;
            sets_[loading_].explicitKeys[section_] |= bit;
        }
    } else if (!addOverride(sets_[loading_], uint8_t(section_), chord, binding)) {
        fail();
    }
}

} // namespace vt
