#pragma once

#include <Arduino.h>
#include <stdint.h>

#if defined(RUNCPM_TARGET_JC8048W550)
#include "jc8048w550_backlight.h"
#elif defined(RUNCPM_TARGET_JC8048W535)
#include "jc8048w535_backlight.h"
#elif defined(RUNCPM_TARGET_RP2350_LCD_1_47_A)
#include "pico_rp2350_lcd_1_47_a_backlight.h"
#else
#include "pico_rp2350_touch_lcd_2_backlight.h"
#endif

// 32 states: OFF=0, PWM=1..30, full ON=31. Lit minimum and intermediate
// duties are module-specific; a level number is not a brightness percentage.
static constexpr uint8_t XTGV_BACKLIGHT_MAX_LEVEL = 31;
static_assert(sizeof(XTGV_BACKLIGHT_DUTIES) / sizeof(XTGV_BACKLIGHT_DUTIES[0]) ==
              XTGV_BACKLIGHT_MAX_LEVEL + 1, "HAL backlight table must have 32 states");

inline uint8_t &xtgvBacklightLevel() {
    static uint8_t level = 16;
    return level;
}
inline uint8_t &xtgvBacklightRemembered() {
    static uint8_t level = 16;
    return level;
}
// Startup blanking is independent of the user's logical/remembered level.
inline bool &xtgvBacklightHeldOff() {
    static bool held = false;
    return held;
}
struct XtgvBacklightRamp {
    uint32_t started = 0;
    uint16_t duration = 0;
    uint8_t shown = 0;
    bool active = false;
};
inline XtgvBacklightRamp &xtgvBacklightRamp() {
    static XtgvBacklightRamp ramp;
    return ramp;
}

inline uint8_t xtgvBacklightDuty(uint8_t level) {
    // Precomputed module ladder keeps key handling tiny and deterministic.
    // Index zero is hard OFF; 255 is reserved for level 31 (full ON).
    // Minimum/highest PWM and perceptual shaping belong in each module's
    // table, not in the terminal's logical-level ramp.
    return XTGV_BACKLIGHT_DUTIES[level <= XTGV_BACKLIGHT_MAX_LEVEL ? level : XTGV_BACKLIGHT_MAX_LEVEL];
}

inline void xtgvBacklightApply(uint8_t level) {
#if defined(GFX_BL)
    uint8_t output = xtgvBacklightDuty(xtgvBacklightHeldOff() ? 0 : level);
#if defined(GFX_BL_INVERTED) && GFX_BL_INVERTED
    output = uint8_t(255 - output);
#endif
    analogWrite(GFX_BL, output);
#else
    (void)level;
#endif
}

inline void xtgvBacklightInit(bool holdOff = false) {
    xtgvBacklightHeldOff() = holdOff;
    xtgvBacklightRamp().active = false;
#if defined(GFX_BL)
    // Set the inactive latch before making the pin an output.
#if defined(GFX_BL_INVERTED) && GFX_BL_INVERTED
    digitalWrite(GFX_BL, HIGH);
#else
    digitalWrite(GFX_BL, LOW);
#endif
    pinMode(GFX_BL, OUTPUT);
#endif
    xtgvBacklightApply(xtgvBacklightLevel());
}
inline void xtgvBacklightRelease(uint16_t rampMs = 0) {
    xtgvBacklightHeldOff() = false;
    XtgvBacklightRamp &ramp = xtgvBacklightRamp();
    ramp.started = millis();
    ramp.duration = rampMs;
    ramp.shown = 0;
    ramp.active = rampMs && xtgvBacklightLevel();
    xtgvBacklightApply(ramp.active ? 0 : xtgvBacklightLevel());
}
inline void xtgvBacklightPoll() {
    XtgvBacklightRamp &ramp = xtgvBacklightRamp();
    if (!ramp.active || xtgvBacklightHeldOff()) return;
    const uint32_t elapsed = uint32_t(millis() - ramp.started);
    const uint8_t target = xtgvBacklightLevel();
    const uint8_t next = elapsed >= ramp.duration
        ? target : uint8_t(uint32_t(target) * elapsed / ramp.duration);
    if (next != ramp.shown) {
        ramp.shown = next;
        xtgvBacklightApply(next);
    }
    if (elapsed >= ramp.duration) ramp.active = false;
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
    // Explicit controls cancel a boot ramp, without changing its INI duration.
    xtgvBacklightRamp().active = false;
    if (level > XTGV_BACKLIGHT_MAX_LEVEL) level = XTGV_BACKLIGHT_MAX_LEVEL;
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
    xtgvBacklightSet(uint8_t(next < 1 ? 1 : next > XTGV_BACKLIGHT_MAX_LEVEL
                                           ? XTGV_BACKLIGHT_MAX_LEVEL : next));
    return true;
}
