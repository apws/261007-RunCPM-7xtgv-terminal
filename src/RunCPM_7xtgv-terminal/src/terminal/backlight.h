#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "../../hardware/arduino_7xtgv/board.h"

// Private implementation included only by terminal_runtime.cpp (or a unit test).
// Logical brightness/fade is terminal behavior; board headers own physical I/O.

// 32 states: OFF=0, PWM=1..30, full ON=31. Lit minimum and intermediate
// duties are module-specific; a level number is not a brightness percentage.
static constexpr uint8_t XTGV_BACKLIGHT_MAX_LEVEL = 31;
static_assert(sizeof(XTGV_BACKLIGHT_DUTIES) / sizeof(XTGV_BACKLIGHT_DUTIES[0]) == XTGV_BACKLIGHT_MAX_LEVEL + 1,
              "HAL backlight table must have 32 states");

struct XtgvBacklightRamp
{
    uint32_t started = 0;
    uint16_t duration = 0;
    uint8_t shown = 0;
    bool active = false;
};

static uint8_t backlightLevel = 16;
static uint8_t backlightRemembered = 16;
// Startup blanking is independent of the user's logical/remembered level.
static bool backlightHeldOff = false;
static XtgvBacklightRamp backlightRamp;

bool xtgvBacklightHeldOff()
{
    return backlightHeldOff;
}

uint8_t xtgvBacklightDuty(uint8_t level)
{
    // Precomputed module ladder keeps key handling tiny and deterministic.
    // Index zero is hard OFF; 255 is reserved for level 31 (full ON).
    // Minimum/highest PWM and perceptual shaping belong in each module's
    // table, not in the terminal's logical-level ramp.
    return XTGV_BACKLIGHT_DUTIES[level <= XTGV_BACKLIGHT_MAX_LEVEL ? level : XTGV_BACKLIGHT_MAX_LEVEL];
}

void xtgvBacklightApply(uint8_t level)
{
    xtgvBoardBacklightWrite(xtgvBacklightDuty(backlightHeldOff ? 0 : level));
}

void xtgvBacklightInit(bool holdOff = false)
{
    backlightHeldOff = holdOff;
    backlightRamp.active = false;
    xtgvBoardBacklightInit();
    xtgvBacklightApply(backlightLevel);
}

void xtgvBacklightRelease(uint16_t rampMs = 0)
{
    backlightHeldOff = false;
    XtgvBacklightRamp& ramp = backlightRamp;
    ramp.started = millis();
    ramp.duration = rampMs;
    ramp.shown = 0;
    ramp.active = rampMs && backlightLevel;
    xtgvBacklightApply(ramp.active ? 0 : backlightLevel);
}

void xtgvBacklightPoll()
{
    XtgvBacklightRamp& ramp = backlightRamp;
    if (!ramp.active || xtgvBacklightHeldOff())
        return;
    const uint32_t elapsed = uint32_t(millis() - ramp.started);
    const uint8_t target = backlightLevel;
    const uint8_t next = elapsed >= ramp.duration ? target : uint8_t(uint32_t(target) * elapsed / ramp.duration);
    if (next != ramp.shown)
    {
        ramp.shown = next;
        xtgvBacklightApply(next);
    }
    if (elapsed >= ramp.duration)
        ramp.active = false;
}

bool xtgvBacklightSupported()
{
    return true;
}

uint8_t xtgvBacklightGet()
{
    return backlightLevel;
}

uint8_t xtgvBacklightGetRemembered()
{
    return backlightRemembered;
}

void xtgvBacklightSet(uint8_t level)
{
    // Explicit controls cancel a boot ramp, without changing its INI duration.
    backlightRamp.active = false;
    if (level > XTGV_BACKLIGHT_MAX_LEVEL)
        level = XTGV_BACKLIGHT_MAX_LEVEL;
    backlightLevel = level;
    if (level)
        backlightRemembered = level;
    xtgvBacklightApply(level);
}

// First adjustment from off restores the remembered non-off level unchanged.
bool xtgvBacklightAdjust(int8_t delta)
{
    if (!xtgvBacklightGet())
    {
        xtgvBacklightSet(xtgvBacklightGetRemembered());
        return true;
    }
    int next = int(xtgvBacklightGet()) + delta;
    xtgvBacklightSet(uint8_t(next < 1 ? 1 : next > XTGV_BACKLIGHT_MAX_LEVEL ? XTGV_BACKLIGHT_MAX_LEVEL : next));
    return true;
}
