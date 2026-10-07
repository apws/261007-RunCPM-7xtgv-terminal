#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Minimum typed Print contract inherited by the real graphics library.
// Extend only when a production caller needs an additional overload.
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t byte) = 0;
    virtual size_t write(const uint8_t *bytes, size_t count) {
        size_t written = 0;
        while (written < count && write(bytes[written]) == 1) ++written;
        return written;
    }
    size_t print(const char *text) {
        return text ? write(reinterpret_cast<const uint8_t *>(text), std::strlen(text)) : 0;
    }
    size_t println(const char *text) { return print(text) + print("\r\n"); }
};
