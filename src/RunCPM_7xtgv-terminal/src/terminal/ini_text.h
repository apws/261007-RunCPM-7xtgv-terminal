#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace vt {

// One owned text buffer, not another parsed settings registry. Storage and
// debounce policy belong to the caller. A future second INI can use another
// instance. No second full-file copy is needed for an edit.
class IniText {
public:
    static constexpr size_t MaxBytes = 65536;
    using Resize = void *(*)(void *, size_t);
    using Release = void (*)(void *);
    explicit IniText(Resize resize = realloc, Release release = free)
        : resize_(resize), release_(release) {}
    ~IniText() { release_(text_); }
    IniText(const IniText &) = delete;
    IniText &operator=(const IniText &) = delete;

    bool valid() const { return valid_; }
    void invalidate() { valid_ = false; }
    const uint8_t *data() const { return reinterpret_cast<const uint8_t *>(text_); }
    size_t size() const { return length_; } // excludes NUL and CP/M EOF

    // Caller fills exactly bytes bytes, then calls finishLoad. An interrupted
    // read remains invalid and must never be serialized over a good SD file.
    uint8_t *beginLoad(size_t bytes) {
        invalidate();
        if (!reserve(bytes)) return nullptr;
        length_ = bytes;
        return reinterpret_cast<uint8_t *>(text_);
    }
    void finishLoad() {
        const void *eof = memchr(text_, 0x1a, length_);
        if (eof) length_ = static_cast<const char *>(eof) - text_;
        text_[length_] = 0;
        valid_ = true;
    }

    // Preserve unrelated bytes and existing line endings. Replace the first
    // matching key, remove later duplicates (also in repeated sections), and
    // append a section/key if absent. Match the parser's 191-byte line limit.
    bool set(const char *section, const char *key, const char *value) {
        if (!valid_) return false;
        const size_t sl = strlen(section), kl = strlen(key), vl = strlen(value);
        if (sl > 80 || kl > 80 || vl > 80) return false;
        const size_t replacement = kl + 1 + vl;
        bool inSection = false, found = false;
        // Reserve the worst-case addition first: allocation failure cannot
        // leave half of this edit applied. Typical file needs ~256 B headroom.
        if (!reserve(length_ + sl + replacement + 8)) return false;
        for (size_t pos = 0; pos < length_;) {
            size_t end = pos;
            while (end < length_ && text_[end] != '\r' && text_[end] != '\n') ++end;
            size_t next = end;
            if (next < length_ && text_[next++] == '\r' &&
                next < length_ && text_[next] == '\n') ++next;
            const size_t lineLength = end - pos;
            size_t first = pos, last = end;
            while (first < last && space(text_[first])) ++first;
            while (last > first && space(text_[last - 1])) --last;
            if (first < last && text_[first] == '[')
                inSection = lineLength <= 191 && last - first == sl + 2 && text_[last - 1] == ']' &&
                            equal(text_ + first + 1, section, sl);
            size_t afterKey = first + kl;
            bool isKey = inSection && lineLength <= 191 && afterKey <= end &&
                         equal(text_ + first, key, kl);
            while (isKey && afterKey < end && space(text_[afterKey])) ++afterKey;
            isKey = isKey && afterKey < end && text_[afterKey] == '=';
            if (!isKey) { pos = next; continue; }
            if (found) {
                erase(pos, next - pos);
                continue;
            }
            // Keep CR/LF/CRLF (or no final newline) on the first occurrence.
            memmove(text_ + pos + replacement, text_ + end, length_ - end + 1);
            memcpy(text_ + pos, key, kl);
            text_[pos + kl] = '=';
            memcpy(text_ + pos + kl + 1, value, vl);
            length_ = length_ - lineLength + replacement;
            pos += replacement + next - end;
            found = true;
        }
        if (!found) {
            append("\r\n[", 3); append(section, sl); append("]\r\n", 3);
            append(key, kl); append("=", 1); append(value, vl); append("\r\n", 2);
        }
        return true;
    }

    static bool samePath(const char *path, const char *expected) {
        if (path[0] == '.' && path[1] == '/') path += 2;
        if (*path == '/') ++path;
        const size_t n = strlen(expected);
        return strlen(path) == n && equal(path, expected, n);
    }

private:
    static bool space(char c) { return c == ' ' || c == '\t'; }
    static char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : c; }
    static bool equal(const char *a, const char *b, size_t n) {
        for (size_t i = 0; i < n; ++i) if (lower(a[i]) != lower(b[i])) return false;
        return true;
    }
    bool reserve(size_t bytes) {
        if (bytes > MaxBytes) return false;
        if (bytes + 1 <= capacity_) return true;
        size_t capacity = (bytes + 1 + 255) & ~size_t(255);
        if (capacity > MaxBytes + 1) capacity = MaxBytes + 1;
        void *next = resize_(text_, capacity);
        if (!next) return false;
        text_ = static_cast<char *>(next);
        capacity_ = capacity;
        return true;
    }
    void erase(size_t pos, size_t count) {
        memmove(text_ + pos, text_ + pos + count, length_ - pos - count + 1);
        length_ -= count;
    }
    void append(const char *data, size_t size) {
        memcpy(text_ + length_, data, size);
        length_ += size;
        text_[length_] = 0;
    }
    Resize resize_;
    Release release_;
    char *text_ = nullptr;
    size_t length_ = 0, capacity_ = 0;
    bool valid_ = false;
};

} // namespace vt
