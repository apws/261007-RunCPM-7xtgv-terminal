#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace vt {

class LineHistory {
  public:
    static constexpr uint8_t Banks = 10;
    // Shared retention limit for every module and the portable .BUF file.
    // Keep SRAM-only boards comfortable; keyword lists have a separate budget.
    static constexpr uint8_t Depth = 100;
    static constexpr uint8_t MaxLine = 127;
    // RunCPM's internal CCP auto-submits at its 125-character BDOS input
    // limit.  A recalled command must stay one character below that limit.
    static constexpr uint8_t MaxCcpRecall = 124;

    struct Recall {
        uint8_t erase = 0;
        uint8_t cursor = 0;
        uint8_t length = 0;
        char text[MaxLine];
    };

    uint8_t activeBank() const { return active_; }
    uint8_t bankCount(uint8_t bank) const { return bank < Banks ? banks_[bank].count : 0; }
    // ordinal zero is the oldest retained line.  These methods are used by
    // the editable SD-card persistence file, not by terminal input.
    bool copyEntry(uint8_t bank, uint8_t ordinal, char *target, uint8_t &length) const {
        if (bank >= Banks || ordinal >= banks_[bank].count)
            return false;
        const Bank &source = banks_[bank];
        uint8_t index = uint8_t((source.next + Depth - source.count + ordinal) % Depth);
        length = source.entries[index].length;
        memcpy(target, source.entries[index].text, length);
        return true;
    }
    bool appendEntry(uint8_t bank, const char *text, uint8_t length) {
        if (bank >= Banks || length <= 1)
            return false;
        const uint8_t limit = bank == 0 ? MaxCcpRecall : MaxLine;
        if (length > limit)
            length = limit;
        Bank &target = banks_[bank];
        Entry &entry = target.entries[target.next];
        entry.length = length;
        memcpy(entry.text, text, length);
        target.next = uint8_t((target.next + 1) % Depth);
        if (target.count < Depth)
            ++target.count;
        return true;
    }
    uint32_t generation() const { return generation_.load(std::memory_order_acquire); }
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
    void left() { if (cursor_) --cursor_; recalling_ = filtering_ = false; offset_ = 0; }
    void right() { if (cursor_ < length_) ++cursor_; recalling_ = filtering_ = false; offset_ = 0; }
    void home() { cursor_ = 0; recalling_ = filtering_ = false; offset_ = 0; }
    void end() { cursor_ = length_; recalling_ = filtering_ = false; offset_ = 0; }
    void eraseAt() {
        if (cursor_ < length_) {
            memmove(current_ + cursor_, current_ + cursor_ + 1, length_ - cursor_ - 1);
            --length_;
        }
        recalling_ = filtering_ = false;
        offset_ = 0;
    }
    void toggleInsert() { insert_ = !insert_; recalling_ = filtering_ = false; offset_ = 0; }
    void replaceCurrent(const char *text, uint8_t length, uint8_t cursor) {
        const uint8_t nextLength = length > MaxLine ? MaxLine : length;
        const bool same = nextLength == length_ && !memcmp(current_, text, nextLength);
        length_ = nextLength;
        memcpy(current_, text, nextLength);
        cursor_ = cursor > length_ ? length_ : cursor;
        if (!same) {
            recalling_ = filtering_ = false;
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
            // A nested filtered completion is part of a program/source line,
            // not an immediate command. An unchanged recall at column zero
            // also need not duplicate its existing history entry.
            if (length_ > 1 && !suppressFilteredComposite_ && !filteredRecallUnchanged_)
                store();
            resetLine();
        } else if ((c == 8 || c == 127) && cursor_) {
            memmove(current_ + cursor_ - 1, current_ + cursor_, length_ - cursor_);
            --cursor_;
            --length_;
            recalling_ = filtering_ = false;
            filteredRecallUnchanged_ = false;
            offset_ = 0;
        } else if (c >= 0x20 && c <= 0x7e && length_ < MaxLine) {
            if (insert_ || cursor_ == length_) {
                memmove(current_ + cursor_ + 1, current_ + cursor_, length_ - cursor_);
                ++length_;
            }
            current_[cursor_++] = char(c);
            recalling_ = filtering_ = false;
            filteredRecallUnchanged_ = false;
            offset_ = 0;
        }
    }

