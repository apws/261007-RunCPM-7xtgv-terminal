#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace vt {

class LineHistory {
  public:
    static constexpr uint8_t Banks = 10;
    static constexpr uint8_t Depth = 100;
    static constexpr uint8_t MaxLine = 127;

    struct Recall {
        uint8_t erase = 0;
        uint8_t length = 0;
        char text[MaxLine];
    };

    uint8_t activeBank() const { return active_; }
    uint8_t discardCurrent() {
        uint8_t discarded = length_;
        resetLine();
        return discarded;
    }
    void select(uint8_t bank) {
        if (bank < Banks)
            active_ = bank;
        resetLine();
    }
    void cycle(int direction) {
        select(uint8_t((int(active_) + direction + Banks) % Banks));
    }

    void input(uint8_t c) {
        if (c == 13) {
            if (length_ > 1)
                store();
            resetLine();
        } else if ((c == 8 || c == 127) && length_) {
            --length_;
            recalling_ = false;
            offset_ = 0;
        } else if (c >= 0x20 && c <= 0x7e && length_ < MaxLine) {
            current_[length_++] = char(c);
            recalling_ = false;
            offset_ = 0;
        }
    }

    bool recall(bool previous, Recall &result) {
        result.erase = result.length = 0;
        Bank &bank = banks_[active_];
        if (!recalling_ && length_)
            return false;
        uint8_t nextOffset = offset_;
        if (previous) {
            if (nextOffset >= bank.count)
                return false;
            ++nextOffset;
        } else {
            if (!nextOffset)
                return false;
            --nextOffset;
        }
        result.erase = recalling_ ? length_ : 0;
        offset_ = nextOffset;
        if (!offset_) {
            length_ = 0;
            recalling_ = false;
            return true;
        }
        uint8_t index = uint8_t((bank.next + Depth - offset_) % Depth);
        const Entry &entry = bank.entries[index];
        length_ = result.length = entry.length;
        memcpy(current_, entry.text, length_);
        memcpy(result.text, entry.text, length_);
        recalling_ = true;
        return true;
    }

  private:
    struct Entry {
        uint8_t length = 0;
        char text[MaxLine];
    };
    struct Bank {
        Entry entries[Depth];
        uint8_t next = 0, count = 0;
    };

    Bank banks_[Banks];
    uint8_t active_ = 0, length_ = 0, offset_ = 0;
    bool recalling_ = false;
    char current_[MaxLine];

    void resetLine() {
        length_ = offset_ = 0;
        recalling_ = false;
    }
    void store() {
        Bank &bank = banks_[active_];
        if (bank.count) {
            const Entry &last = bank.entries[(bank.next + Depth - 1) % Depth];
            if (last.length == length_ && !memcmp(last.text, current_, length_))
                return;
        }
        Entry &entry = bank.entries[bank.next];
        entry.length = length_;
        memcpy(entry.text, current_, length_);
        bank.next = uint8_t((bank.next + 1) % Depth);
        if (bank.count < Depth)
            ++bank.count;
    }
};

} // namespace vt
