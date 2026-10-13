#pragma once
#include <stdint.h>

namespace vt
{

    // Stable IDs in the host-to-terminal CSI >70;1 keyboard event. These describe
    // logical positions in the 7xtgv keyboard vocabulary, not Unicode characters.
    enum KeyBase : uint8_t
    {
        KeyLeft = 1,
        KeyRight,
        KeyHome,
        KeyEnd,
        KeyDelete,
        KeyInsert,
        KeyUp,
        KeyDown,
        KeyPageUp,
        KeyPageDown,
        KeyBackspace,
        KeyEscape,
        KeyEnter,
        KeySpace,
        KeyF1,
        KeyF2,
        KeyF3,
        KeyF4,
        KeyF5,
        KeyF6,
        KeyF7,
        KeyF8,
        KeyF9,
        KeyF10,
        KeyF11,
        KeyF12,
        KeyNum0,
        KeyNum1,
        KeyNum2,
        KeyNum3,
        KeyNum4,
        KeyNum5,
        KeyNum6,
        KeyNum7,
        KeyNum8,
        KeyNum9,
        KeyBs2,
        KeyBs1,
        KeyP1,
        KeyP2,
        KeyL1,
        KeyL2,
        KeyL3,
        KeyZ1,
        KeyM1,
        KeyM2,
        KeyM3,
        KeyKpDecimal,
        KeyKpDelete,
        KeyA,
        KeyB,
        KeyC,
        KeyD,
        KeyE,
        KeyF,
        KeyG,
        KeyH,
        KeyI,
        KeyJ,
        KeyK,
        KeyL,
        KeyM,
        KeyN,
        KeyO,
        KeyP,
        KeyQ,
        KeyR,
        KeyS,
        KeyT,
        KeyU,
        KeyV,
        KeyW,
        KeyX,
        KeyY,
        KeyZ
    };

    enum KeyModifier : uint8_t
    {
        KeyCtrl = 1,
        KeyShift = 2,
        KeyAlt = 4
    };

    struct KeyChord
    {
        uint8_t base = 0;
        uint8_t modifiers = 0;

        bool operator==(KeyChord other) const
        {
            return base == other.base && modifiers == other.modifiers;
        }
    };

    inline bool validKeyChord(KeyChord chord)
    {
        if (chord.base < KeyLeft || chord.base > KeyZ || chord.modifiers > 7)
            return false;
        // Plain/Shift-only letters are normal text. Unmodified dead accents must
        // stay in the host compositor until a reliable composition fallback exists.
        if (((chord.base >= KeyA && chord.base <= KeyZ) || chord.base == KeyBs1 || chord.base == KeyL3) &&
            !(chord.modifiers & (KeyCtrl | KeyAlt)))
            return false;
        return true;
    }

} // namespace vt
