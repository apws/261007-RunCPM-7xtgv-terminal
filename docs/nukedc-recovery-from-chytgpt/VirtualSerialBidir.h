// VirtualSerialBidir.h
// Minimal bidirectional "virtual serial" between core0 and core1 on RP2040/RP2350 (Philhower core)
// - Two lock-free SPSC ring buffers: 0->1 and 1->0
// - Stream-like API: write/read/available/peek/flush
// - Designed so you can swap it in place of Serial (or wrap it behind macros)
//
// Usage pattern (recommended):
//   - Define ONE global link instance in a .cpp:  VtLink g_vtlink;
//   - On core0: VirtualSerial vser0(g_vtlink, VirtualSerial::Side::Core0);
//   - On core1: VirtualSerial vser1(g_vtlink, VirtualSerial::Side::Core1);
//   - core0 writes to vser0 -> core1 reads from vser1, and vice versa.
//
// Notes:
//   - This is NOT bit-banged SPI; it's just byte pipes between cores.
//   - For full duplex, you MUST drain both directions to avoid overflow.
//   - Overflow policy: write() blocks (with yield()) until space is available.
//     If you prefer dropping instead of blocking, see the DROP_* toggles below.

#pragma once
#include <Arduino.h>
#include <stdint.h>

#ifndef VTSER_BUF_SIZE
#define VTSER_BUF_SIZE 2048  // must be power of two
#endif

static_assert((VTSER_BUF_SIZE & (VTSER_BUF_SIZE - 1)) == 0, "VTSER_BUF_SIZE must be power of 2");

// If you want "never block", set these to 1 to drop bytes when full.
// Blocking is safer for correctness, dropping is safer for realtime.
#ifndef VTSER_DROP_ON_FULL
#define VTSER_DROP_ON_FULL 0
#endif

struct VtPipe {
  volatile uint32_t w = 0;
  volatile uint32_t r = 0;
  uint8_t buf[VTSER_BUF_SIZE];
};

static inline uint32_t vtpipe_count(const VtPipe* p) {
  return (uint32_t)(p->w - p->r);
}

static inline uint32_t vtpipe_space(const VtPipe* p) {
  return (uint32_t)(VTSER_BUF_SIZE - vtpipe_count(p));
}

static inline bool vtpipe_push(VtPipe* p, uint8_t b) {
  uint32_t w = p->w;
  uint32_t r = p->r;
  if ((uint32_t)(w - r) >= VTSER_BUF_SIZE) return false;     // full
  p->buf[w & (VTSER_BUF_SIZE - 1)] = b;
  __asm volatile("" ::: "memory");                           // compiler barrier
  p->w = w + 1;
  return true;
}

static inline int vtpipe_pop(VtPipe* p) {
  uint32_t r = p->r;
  uint32_t w = p->w;
  if (r == w) return -1;                                     // empty
  uint8_t b = p->buf[r & (VTSER_BUF_SIZE - 1)];
  __asm volatile("" ::: "memory");
  p->r = r + 1;
  return (int)b;
}

static inline int vtpipe_peek(const VtPipe* p) {
  uint32_t r = p->r;
  uint32_t w = p->w;
  if (r == w) return -1;
  return (int)p->buf[r & (VTSER_BUF_SIZE - 1)];
}

// Full duplex link = two one-way pipes
struct VtLink {
  VtPipe p0to1; // core0 -> core1
  VtPipe p1to0; // core1 -> core0
};

class VirtualSerial : public Stream {
public:
  enum class Side : uint8_t { Core0 = 0, Core1 = 1 };

  VirtualSerial(VtLink& link, Side side)
  : _link(link), _side(side) {}

  // Optional "begin" for compatibility with Serial-like code
  void begin(unsigned long /*baud*/ = 0) {
    // no-op; you can keep Serial.begin(...) calls unchanged if you redirect via macros
  }

  // Stream API
  int available() override {
    return (int)vtpipe_count(rxPipe());
  }

  int peek() override {
    return vtpipe_peek(rxPipe());
  }

  int read() override {
    return vtpipe_pop(rxPipe());
  }

  void flush() override {
    // For a pipe, "flush" usually means "wait until TX drained".
    // We don't have a TX-drained signal unless we busy-wait on txPipe count==0.
    // This can deadlock if the other core isn't draining.
    // So: best-effort no-op by default.
  }

  size_t write(uint8_t b) override {
    if (tryWriteByte(b)) return 1;
#if VTSER_DROP_ON_FULL
    return 0;
#else
    // Block (cooperatively) until space exists
    while (!tryWriteByte(b)) yield();
    return 1;
#endif
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    size_t sent = 0;
    for (size_t i = 0; i < size; i++) {
      if (tryWriteByte(buffer[i])) {
        sent++;
        continue;
      }
#if VTSER_DROP_ON_FULL
      break;
#else
      while (!tryWriteByte(buffer[i])) yield();
      sent++;
#endif
    }
    return sent;
  }

  using Print::write; // bring in other overloads

  // Convenience helpers
  int availableForWrite() {
    return (int)vtpipe_space(txPipe());
  }

  // Clear RX/TX (debug / reset)
  void clear() {
    // Only safe if called when the other side is idle.
    rxPipe()->r = rxPipe()->w;
    txPipe()->r = txPipe()->w;
  }

private:
  VtLink& _link;
  Side _side;

  VtPipe* rxPipe() {
    // core0 receives from p1to0, core1 receives from p0to1
    return (_side == Side::Core0) ? &_link.p1to0 : &_link.p0to1;
  }

  const VtPipe* rxPipe() const {
    return (_side == Side::Core0) ? &_link.p1to0 : &_link.p0to1;
  }

  VtPipe* txPipe() {
    // core0 transmits to p0to1, core1 transmits to p1to0
    return (_side == Side::Core0) ? &_link.p0to1 : &_link.p1to0;
  }

  bool tryWriteByte(uint8_t b) {
    return vtpipe_push(txPipe(), b);
  }
};
