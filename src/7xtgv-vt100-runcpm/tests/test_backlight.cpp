// Build twice: ordinary polarity and -DGFX_BL_INVERTED=1.
#define GFX_BL 15
#include "../hardware/arduino_7xtgv/backlight.h"
#include <iostream>
#include <cmath>

static int physical(uint8_t duty) {
#if defined(GFX_BL_INVERTED) && GFX_BL_INVERTED
    return 255 - duty;
#else
    return duty;
#endif
}
int main() {
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
#if defined(RUNCPM_TARGET_JC8048W535) && !defined(RUNCPM_TARGET_JC8048W550)
    assert(xtgvBacklightDuty(1) == 1);
    for (uint8_t level = 1; level <= 31; ++level) {
        const int shaped = 1 + int(std::lround(254 * std::pow(double(level - 1) / 30, 2.2)));
        assert(xtgvBacklightDuty(level) == (shaped < level ? level : shaped));
    }
#else
    assert(xtgvBacklightDuty(1) == 51 && xtgvBacklightDuty(30) == 253);
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
        assert(!xtgvBacklightRamp().active);
    }
    xtgvBacklightRelease(60000);
    fakeTime += 30000;
    xtgvBacklightPoll();
    assert(backlight == physical(xtgvBacklightDuty(15)));
    xtgvBacklightAdjust(-1); // immediate user override, from logical target 31
    assert(!xtgvBacklightRamp().active && xtgvBacklightGet() == 30);
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
    assert(!xtgvBacklightRamp().active && backlight == physical(0));
    xtgvBacklightSet(32); // legacy clamping
    assert(xtgvBacklightGet() == 31);
    xtgvBacklightRelease(1);
    fakeTime++;
    xtgvBacklightPoll();
    assert(!xtgvBacklightRamp().active && backlight == physical(255));
    std::cout << "Backlight HAL gate/polarity tests passed\n";
}
