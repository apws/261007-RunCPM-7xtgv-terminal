// Loaded only into the explicitly selected Mintty GUI thread.
#include <windows.h>
#include "key_bridge_map.h"

static bool deadPending = false, imeActive = false;
static constexpr wchar_t property[] = L"XTGV.KeyBridge.Active";

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
    key.czech = (reinterpret_cast<ULONG_PTR>(layout) & 0xffff) == 0x0405;
    const bool plainCandidate = !key.extended && !key.composing && !key.altgr && !key.win &&
        !(key.mods & (vt::KeyCtrl | vt::KeyAlt)) &&
        ((key.czech && key.vk >= '0' && key.vk <= '9') || key.vk == VK_DECIMAL);
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
