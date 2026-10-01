#define XTGV_HAL_IMPLEMENTATION
#define XTGV_TERMINAL 1
// Build for each HAL target; all four supported backlights are active-high.
#include "../src/terminal/backlight.h"
#include <iostream>
#include <cmath>

static int physical(uint8_t duty) {
    return duty;
}
int main() {
    expectedBacklightPin = XTGV_BACKLIGHT_PIN;
    xtgvBacklightInit(true);
    assert(backlight == physical(0));
    assert(xtgvBacklightGet() == 16 && xtgvBacklightGetRemembered() == 16);
    for (uint8_t level = 1; level <= 31; ++level) {
        xtgvBacklightSet(level);
        assert(backlight == physical(0));
        assert(xtgvBacklightGet() == level && xtgvBacklightGetRemembered() == level);
    }
    xtgvBacklightSet(7);
    xtgvBacklightRelease();
    assert(backlight == physical(xtgvBacklightDuty(7)));
    xtgvBacklightInit(true);
    xtgvBacklightSet(0);
    xtgvBacklightRelease();
    assert(backlight == physical(0) && xtgvBacklightGetRemembered() == 7);
    xtgvBacklightAdjust(1);
    assert(backlight == physical(xtgvBacklightDuty(7)));
    // Existing callers that don't request startup blanking keep their behavior.
    xtgvBacklightInit();
    assert(backlight == physical(xtgvBacklightDuty(7)));
    assert(xtgvBacklightDuty(0) == 0 && xtgvBacklightDuty(31) == 255);
    for (uint8_t level = 1; level <= 31; ++level)
        assert(xtgvBacklightDuty(level) > xtgvBacklightDuty(level - 1));
#if defined(RUNCPM_TARGET_JC8048W550)
    assert(xtgvBacklightDuty(1) == 51 && xtgvBacklightDuty(30) == 253);
#else
#if defined(RUNCPM_TARGET_JC8048W535)
    constexpr int minimum = 1;
#elif defined(RUNCPM_TARGET_RP2350_LCD_1_47_A)
    constexpr int minimum = 17;
#else // RP2_2, including the host-default HAL selection
    constexpr int minimum = 3;
#endif
    assert(xtgvBacklightDuty(1) == minimum);
    for (uint8_t level = 1; level <= 31; ++level) {
        const int shaped = minimum + int(std::lround(
            (255 - minimum) * std::pow(double(level - 1) / 30, 2.2)));
        const int floor = minimum + level - 1;
        assert(xtgvBacklightDuty(level) == (shaped < floor ? floor : shaped));
    }
#endif
    // Ramp uses HAL table entries; logical/remembered target never traverses
    // transient values. Check all targets and timer wraparound.
    for (uint8_t target = 1; target <= 31; ++target) {
        xtgvBacklightSet(target);
        fakeTime = UINT32_MAX - 50;
        xtgvBacklightRelease(310);
        assert(backlight == physical(0));
        for (unsigned elapsed = 0; elapsed <= 310; ++elapsed) {
            xtgvBacklightPoll();
            const uint8_t shown = uint8_t(unsigned(target) * elapsed / 310);
            assert(backlight == physical(xtgvBacklightDuty(shown)));
            assert(xtgvBacklightGet() == target && xtgvBacklightGetRemembered() == target);
            fakeTime++;
        }
        assert(!backlightRamp.active);
    }
    xtgvBacklightRelease(60000);
    fakeTime += 30000;
    xtgvBacklightPoll();
    assert(backlight == physical(xtgvBacklightDuty(15)));
    xtgvBacklightAdjust(-1); // immediate user override, from logical target 31
    assert(!backlightRamp.active && xtgvBacklightGet() == 30);
    assert(backlight == physical(xtgvBacklightDuty(30)));
    fakeTime += 60000;
    xtgvBacklightPoll();
    assert(backlight == physical(xtgvBacklightDuty(30)));
    xtgvBacklightRelease(1000);
    xtgvBacklightSet(0);
    fakeTime += 1000;
    xtgvBacklightPoll();
    assert(backlight == physical(0));
    xtgvBacklightRelease(1000); // target OFF must stay OFF
    assert(!backlightRamp.active && backlight == physical(0));
    xtgvBacklightSet(32); // legacy clamping
    assert(xtgvBacklightGet() == 31);
    xtgvBacklightRelease(1);
    fakeTime++;
    xtgvBacklightPoll();
    assert(!backlightRamp.active && backlight == physical(255));
    std::cout << "Backlight HAL gate/polarity tests passed\n";
}