    bool recall(bool previous, Recall &result) {
        result.erase = result.length = 0;
        Bank &bank = banks_[active_];
        // Filter on the token directly before the cursor, rather than only a
        // whole line at its end. This keeps keyword recall useful after a
        // number/prefix or while editing a larger command.
        if (!offset_ && !filtering_ && cursor_) {
            uint8_t start = cursor_;
            while (start && current_[start - 1] != ' ' && current_[start - 1] != '\t')
                --start;
            queryLength_ = uint8_t(cursor_ - start);
            if (queryLength_) {
                memcpy(query_, current_ + start, queryLength_);
                filterPrefixLength_ = start;
                filterSuffixLength_ = uint8_t(length_ - cursor_);
                memcpy(filterPrefix_, current_, filterPrefixLength_);
                memcpy(filterSuffix_, current_ + cursor_, filterSuffixLength_);
                filtering_ = true;
            }
        }
        uint8_t nextOffset = offset_;
        if (filtering_) {
            // Filtered browsing deliberately wraps in either direction. The
            // original typed token is not a separate stop between matches.
            if (!bank.count)
                return false;
            uint8_t attempts = 0;
            do {
                if (previous)
                    nextOffset = nextOffset >= bank.count ? 1 : uint8_t(nextOffset + 1);
                else
                    nextOffset = nextOffset <= 1 ? bank.count : uint8_t(nextOffset - 1);
                if (matches(bank, nextOffset))
                    break;
            } while (++attempts < bank.count);
            // No match leaves the edited line alone. Falling back to a whole
            // old line would overwrite an in-line keyword insertion attempt.
            if (!matches(bank, nextOffset))
                return false;
        }
        if (!filtering_ && previous) {
            do {
                if (nextOffset >= bank.count) return false;
                ++nextOffset;
            } while (!matches(bank, nextOffset));
        } else if (!filtering_) {
            if (!nextOffset)
                return false;
            do {
                --nextOffset;
            } while (nextOffset && !matches(bank, nextOffset));
        }
        result.erase = length_;
        result.cursor = cursor_;
        offset_ = nextOffset;
        if (!offset_) {
            length_ = result.length = filtering_ ? queryLength_ : 0;
            cursor_ = length_;
            if (filtering_)
                memcpy(current_, query_, length_);
            memcpy(result.text, current_, length_);
            recalling_ = false;
            return true;
        }
        uint8_t index = uint8_t((bank.next + Depth - offset_) % Depth);
        const Entry &entry = bank.entries[index];
        const uint8_t limit = active_ == 0 ? MaxCcpRecall : MaxLine;
        uint8_t entryLength = active_ == 0 && entry.length > MaxCcpRecall ? MaxCcpRecall : entry.length;
        if (filtering_) {
            // Splice the matched history item into the token position. The
            // transport may redraw a whole app line, but the model preserves
            // both sides of this in-line insertion.
            uint8_t composed = filterPrefixLength_;
            const uint8_t available = composed < limit ? uint8_t(limit - composed) : 0;
            if (entryLength > available) entryLength = available;
            memcpy(current_, filterPrefix_, composed);
            memcpy(current_ + composed, entry.text, entryLength);
            composed = uint8_t(composed + entryLength);
            // A completion at the end needs a convenient next-word space.
            // Inside a line, retain an existing separator rather than adding
            // a second one; add one only when the suffix would touch the
            // completed identifier directly.
            const bool suffixHasSeparator = filterSuffixLength_ &&
                                            (filterSuffix_[0] == ' ' || filterSuffix_[0] == '\t');
            if (composed < limit && !suffixHasSeparator)
                current_[composed++] = ' ';
            cursor_ = composed;
            const uint8_t suffixAvailable = composed < limit ? uint8_t(limit - composed) : 0;
            const uint8_t suffixLength = filterSuffixLength_ > suffixAvailable ? suffixAvailable
                                                                                : filterSuffixLength_;
            memcpy(current_ + composed, filterSuffix_, suffixLength);
            length_ = uint8_t(composed + suffixLength);
            suppressFilteredComposite_ = filterPrefixLength_ != 0;
            filteredRecallUnchanged_ = true;
        } else {
            length_ = entryLength;
            memcpy(current_, entry.text, length_);
            cursor_ = length_;
            suppressFilteredComposite_ = filteredRecallUnchanged_ = false;
        }
        result.length = length_;
        insert_ = true;
        memcpy(result.text, current_, length_);
        recalling_ = true;
        return true;
    }

