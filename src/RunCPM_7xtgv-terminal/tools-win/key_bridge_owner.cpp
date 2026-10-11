#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <string>
#include "key_bridge_config.h"
#include "key_bridge_map.h"

int wmain(int argc, wchar_t **argv) {
    std::array<unsigned, 10> row{};
    if (argc != 4 || wcsncmp(argv[1], L"xtgv-keys-", 10) || !xtgv_keys::decodeNumberRow(argv[3], row)) {
        std::fputs("Expected unique xtgv-keys- window class, absolute hook DLL path and hex row (or -)\n", stderr);
        return 2;
    }
    HWND window = nullptr;
    for (unsigned tries = 0; tries < 100 && !window; ++tries) {
        window = FindWindowW(argv[1], nullptr);
        if (!window) Sleep(50);
    }
    if (!window) { std::fputs("Mintty window not found\n", stderr); return 2; }
    DWORD pid = 0;
    DWORD thread = GetWindowThreadProcessId(window, &pid);
    HMODULE module = LoadLibraryExW(argv[2], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) { std::fprintf(stderr, "Hook DLL load failed: %lu\n", GetLastError()); return 2; }
    auto address = GetProcAddress(module, "XtgvGetMessage");
    HOOKPROC proc = nullptr;
    static_assert(sizeof(proc) == sizeof(address));
    std::memcpy(&proc, &address, sizeof(proc));
    const wchar_t *property = L"XTGV.KeyBridge.Active";
    if (!thread || !pid || !proc || GetPropW(window, property) ||
        !SetPropW(window, property, reinterpret_cast<HANDLE>(1))) {
        std::fputs("Cannot arm this Mintty window (or already armed)\n", stderr);
        FreeLibrary(module); return 2;
    }
    if (!xtgv_keys::setWindowRow(window, row)) {
        std::fputs("Cannot configure this Mintty window\n", stderr);
        RemovePropW(window, property); FreeLibrary(module); return 2;
    }
    HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, proc, module, thread);
    if (!hook) {
        std::fprintf(stderr, "Thread hook failed: %lu\n", GetLastError());
        xtgv_keys::clearWindowRow(window);
        RemovePropW(window, property); FreeLibrary(module); return 2;
    }
    PostMessageW(window, WM_NULL, 0, 0);
    unsigned waits = 0;
    while (GetPropW(window, property) != reinterpret_cast<HANDLE>(2) && waits++ < 100)
        Sleep(50);
    if (GetPropW(window, property) != reinterpret_cast<HANDLE>(2)) {
        std::fputs("Hook DLL did not acknowledge loading in Mintty\n", stderr);
        UnhookWindowsHookEx(hook); xtgv_keys::clearWindowRow(window);
        RemovePropW(window, property); FreeLibrary(module); return 2;
    }
    std::puts("READY"); std::fflush(stdout);
    // Parent holds the pipe open. Normal close, parent crash and terminal
    // disappearance all close it, releasing the hook without a resident task.
    char byte;
    DWORD count = 0;
    ReadFile(GetStdHandle(STD_INPUT_HANDLE), &byte, 1, &count, nullptr);
    UnhookWindowsHookEx(hook);
    xtgv_keys::clearWindowRow(window);
    RemovePropW(window, property);
    FreeLibrary(module);
    return 0;
}
