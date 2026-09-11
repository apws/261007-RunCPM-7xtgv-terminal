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

enum BindingKind : uint8_t {
    BindingNone, BindingSend, BindingSendCancel, BindingHistoryPrevious, BindingHistoryNext
};

struct KeyBinding {
    static constexpr uint8_t MaxBytes = 16;
    BindingKind kind = BindingNone;
    uint8_t length = 0;
    uint8_t bytes[MaxBytes] = {};
};

// Input adapters are deliberately profile-selected.  They are not terminal
// protocol modes: a line-oriented local composer will be added only when it
// is fully implemented and explicitly selected.
enum InputAdapter : uint8_t { InputAdapterNone, InputAdapterMbasicNative, InputAdapterLocalCompose };

struct HistoryProfile {
    static constexpr uint8_t MaxName = 20, MaxSelectBytes = 64, MaxCommandBytes = 64,
                             MaxPrefixBytes = 8;
    char name[MaxName + 1] = {};
    KeyBinding keys[AppKeyCount];
    uint8_t selectLength = 0;
    uint8_t selectBytes[MaxSelectBytes] = {};
    uint8_t historyClearLength = 0;
    uint8_t historyClearBytes[KeyBinding::MaxBytes] = {};
    uint8_t linePrefixLength = 0;
    uint8_t linePrefixBytes[MaxPrefixBytes] = {};
    uint8_t activatePrefixLength = 0;
    uint8_t activatePrefixBytes[MaxPrefixBytes] = {};
    uint8_t exitLength = 0, clearLength = 0;
    uint8_t exitBytes[MaxCommandBytes] = {}, clearBytes[MaxCommandBytes] = {};
    uint8_t activeLineRGB[3] = {205, 255, 205};
    uint8_t cursorRGB[3] = {0, 255, 80};
    uint16_t historyRetypeDelayMs = 0;
    // This deliberately narrow adapter is for BASIC-80's modal EDIT command.
    // It is off for every other CP/M application.
    bool screenLine = false, trimLeft = false, escapeCancels = false, activeLine = true,
         commandNewLine = false;
    InputAdapter inputAdapter = InputAdapterNone;
};

enum GlobalKey : uint8_t { GlobalKeyNone, GlobalKeyDualAltEscape, GlobalKeyDualAltDelete };
struct GlobalProfile {
    GlobalKey exitKey = GlobalKeyDualAltEscape;
    GlobalKey clearKey = GlobalKeyDualAltDelete;
};

// A complete staging set is published atomically, so the terminal task never
// observes a half-parsed boot configuration.
class HistoryProfiles {
  public:
    static constexpr uint8_t Banks = 10;
    HistoryProfiles();
    const HistoryProfile &get(uint8_t bank) const;
    const GlobalProfile &global() const;
    void beginLoad();
    void feed(uint8_t byte);
    void finishLoad();
    unsigned errors() const { return errors_; }

  private:
    struct Set { GlobalProfile global; HistoryProfile banks[Banks]; } sets_[2];
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
