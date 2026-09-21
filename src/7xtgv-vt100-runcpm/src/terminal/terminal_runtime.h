#pragma once
#include <stdint.h>
void terminalSetup();
void terminalLoop();
void terminalWaitReady();
void terminalConfigBegin();
void terminalConfigFeed(uint8_t byte);
unsigned terminalConfigEnd();
uint8_t terminalConfiguredConsoleBits();
unsigned terminalConfigErrorCount();
uint8_t terminalConfigErrorLineCount();
bool terminalConfigErrorLine(uint8_t index, uint16_t &number, const char *&section, const char *&text);
bool terminalConfigUsesAnsi();
// History lives beside 7XTGVTRM.INI on the CP/M SD card.  Storage itself stays
// board-specific; these functions only parse and serialise the text format.
void terminalHistoryLoadBegin();
void terminalHistoryLoadFeed(uint8_t byte);
void terminalHistoryLoadEnd();
bool terminalHistorySaveDue();
void terminalHistoryForceSave();
typedef bool (*TerminalHistoryWriteByte)(void *context, uint8_t byte);
bool terminalHistoryWrite(TerminalHistoryWriteByte writeByte, void *context);
void terminalHistorySaveComplete(bool saved);
bool terminalHistoryStoragePoll();
bool terminalConfigStoragePoll();
void terminalConfigForceSave();
bool terminalConfigSaveDue(uint8_t &backlightLevel, uint8_t &zoomLevel);
void terminalConfigSaveComplete(bool saved);
void terminalWaitDisplayProfileApplied();
void terminalDiskActivity(bool active);
void terminalScheduleReboot();
void terminalPause(bool paused);
void terminalTextBenchmarkRequest();
bool terminalTextBenchmarkDone();
uint32_t terminalTextBenchmarkElapsedUs();
uint32_t terminalTextBenchmarkBytes();
uint32_t terminalTextBenchmarkChars();
int virtualAvailable();
uint8_t virtualRead();
void virtualWrite(uint8_t c);
