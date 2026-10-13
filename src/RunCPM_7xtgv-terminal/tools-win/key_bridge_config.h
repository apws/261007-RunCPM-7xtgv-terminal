#pragma once
#include <windows.h>
#include <array>
#include <string>

namespace xtgv_keys
{
    // Explicit, immutable per-window configuration. MSYS process environments are
    // not necessarily visible through GetEnvironmentVariableW inside Mintty.
    inline std::wstring rowProperty(unsigned index)
    {
        return L"XTGV.KeyBridge.Row." + std::to_wstring(index);
    }

    inline void clearWindowRow(HWND window)
    {
        for (unsigned i = 0; i < 10; ++i)
            RemovePropW(window, rowProperty(i).c_str());
    }

    inline bool setWindowRow(HWND window, const std::array<unsigned, 10>& row)
    {
        for (unsigned i = 0; i < row.size(); ++i)
        {
            if (row[i] && !SetPropW(window, rowProperty(i).c_str(), reinterpret_cast<HANDLE>(ULONG_PTR(row[i]))))
            {
                clearWindowRow(window);
                return false;
            }
        }
        return true;
    }

    inline std::array<unsigned, 10> windowRow(HWND window)
    {
        std::array<unsigned, 10> row{};
        for (unsigned i = 0; i < row.size(); ++i)
            row[i] = unsigned(reinterpret_cast<ULONG_PTR>(GetPropW(window, rowProperty(i).c_str())));
        return row;
    }
}
