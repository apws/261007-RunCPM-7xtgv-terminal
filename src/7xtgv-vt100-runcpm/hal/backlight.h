#pragma once

#include <Arduino.h>
#include <stdint.h>

// Zero is physical off. The 32 live levels start at a deliberately usable
// 20% PWM floor and end at 100%. Gamma-shaped duty values make adjacent low
// levels visibly closer together than a plain linear duty ladder.
static constexpr uint8_t XTGV_BACKLIGHT_LEVELS = 32;

inline uint8_t &xtgvBacklightLevel() {
    static uint8_t level = 16;
    return level;
}
inline uint8_t &xtgvBacklightRemembered() {
    static uint8_t level = 16;
    return level;
}

inline uint8_t xtgvBacklightDuty(uint8_t level) {
    // 20..100% perceptual ladder, precomputed to keep key handling tiny and
    // deterministic on every HAL. Index zero is the separate hard-off state.
    static const uint8_t duty[XTGV_BACKLIGHT_LEVELS + 1] = {
        0, 51, 52, 53, 55, 57, 60, 64, 69, 75, 82, 90, 99, 109, 120, 132,
        145, 158, 172, 186, 199, 210, 220, 229, 237, 241, 245, 248, 250,
        252, 253, 254, 255};
    return duty[level <= XTGV_BACKLIGHT_LEVELS ? level : XTGV_BACKLIGHT_LEVELS];
}

inline void xtgvBacklightApply(uint8_t level) {
#if defined(GFX_BL)
    uint8_t output = xtgvBacklightDuty(level);
#if defined(GFX_BL_INVERTED) && GFX_BL_INVERTED
    output = uint8_t(255 - output);
#endif
    analogWrite(GFX_BL, output);
#else
    (void)level;
#endif
}

inline void xtgvBacklightInit() {
#if defined(GFX_BL)
    pinMode(GFX_BL, OUTPUT);
#endif
    xtgvBacklightApply(xtgvBacklightLevel());
}

inline bool xtgvBacklightSupported() {
#if defined(GFX_BL)
    return true;
#else
    return false;
#endif
}

inline uint8_t xtgvBacklightGet() { return xtgvBacklightLevel(); }
inline uint8_t xtgvBacklightGetRemembered() { return xtgvBacklightRemembered(); }

inline void xtgvBacklightSet(uint8_t level) {
    if (level > XTGV_BACKLIGHT_LEVELS) level = XTGV_BACKLIGHT_LEVELS;
    xtgvBacklightLevel() = level;
    if (level)
        xtgvBacklightRemembered() = level;
    xtgvBacklightApply(level);
}

// First adjustment from off restores the remembered non-off level unchanged.
inline bool xtgvBacklightAdjust(int8_t delta) {
    if (!xtgvBacklightGet()) {
        xtgvBacklightSet(xtgvBacklightGetRemembered());
        return true;
    }
    int next = int(xtgvBacklightGet()) + delta;
    xtgvBacklightSet(uint8_t(next < 1 ? 1 : next > XTGV_BACKLIGHT_LEVELS
                                           ? XTGV_BACKLIGHT_LEVELS : next));
    return true;
}
