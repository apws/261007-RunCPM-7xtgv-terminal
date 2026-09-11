#pragma once
#include <stdint.h>
void terminalSetup();
void terminalLoop();
void terminalWaitReady();
void terminalConfigBegin();
void terminalConfigFeed(uint8_t byte);
unsigned terminalConfigEnd();
// History lives beside 7XTGVTRM.INI on the CP/M SD card.  Storage itself stays
// board-specific; these functions only parse and serialise the text format.
void terminalHistoryLoadBegin();
void terminalHistoryLoadFeed(uint8_t byte);
void terminalHistoryLoadEnd();
bool terminalHistorySaveDue();
typedef bool (*TerminalHistoryWriteByte)(void *context, uint8_t byte);
bool terminalHistoryWrite(TerminalHistoryWriteByte writeByte, void *context);
void terminalHistorySaveComplete(bool saved);
void terminalHistoryStoragePoll();
void terminalPause(bool paused);
void terminalTextBenchmarkRequest();
bool terminalTextBenchmarkDone();
uint32_t terminalTextBenchmarkElapsedUs();
uint32_t terminalTextBenchmarkBytes();
uint32_t terminalTextBenchmarkChars();
int virtualAvailable();
uint8_t virtualRead();
void virtualWrite(uint8_t c);
