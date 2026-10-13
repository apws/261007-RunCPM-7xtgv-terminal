#pragma once

// Portable services required by real Arduino_GFX. No MCU or fake SPI defines.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <Print.h>
#include <deque>
#include <mutex>
#include <memory>

using byte = uint8_t;
using boolean = bool;
using std::abs;
using std::min;
using std::max;
class __FlashStringHelper;

class String
{
public:
    String() = default;

    String(const char* text) : text_(text ? text : "")
    {
    }

    size_t length() const
    {
        return text_.size();
    }

    const char* c_str() const
    {
        return text_.c_str();
    }

private:
    std::string text_;
};

uint32_t millis();
uint32_t micros();
void delay(uint32_t milliseconds);
void delayMicroseconds(uint32_t microseconds);
void yield();

// Desktop constants/fonts already reside in directly addressable memory.
#define PROGMEM
#define F(text) (reinterpret_cast<const __FlashStringHelper *>(text))
#define PI 3.14159265358979323846

uint8_t xtgvPgmReadByte(const void* address);
uint16_t xtgvPgmReadWord(const void* address);
uint32_t xtgvPgmReadDword(const void* address);
const void* pgm_read_ptr(const void* address);
// Upstream detects these as macros. Keep dword 32-bit even on LP64 hosts.
#define pgm_read_byte xtgvPgmReadByte
#define pgm_read_word xtgvPgmReadWord
#define pgm_read_dword xtgvPgmReadDword

class XtgvDesktopSerial : public Print
{
public:
    void begin(unsigned long)
    {
    }

    explicit operator bool() const
    {
        return attached_;
    }

    int available();
    int read();
    int availableForWrite() const;

    size_t write(uint8_t byte) override
    {
        return write(&byte, 1);
    }

    size_t write(const uint8_t*, size_t count) override;
    size_t feed(const uint8_t* bytes, size_t count);
    std::string openPty();
    std::string openLoopback(const std::string& token);
    void closeLoopback();
    bool transportStopRequested() const;
    void pump();
    void closePty();

private:
    struct Loopback;
    std::shared_ptr<Loopback> loopback_;
    void pumpLoopback();
    std::mutex mutex_;
    std::deque<uint8_t> input_;
    // Only the terminal/UI owner touches transport output and connection state.
    std::deque<uint8_t> output_;
    int master_ = -1;
    bool attached_ = false;
};

extern XtgvDesktopSerial Serial;
