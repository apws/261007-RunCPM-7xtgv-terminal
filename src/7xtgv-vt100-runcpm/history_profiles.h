#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>

namespace vt {

enum AppKey : uint8_t {
    AppLeft, AppRight, AppHome, AppEnd, AppDelete, AppInsert,
    AppUp, AppDown, AppAltUp, AppAltDown,
    AppPageUp, AppPageDown,
    AppCtrlLeft, AppCtrlRight, AppCtrlUp, AppCtrlDown,
    AppCtrlHome, AppCtrlEnd, AppCtrlDelete, AppCtrlInsert,
    AppCtrlPageUp, AppCtrlPageDown,
    AppAltLeft, AppAltRight, AppAltHome, AppAltEnd,
    AppAltDelete, AppAltInsert, AppAltPageUp, AppAltPageDown,
    AppKeyCount
};

enum BindingKind : uint8_t { BindingNone, BindingSend, BindingHistoryPrevious, BindingHistoryNext };

struct KeyBinding {
    static constexpr uint8_t MaxBytes = 16;
    BindingKind kind = BindingNone;
    uint8_t length = 0;
    uint8_t bytes[MaxBytes] = {};
};

struct HistoryProfile {
    static constexpr uint8_t MaxName = 20, MaxSelectBytes = 64;
    char name[MaxName + 1] = {};
    KeyBinding keys[AppKeyCount];
    uint8_t selectLength = 0;
    uint8_t selectBytes[MaxSelectBytes] = {};
};

// A complete staging set is published atomically, so the terminal task never
// observes a half-parsed boot configuration.
class HistoryProfiles {
  public:
    static constexpr uint8_t Banks = 10;
    HistoryProfiles();
    const HistoryProfile &get(uint8_t bank) const;
    void beginLoad();
    void feed(uint8_t byte);
    void finishLoad();
    unsigned errors() const { return errors_; }

  private:
    struct Set { HistoryProfile banks[Banks]; } sets_[2];
    std::atomic<uint8_t> active_{0};
    uint8_t loading_ = 1;
    int section_ = -1;
    unsigned errors_ = 0;
    char line_[192] = {};
    size_t length_ = 0;
    bool overflow_ = false, eof_ = false;
    void defaults(Set &set);
    void parseLine();
};

} // namespace vt
