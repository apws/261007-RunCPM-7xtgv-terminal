#include <Arduino.h>
#include <emscripten.h>
#include "platform.h"

namespace
{
    bool connected = false;
}

EM_JS(int, outputReady, (), { return Module.mirrorReady() ? 1 : 0; });
EM_JS(void, outputBytes, (const uint8_t* bytes, size_t count),
      { Module.mirrorOutput(HEAPU8.slice(bytes, bytes + count)); });
extern "C"
{
    EMSCRIPTEN_KEEPALIVE int web_receive(const uint8_t* bytes, int count)
    {
        if (count < 0)
            return 0;
        return int(Serial.feed(bytes, size_t(count)));
    }

    // Short browser key frames are atomic. Only the UI owner feeds/consumes Serial
    // in this web adapter, so the capacity check and feed cannot interleave.
    EMSCRIPTEN_KEEPALIVE int web_receive_event(const uint8_t* bytes, int count)
    {
        if (count < 1 || count > 64 || Serial.available() + count > 16384)
            return 0;
        return int(Serial.feed(bytes, size_t(count)));
    }

    EMSCRIPTEN_KEEPALIVE void web_connect(int value)
    {
        connected = value != 0;
    }

    EMSCRIPTEN_KEEPALIVE void web_stop()
    {
        xtgvDesktopRequestStop();
    }
}

std::string XtgvDesktopSerial::openPty()
{
    return "browser-websocket";
}

void XtgvDesktopSerial::closePty()
{
    attached_ = connected = false;
    output_.clear();
}

bool XtgvDesktopSerial::transportStopRequested() const
{
    return xtgvDesktopStopping();
}

int XtgvDesktopSerial::availableForWrite() const
{
    return attached_ ? int(65536 - output_.size()) : 0;
}

size_t XtgvDesktopSerial::write(const uint8_t* bytes, size_t count)
{
    const auto accepted = std::min(count, size_t(availableForWrite()));
    output_.insert(output_.end(), bytes, bytes + accepted);
    return accepted;
}

void XtgvDesktopSerial::pump()
{
    attached_ = connected;
    if (!attached_)
    {
        output_.clear();
        return;
    }
    if (!output_.empty() && outputReady())
    {
        uint8_t bytes[4096];
        const size_t count = std::min(output_.size(), sizeof(bytes));
        for (size_t i = 0; i < count; ++i)
        {
            bytes[i] = output_.front();
            output_.pop_front();
        }
        outputBytes(bytes, count);
    }
}
