#pragma once

#include <Arduino.h>
#include <stdint.h>

// Levels are intentionally 0..90%, not a full-power 10th step.  Boot keeps
// the long-established duty of 150; logical level 6 is the +/- starting point.
inline uint8_t &xtgvBacklightLevel() {
    static uint8_t level = 6;
    return level;
}

inline void xtgvBacklightInit() {
#if defined(GFX_BL)
    pinMode(GFX_BL, OUTPUT);
#if defined(GFX_BL_INVERTED) && GFX_BL_INVERTED
    analogWrite(GFX_BL, 105);
#else
    analogWrite(GFX_BL, 150);
#endif
#endif
}

inline bool xtgvBacklightSupported() {
#if defined(GFX_BL)
    return true;
#else
    return false;
#endif
}

inline uint8_t xtgvBacklightGet() { return xtgvBacklightLevel(); }

inline void xtgvBacklightSet(uint8_t level) {
    static const uint8_t duty[] = {0, 26, 51, 77, 102, 128, 153, 179, 204, 230};
    if (level > 9) level = 9;
    xtgvBacklightLevel() = level;
#if defined(GFX_BL)
    uint8_t output = duty[level];
#if defined(GFX_BL_INVERTED) && GFX_BL_INVERTED
    output = uint8_t(255 - output);
#endif
    analogWrite(GFX_BL, output);
#endif
}

inline void xtgvBacklightAdjust(int8_t delta) {
    int next = int(xtgvBacklightGet()) + delta;
    xtgvBacklightSet(uint8_t(next < 0 ? 0 : next > 9 ? 9 : next));
}
