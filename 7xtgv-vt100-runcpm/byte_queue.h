#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>

// One producer and one consumer. Payload becomes visible before the write index;
// storage is not reused until the consumer publishes its read index.
template <size_t N> class ByteQueue {
    static_assert(N && !(N & (N - 1)), "Queue size must be a power of two");
    static_assert(ATOMIC_INT_LOCK_FREE == 2, "32-bit lock-free atomics required");
    uint8_t bytes_[N] = {};
    std::atomic<uint32_t> read_{0}, write_{0};

  public:
    bool push(uint8_t c) {
        uint32_t w = write_.load(std::memory_order_relaxed);
        if (uint32_t(w - read_.load(std::memory_order_acquire)) == N)
            return false;
        bytes_[w & (N - 1)] = c;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(uint8_t &c) {
        uint32_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire))
            return false;
        c = bytes_[r & (N - 1)];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }
    size_t available() const {
        return uint32_t(write_.load(std::memory_order_acquire) -
                        read_.load(std::memory_order_acquire));
    }
    size_t free() const { return N - available(); }
};