    bool deleteRecalled() {
        if (!recalling_ || !offset_ || offset_ > banks_[active_].count)
            return false;
        Bank &bank = banks_[active_];
        uint8_t index = uint8_t((bank.next + Depth - offset_) % Depth);
        const uint8_t last = uint8_t((bank.next + Depth - 1) % Depth);
        while (index != last) {
            const uint8_t following = uint8_t((index + 1) % Depth);
            bank.entries[index] = bank.entries[following];
            index = following;
        }
        bank.next = last;
        --bank.count;
        // The next Up should visit the entry immediately older than the one
        // just deleted.  After compaction, that entry occupies the deleted
        // item's former recall offset.
        const uint8_t resumeOffset = uint8_t(offset_ - 1);
        const bool resumeFilter = filtering_;
        const uint8_t resumeQueryLength = queryLength_;
        const uint8_t resumePrefixLength = filterPrefixLength_;
        const uint8_t resumeSuffixLength = filterSuffixLength_;
        char resumePrefix[MaxLine], resumeSuffix[MaxLine];
        memcpy(resumePrefix, filterPrefix_, resumePrefixLength);
        memcpy(resumeSuffix, filterSuffix_, resumeSuffixLength);
        resetLine();
        offset_ = resumeOffset;
        filtering_ = resumeFilter;
        queryLength_ = resumeQueryLength;
        filterPrefixLength_ = resumePrefixLength;
        filterSuffixLength_ = resumeSuffixLength;
        memcpy(filterPrefix_, resumePrefix, resumePrefixLength);
        memcpy(filterSuffix_, resumeSuffix, resumeSuffixLength);
        filteredRecallUnchanged_ = false;
        generation_.fetch_add(1, std::memory_order_release);
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
    bool recalling_ = false, insert_ = true, filtering_ = false;
    bool filteredRecallUnchanged_ = false, suppressFilteredComposite_ = false;
    char current_[MaxLine];
    char query_[MaxLine];
    char filterPrefix_[MaxLine], filterSuffix_[MaxLine];
    uint8_t queryLength_ = 0, filterPrefixLength_ = 0, filterSuffixLength_ = 0;

    bool matches(const Bank &bank, uint8_t offset) const {
        if (!filtering_ || !queryLength_) return true;
        const Entry &entry = bank.entries[(bank.next + Depth - offset) % Depth];
        if (entry.length < queryLength_)
            return false;
        for (uint8_t i = 0; i < queryLength_; ++i) {
            char a = entry.text[i], b = query_[i];
            if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    }
    std::atomic<uint32_t> generation_{0};

    void resetLine() {
        length_ = cursor_ = offset_ = 0;
        recalling_ = filtering_ = false;
        filteredRecallUnchanged_ = suppressFilteredComposite_ = false;
        queryLength_ = filterPrefixLength_ = filterSuffixLength_ = 0;
        insert_ = true;
    }
    void store() {
        Bank &bank = banks_[active_];
        const uint8_t storedLength = active_ == 0 && length_ > MaxCcpRecall
                                         ? MaxCcpRecall : length_;
        if (bank.count) {
            const Entry &last = bank.entries[(bank.next + Depth - 1) % Depth];
            if (last.length == storedLength && !memcmp(last.text, current_, storedLength))
                return;
        }
        Entry &entry = bank.entries[bank.next];
        entry.length = storedLength;
        memcpy(entry.text, current_, storedLength);
        bank.next = uint8_t((bank.next + 1) % Depth);
        if (bank.count < Depth)
            ++bank.count;
        generation_.fetch_add(1, std::memory_order_release);
    }
};

} // namespace vt
