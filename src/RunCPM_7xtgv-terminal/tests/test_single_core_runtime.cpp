#include <cassert>
#include <cstdint>
#include <string>

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
constexpr int pdPASS = 1;
static int creationResult = pdPASS;
static int setupCalls, loopCalls, guestCalls, deletedCalls, delayTicks, bootReleased;
static void (*guestTask)(void *);
static std::string output;
static int capturedCalls;
void xtgvBoardCaptureBootHeap() {
    assert(setupCalls == capturedCalls);
    ++capturedCalls;
}
void terminalSetup() { assert(capturedCalls == setupCalls + 1); ++setupCalls; }
void terminalLoop() { ++loopCalls; }
void runCPM() { ++guestCalls; }
void virtualWrite(char c) { output += c; }
void terminalBootConfigComplete() { ++bootReleased; }
void vTaskDelete(void *) { ++deletedCalls; }
void vTaskDelay(int ticks) { delayTicks += ticks; }
int xTaskCreate(void (*task)(void *), const char *, uint32_t stack,
                void *, int priority, void *) {
    assert(stack == 16 * 1024 && priority == 1);
    guestTask = task;
    return creationResult;
}
#define RUNCPM_TARGET_ESP32C6_LCD_1_47 1
#include "../hardware/arduino_7xtgv/runtime_esp32_integrated_single.h"

int main() {
    setup();
    assert(setupCalls == 1 && guestTask && output.empty());
    guestTask(nullptr);
    assert(guestCalls == 1 && deletedCalls == 1);
    loop();
    assert(loopCalls == 1 && delayTicks == 1);
    creationResult = 0;
    setup();
    assert(bootReleased == 1 && output == "RunCPM task creation failed.\r\n");
}
