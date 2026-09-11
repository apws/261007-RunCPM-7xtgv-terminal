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
    uint8_t currentLength() const { return length_; }
    uint8_t currentCursor() const { return cursor_; }
    void copyCurrent(char *target, uint8_t &length, uint8_t &cursor) const {
        length = length_; cursor = cursor_; memcpy(target, current_, length_);
    }
    bool currentStartsWithIgnoreCase(const char *prefix) const {
        for (uint8_t i = 0; prefix[i]; ++i) {
            if (i >= length_)
                return false;
            char a = current_[i], b = prefix[i];
            if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
            if (a != b)
                return false;
        }
        return true;
    }
    void left() { if (cursor_) --cursor_; recalling_ = false; offset_ = 0; }
    void right() { if (cursor_ < length_) ++cursor_; recalling_ = false; offset_ = 0; }
    void home() { cursor_ = 0; recalling_ = false; offset_ = 0; }
    void end() { cursor_ = length_; recalling_ = false; offset_ = 0; }
    void eraseAt() {
        if (cursor_ < length_) {
            memmove(current_ + cursor_, current_ + cursor_ + 1, length_ - cursor_ - 1);
            --length_;
        }
        recalling_ = false;
        offset_ = 0;
    }
    void toggleInsert() { insert_ = !insert_; recalling_ = false; offset_ = 0; }
    void replaceCurrent(const char *text, uint8_t length, uint8_t cursor) {
        const uint8_t nextLength = length > MaxLine ? MaxLine : length;
        const bool same = nextLength == length_ && !memcmp(current_, text, nextLength);
        length_ = nextLength;
        memcpy(current_, text, nextLength);
        cursor_ = cursor > length_ ? length_ : cursor;
        if (!same) {
            recalling_ = false;
            offset_ = 0;
        }
    }
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
        } else if ((c == 8 || c == 127) && cursor_) {
            memmove(current_ + cursor_ - 1, current_ + cursor_, length_ - cursor_);
            --cursor_;
            --length_;
            recalling_ = false;
            offset_ = 0;
        } else if (c >= 0x20 && c <= 0x7e && length_ < MaxLine) {
            if (insert_ || cursor_ == length_) {
                memmove(current_ + cursor_ + 1, current_ + cursor_, length_ - cursor_);
                ++length_;
            }
            current_[cursor_++] = char(c);
            recalling_ = false;
            offset_ = 0;
        }
    }

    bool recall(bool previous, Recall &result) {
        result.erase = result.length = 0;
        Bank &bank = banks_[active_];
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
        result.erase = length_;
        offset_ = nextOffset;
        if (!offset_) {
            length_ = 0;
            recalling_ = false;
            return true;
        }
        uint8_t index = uint8_t((bank.next + Depth - offset_) % Depth);
        const Entry &entry = bank.entries[index];
        length_ = result.length = entry.length;
        cursor_ = length_;
        insert_ = true;
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
    uint8_t active_ = 0, length_ = 0, cursor_ = 0, offset_ = 0;
    bool recalling_ = false, insert_ = true;
    char current_[MaxLine];

    void resetLine() {
        length_ = cursor_ = offset_ = 0;
        recalling_ = false;
        insert_ = true;
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
