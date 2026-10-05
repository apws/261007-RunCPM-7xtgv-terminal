#pragma once

// Single application CPU: two priority-1 tasks time-slice. No external
// suspension of a task that could be holding the shared SPI transaction lock.
static void xtgvSingleRunCPMTask(void *) {
    runCPM();
    vTaskDelete(nullptr);
}

void setup() {
    static_assert(sizeof(uint8) == 1 && sizeof(uint16) == 2 && sizeof(uint32) == 4,
                  "RunCPM requires 8/16/32-bit integer types");
    #if defined(RUNCPM_TARGET_ESP32C6_LCD_1_47)
        xtgvBoardCaptureBootHeap();
    #endif
    terminalSetup();
    if (xTaskCreate(xtgvSingleRunCPMTask, "RunCPM", 16 * 1024,
                    nullptr, 1, nullptr) != pdPASS) {
        const char *message = "RunCPM task creation failed.\r\n";
        while (*message) virtualWrite(*message++);
        terminalBootConfigComplete();
    }
}

void loop() {
    terminalLoop();
    // Together with the HAL-owned guest poll, allow the watched idle task
    // to run. One RTOS tick, not a milliseconds-to-ticks zero-delay rounding.
    vTaskDelay(1);
}
