#pragma once

#include <Arduino.h>

// The first ESP32 milestone deliberately excludes the TFT terminal.  It proves
// the unchanged RunCPM core, native USB serial and the board's SD_MMC storage
// before adding the second-core terminal task.
#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3)
#define RUNCPM_HEADLESS 1
#endif
