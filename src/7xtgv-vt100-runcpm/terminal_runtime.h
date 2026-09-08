#pragma once
#include <stdint.h>
void terminalSetup();
void terminalLoop();
void terminalWaitReady();
void terminalPause(bool paused);
int virtualAvailable();
uint8_t virtualRead();
void virtualWrite(uint8_t c);
