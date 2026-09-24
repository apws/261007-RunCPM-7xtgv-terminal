#pragma once

// Compiled configuration, not a claim to detect the physical board's wiring.
inline const char *xtgvFirmwareTarget() {
#if defined(RUNCPM_TARGET_JC8048W550)
    return "jc8048w550";
#elif defined(RUNCPM_TARGET_JC8048W535)
    return "jc8048w535";
#elif defined(RUNCPM_TARGET_RP2350_TOUCH_LCD_2) || !defined(ARDUINO)
    return "rp2350-touch-lcd-2";
#else
    return "unknown"; // an unported target must not authorize an upload
#endif
}
inline const char *xtgvFirmwareBuild() {
#ifdef XTGV_BUILD_STAMP
#define XTGV_ID_STRING_INNER(value) #value
#define XTGV_ID_STRING(value) XTGV_ID_STRING_INNER(value)
    return XTGV_ID_STRING(XTGV_BUILD_STAMP);
#else
    return "unstamped";
#endif
}
