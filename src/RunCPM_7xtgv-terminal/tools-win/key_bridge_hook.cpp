// Loaded only into the explicitly selected Mintty GUI thread.
#include <windows.h>
#include <cstdio>
#include "key_bridge_map.h"
#include "key_bridge_config.h"

static bool deadPending = false, imeActive = false;
static constexpr wchar_t property[] = L"XTGV.KeyBridge.Active";
static HMODULE hookModule = nullptr;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) hookModule = instance;
    return TRUE;
}

// Bounded diagnostics from inside Mintty, independent of its native environment.
// Log only layout metadata and the first row event's routing, never typed text.
static void rowDiagnostic(const char *text) {
    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(hookModule, path, 32768);
    if (!length || length >= 32768) return;
    std::wstring name(path, length);
    auto slash = name.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    name.resize(slash + 1);
    name += L"key-row-" + std::to_wstring(GetCurrentProcessId()) + L".log";
    HANDLE file = CreateFileW(name.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text, DWORD(std::char_traits<char>::length(text)), &written, nullptr);
    CloseHandle(file);
}

static bool hasConfiguredNumberRow(HWND window, HKL layout) {
    static HKL previous = nullptr;
    static bool matches = false;
    if (layout == previous) return matches;
    const auto configured = xtgv_keys::windowRow(window);
    std::array<unsigned, 10> row{};
    BYTE plain[256]{}; // Detect unshifted layout independently of held keys/CapsLock.
    for (unsigned i = 0; i < row.size(); ++i) {
        const unsigned scan = i + 2;
        const unsigned vk = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX, layout);
        wchar_t chars[8]{};
        // This is a non-mutating layout query, just like the fallback below.
        if (vk && ToUnicodeEx(vk, scan, plain, chars, 8, 4, layout) == 1)
            row[i] = unsigned(chars[0]);
    }
    previous = layout;
    matches = xtgv_keys::numberRowMatches(row, configured);
    char report[512];
    int used = std::snprintf(report, sizeof(report),
        "Layout=%p config=window-properties enabled=%u match=%u\nConfigured:",
        reinterpret_cast<void *>(layout), unsigned(configured[0] != 0), unsigned(matches));
    for (unsigned c : configured)
        used += std::snprintf(report + used, sizeof(report) - used, " %04X", c);
    used += std::snprintf(report + used, sizeof(report) - used, "\nDetected:");
    for (unsigned c : row)
        used += std::snprintf(report + used, sizeof(report) - used, " %04X", c);
    std::snprintf(report + used, sizeof(report) - used, "\n");
    rowDiagnostic(report);
    return matches;
}

extern "C" __declspec(dllexport) LRESULT CALLBACK XtgvGetMessage(int code, WPARAM removing, LPARAM value) {
    if (code < 0 || removing != PM_REMOVE) return CallNextHookEx(nullptr, code, removing, value);
    auto *msg = reinterpret_cast<MSG *>(value);
    if (!msg || !GetPropW(msg->hwnd, property))
        return CallNextHookEx(nullptr, code, removing, value);
    SetPropW(msg->hwnd, property, reinterpret_cast<HANDLE>(2)); // loaded/ready handshake
    if (GetForegroundWindow() != msg->hwnd)
        return CallNextHookEx(nullptr, code, removing, value);
    if (msg->message == WM_DEADCHAR || msg->message == WM_SYSDEADCHAR) deadPending = true;
    if (msg->message == WM_CHAR || msg->message == WM_SYSCHAR) deadPending = false;
    if (msg->message == WM_IME_STARTCOMPOSITION) imeActive = true;
    if (msg->message == WM_IME_ENDCOMPOSITION) imeActive = false;
    if (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN)
        return CallNextHookEx(nullptr, code, removing, value);
    xtgv_keys::Key key;
    key.vk = unsigned(msg->wParam);
    key.scan = unsigned((msg->lParam >> 16) & 0xff);
    key.extended = (msg->lParam & (1L << 24)) != 0;
    key.composing = deadPending || imeActive;
    BYTE state[256]{};
    if (!GetKeyboardState(state)) return CallNextHookEx(nullptr, code, removing, value);
    auto down = [&state](int vk) { return (state[vk] & 0x80) != 0; };
    key.mods = (down(VK_CONTROL) ? vt::KeyCtrl : 0) |
               (down(VK_SHIFT) ? vt::KeyShift : 0) |
               (down(VK_LMENU) ? vt::KeyAlt : 0);
    key.altgr = down(VK_RMENU);
    key.win = down(VK_LWIN) || down(VK_RWIN);
    key.numlock = (state[VK_NUMLOCK] & 1) != 0;
    HKL layout = GetKeyboardLayout(0);
    const bool plainRow = !key.extended && xtgv_keys::topRowDigit(key.scan) >= 0 &&
        !key.composing && !key.altgr && !key.win && !(key.mods & (vt::KeyCtrl | vt::KeyAlt));
    key.rowMatches = plainRow && hasConfiguredNumberRow(msg->hwnd, layout);
    const bool plainCandidate = !key.extended && !key.composing && !key.altgr && !key.win &&
        !(key.mods & (vt::KeyCtrl | vt::KeyAlt)) &&
        (key.rowMatches || key.vk == VK_DECIMAL);
    if (plainCandidate) {
        wchar_t chars[8]{};
        // Win10 1607+: inspect the layout without modifying its dead-key state.
        int count = ToUnicodeEx(key.vk, key.scan, state, chars, 8, 4, layout);
        if (count > 0) {
            char utf8[32];
            int size = WideCharToMultiByte(CP_UTF8, 0, chars, count, utf8, 32, nullptr, nullptr);
            if (size > 0) key.fallback.assign(utf8, size);
        }
    }
    const auto packet = xtgv_keys::translate(key);
    static bool reportedRow = false;
    if (!reportedRow && xtgv_keys::topRowDigit(key.scan) >= 0) {
        reportedRow = true;
        char report[256];
        std::snprintf(report, sizeof(report),
            "First row: scan=%02X vk=%02X mods=%u extended=%u composing=%u altgr=%u win=%u match=%u fallback-bytes=%zu packet-bytes=%zu\n",
            key.scan, key.vk, key.mods, unsigned(key.extended), unsigned(key.composing),
            unsigned(key.altgr), unsigned(key.win), unsigned(key.rowMatches), key.fallback.size(), packet.size());
        rowDiagnostic(report);
    }
    if (!packet.empty()) {
        // We run on Mintty's own GUI thread, before TranslateMessage. Deliver
        // the complete ASCII protocol packet synchronously and suppress only
        // this keydown: no separate input stream, queue reordering or paste.
        unsigned repeat = LOWORD(msg->lParam);
        if (!repeat) repeat = 1;
        while (repeat--)
            for (unsigned char c : packet) SendMessageW(msg->hwnd, WM_CHAR, c, 1);
        msg->message = WM_NULL;
        msg->wParam = msg->lParam = 0;
    }
    return CallNextHookEx(nullptr, code, removing, value);
}
