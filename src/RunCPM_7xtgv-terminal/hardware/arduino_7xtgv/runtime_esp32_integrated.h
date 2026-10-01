#pragma once

// ESP32-S3 RunCPM task plus Arduino terminal/display task.

static TaskHandle_t runCPMTaskHandle = nullptr;
static TaskHandle_t runCPMIdleWindowTaskHandle = nullptr;

static void runCPMTask(void *) {
    runCPM();
    // A missing SD card legitimately makes runCPM() return. Do not leave the
    // watchdog-yield helper holding a deleted FreeRTOS task handle.
    runCPMTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

static void runCPMIdleWindowTask(void *) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(250));
        if (!runCPMTaskHandle) {
            runCPMIdleWindowTaskHandle = nullptr;
            vTaskDelete(nullptr);
        }
        // A one-tick suspension guarantees time for core 0's watched idle
        // task without adding work to the Z80 instruction dispatch path.
        vTaskSuspend(runCPMTaskHandle);
        vTaskDelay(1);
        vTaskResume(runCPMTaskHandle);
    }
}

void setup() {
    static_assert(sizeof(uint8) == 1 && sizeof(uint16) == 2 && sizeof(uint32) == 4,
                  "RunCPM requires 8/16/32-bit integer types");
    terminalSetup();
    // ESP-IDF measures task stack depth in bytes. RunCPM owns this task and
    // SD_MMC; Arduino's loopTask retains terminal/display/USB ownership.
    constexpr uint32_t RunCPMStackBytes = 16 * 1024;
    BaseType_t created = xTaskCreatePinnedToCore(runCPMTask, "RunCPM", RunCPMStackBytes, nullptr,
                                                1, &runCPMTaskHandle, 0);
    if (created != pdPASS) {
        const char *message = "RunCPM task creation failed.\r\n";
        while (*message)
            virtualWrite(*message++);
        terminalBootConfigComplete();
        return;
    }
    created = xTaskCreatePinnedToCore(runCPMIdleWindowTask, "RunCPMIdle", 2048, nullptr, 2,
                                     &runCPMIdleWindowTaskHandle, 0);
    if (created != pdPASS) {
        const char *message = "RunCPM idle-window task creation failed.\r\n";
        while (*message)
            virtualWrite(*message++);
    }
}
void loop() { terminalLoop(); }
