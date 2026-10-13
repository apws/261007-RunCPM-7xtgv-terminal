#pragma once

// Serial-only RunCPM runtime; no local TFT terminal.

void setup()
{
    Serial.begin(SERIALSPD);
    while (!Serial)
        delay(10);
    static_assert(sizeof(uint8) == 1 && sizeof(uint16) == 2 && sizeof(uint32) == 4,
                  "RunCPM requires 8/16/32-bit integer types");
    runCPM();
}

void loop()
{
    delay(10);
}
