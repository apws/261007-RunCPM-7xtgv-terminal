#pragma once
#include <stdint.h>
void terminalSetup();
void terminalLoop();
void terminalWaitReady();
void terminalConfigBegin();
void terminalConfigFeed(uint8_t byte);
unsigned terminalConfigEnd();
void terminalPause(bool paused);
void terminalTextBenchmarkRequest();
bool terminalTextBenchmarkDone();
uint32_t terminalTextBenchmarkElapsedUs();
uint32_t terminalTextBenchmarkBytes();
uint32_t terminalTextBenchmarkChars();
int virtualAvailable();
uint8_t virtualRead();
void virtualWrite(uint8_t c);
