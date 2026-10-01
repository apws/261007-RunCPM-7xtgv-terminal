#pragma once

// RP2350 Arduino-Pico split-core RunCPM and terminal runtime.

bool core1_separate_stack = true;
void setup() { terminalSetup(); }
void loop() { terminalLoop(); }
void setup1() { runCPM(); }
void loop1() { delay(1); }
