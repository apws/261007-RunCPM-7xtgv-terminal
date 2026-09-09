#pragma once
#include <algorithm>
#include <assert.h>
#include <deque>
#include <stddef.h>
#include <stdint.h>
#include <string>
inline uint32_t fakeTime = 0;
inline uint32_t millis() { return fakeTime; }
inline uint32_t micros() { return fakeTime * 1000U; }
inline void delay(int n) { fakeTime += n; }
constexpr int OUTPUT = 1, HIGH = 1, LOW = 0;
inline int backlight = -1;
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline void analogWrite(int pin, int value) {
    assert(pin == 15);
    backlight = value;
}
struct FakeSerial {
    std::deque<uint8_t> rx;
    std::string tx;
    bool online = false;
    int capacity = 64, chunk = 64;
    void begin(int) {}
    explicit operator bool() const { return online; }
    int available() const { return rx.size(); }
    int read() {
        uint8_t c = rx.front();
        rx.pop_front();
        return c;
    }
    int availableForWrite() const { return capacity; }
    size_t write(const uint8_t *p, size_t n) {
        assert(n <= size_t(capacity));
        n = std::min(n, size_t(chunk));
        tx.append(reinterpret_cast<const char *>(p), n);
        return n;
    }
};
inline FakeSerial Serial;
