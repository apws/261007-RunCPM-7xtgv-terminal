#pragma once
#include <atomic>
#include <thread>

// Native threads preserve the existing UI / RunCPM+filesystem ownership.
// No FreeRTOS, core pinning, simulated SPI or second filesystem owner.
static std::atomic<bool> xtgvEmulatorFinished{false};
static std::thread xtgvEmulatorWorker;
static bool xtgvDesktopSaveOK = true;
static std::string xtgvDesktopWorkerError;
void setup() {
    static_assert(sizeof(uint8) == 1 && sizeof(uint16) == 2 && sizeof(uint32) == 4,
                  "RunCPM requires 8/16/32-bit integer types");
    terminalSetup();
    xtgvEmulatorWorker = std::thread([] {
        xtgvDesktopSetWorker(true);
        try { runCPM(); }
        catch (const XtgvDesktopStop &) {} // only thrown by this owner
        catch (const std::exception &error) { xtgvDesktopWorkerError = error.what(); }
        // Disable cancellation while the owner completes pending persistence.
        xtgvDesktopSetWorker(false);
        terminalPause(false);
        try {
            terminalHistoryForceSave();
            terminalConfigForceSave();
            xtgvDesktopSaveOK = terminalHistoryStoragePoll();
            xtgvDesktopSaveOK = terminalConfigStoragePoll() && xtgvDesktopSaveOK;
            xtgvDesktopSaveOK = SD.closeAll() && xtgvDesktopSaveOK;
        } catch (const std::exception &error) {
            xtgvDesktopWorkerError = error.what();
        }
        terminalPause(false);
        xtgvEmulatorFinished.store(true, std::memory_order_release);
    });
}
void loop() { terminalLoop(); }
static void xtgvEmulatorJoinFinished() {
    if (xtgvEmulatorWorker.joinable()) xtgvEmulatorWorker.join();
}
