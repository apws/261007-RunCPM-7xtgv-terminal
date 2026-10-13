// Transport-only fixture: exercise arbitrary guest bytes without terminal parsing.
#include <Arduino.h>
#include "platform.h"
#include <iostream>

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
            return 2;
        std::cout << "LOOPBACK=" << Serial.openLoopback(argv[1]) << std::endl;
        const auto started = millis();
        while (!xtgvDesktopStopping() && uint32_t(millis() - started) < 20000)
        {
            Serial.pump();
            while (Serial.available() && Serial.availableForWrite())
            {
                const auto byte = uint8_t(Serial.read());
                Serial.write(byte);
            }
            delay(1);
        }
        const bool stopped = Serial.transportStopRequested();
        Serial.closePty();
        return stopped ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
