#pragma once
#include <stdint.h>
void terminalSetup();
void terminalLoop();
void terminalWaitReady();
uint16_t terminalLogicalColumns();
// Storage owner calls this even when SD/INI loading failed: show defaults/errors.
void terminalBootConfigComplete();
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
enum TerminalConfigDirty : uint8_t {
    ConfigBacklightDirty = 1, ConfigPaletteDirty = 2, ConfigZoomDirty = 4
};
bool terminalConfigSaveDue(uint8_t &backlightLevel, uint8_t &zoomLevel,
                           uint8_t *paletteIndex = nullptr, uint8_t *dirtyFields = nullptr);
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
// RunCPM task only: discard queued guest keystrokes after a forced COM return.
void terminalDiscardGuestInput();
