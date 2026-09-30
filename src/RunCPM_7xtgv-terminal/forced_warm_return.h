#pragma once

#include <atomic>
#include <stdint.h>

// The terminal task may request an abort, but only the RunCPM task may take
// it and change Status. This state is deliberately independent of the Z80
// registers and of the terminal renderer so both cores share one safe point.
class ForcedWarmReturn {
  public:
    enum Phase : uint32_t { Idle, ExternalCom, Requested, Finishing };

    void beginExternal() {
        taken_ = false; // RunCPM task only
        inputCleaned_.store(false, std::memory_order_relaxed);
        phase_.store(ExternalCom, std::memory_order_release);
    }

    bool request() {
        uint32_t expected = ExternalCom;
        return phase_.compare_exchange_strong(expected, Requested,
                                              std::memory_order_acq_rel);
    }

    bool pending() const {
        return phase_.load(std::memory_order_acquire) == Requested;
    }

    bool externalComActive() const {
        return phase_.load(std::memory_order_acquire) == ExternalCom;
    }

    bool suppressInput() const {
        const uint32_t phase = phase_.load(std::memory_order_acquire);
        return phase == Requested || phase == Finishing;
    }

    bool finishing() const {
        return phase_.load(std::memory_order_acquire) == Finishing;
    }

    // Called by the RunCPM task only, between instructions or after a
    // blocking console read wakes. The caller sets STATUS_RESTART.
    bool cpuTakeRequest() {
        if (!pending()) return false;
        taken_ = true;
        return true;
    }

    bool finishExternal() {
        if (taken_) {
            inputCleaned_.store(false, std::memory_order_relaxed);
            phase_.store(Finishing, std::memory_order_release);
        } else {
            phase_.store(Idle, std::memory_order_release);
        }
        return taken_;
    }

    void terminalInputCleaned() {
        inputCleaned_.store(true, std::memory_order_release);
    }

    bool inputCleaned() const {
        return inputCleaned_.load(std::memory_order_acquire);
    }

    void resumeInput() {
        phase_.store(Idle, std::memory_order_release);
    }

  private:
    std::atomic<uint32_t> phase_{Idle};
    std::atomic<bool> inputCleaned_{false};
    bool taken_ = false; // RunCPM task only
};

inline ForcedWarmReturn &forcedWarmReturn() {
    static ForcedWarmReturn state;
    return state;
}
