// The full production runtime, but no device or storage activity.
bool terminalHistoryStoragePoll() { return true; }
bool terminalConfigStoragePoll() { return true; }
#include "../src/terminal/terminal_runtime.cpp"
#include <cassert>

int main() {
    uint8_t level = 0, zoom = 0, palette = 0;
    configSavePending.store(true);
    configChangedAt.store(millis());
    configSaveBacklight.store(17);
    configSaveZoom.store(2);
    configSavePalette.store(3);
    assert(!terminalConfigSaveDue(level, zoom, &palette));
    setRawInputMode(true);
    assert(historyForceSaveRequested.load());
    assert(terminalConfigSaveDue(level, zoom, &palette));
    assert(level == 17 && zoom == 2 && palette == 3);
    terminalConfigSaveComplete(false);
    assert(!terminalConfigSaveDue(level, zoom, &palette)); // normal retry delay
    setRawInputMode(true);
    assert(terminalConfigSaveDue(level, zoom, &palette));
    terminalConfigSaveComplete(true);
    assert(!terminalConfigSaveDue(level, zoom, &palette));
    setRawInputMode(false);
    assert(!configForceSaveRequested.load());
}
