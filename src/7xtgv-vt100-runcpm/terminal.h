#pragma once
#include <stddef.h>
#include <stdint.h>
#include "7xtgv-palette.h"

namespace vt {
constexpr int Cols = 80, Rows = 24, Visible = 52, Cells = Cols * Rows;
using RGB = mixworx::RGB;
extern const RGB (&Ink)[16];
extern const RGB (&Paper)[4];
enum Flag : uint8_t { Underline = 1, Reverse = 2, Graphics = 4, Blink = 8 };
class Terminal {
  public:
    // Original 7xtgv planes and packed GGPPIIII attributes.
    uint8_t paperRAM[Cells], charRAM[Cells], attrRAM[Cells], flags[Cells];
    bool dirty[Cells], mirrorDirty[Rows];
    int x = 0, y = 0, viewport = 0;
    bool ansi = true, cursorVisible = true, applicationCursor = false, applicationKeypad = false;
    bool mirrorCursorDirty = true, indicatorDirty = true;
    unsigned bells = 0;
    using Reply = void (*)(void *, const uint8_t *, size_t);
    explicit Terminal(Reply reply = nullptr, void *context = nullptr);
    void reset();
    void feed(uint8_t c);
    void setMode(bool ansiMode);
    void pan(int offset);
    void invalidate();
    RGB foreground(int i) const;
    RGB background(int i) const;
    bool graphics(int i) const { return flags[i] & Graphics; }

  private:
    enum State {
        Ground,
        Escape,
        CSI,
        Charset,
        Hash,
        VTAddressRow,
        VTAddressCol,
        String,
        StringEscape
    } state = Ground;
    Reply reply_;
    void *context_;
    int top = 0, bottom = Rows - 1, savedX = 0, savedY = 0, addressRow = 0;
    uint8_t ink = 1, paper = 0, rendition = 0, savedInk = 1, savedPaper = 0, savedRendition = 0;
    bool bold = false, savedBold = false, origin = false, autowrap = true, wrapPending = false;
    bool savedOrigin = false, savedWrap = false, savedG0 = false, savedG1 = false,
         savedUseG1 = false;
    bool g0 = false, g1 = false, useG1 = false, designateG1 = false;
    bool tabs[Cols];
    int params[16], count = 0;
    char prefix = 0;
    bool invalid = false;
    void changed(int i);
    void move(int col, int row);
    void blank(int i);
    void erase(int first, int last);
    void scroll(int delta);
    void index(bool reverse = false);
    void put(uint8_t c);
    void dispatch(uint8_t final);
    void sgr();
    void save();
    void restore();
    void send(const char *s);
    int param(int n, int fallback = 1) const;
    void designate(uint8_t c);
};

// Incremental PC key decoder. Unknown sequences pass through unchanged.
class Keyboard {
  public:
    using Send = void (*)(void *, const uint8_t *, size_t);
    Keyboard(Terminal &t, Send send, void *context) : t_(t), send_(send), context_(context) {}
    void feed(uint8_t c, uint32_t now);
    void tick(uint32_t now);

  private:
    Terminal &t_;
    Send send_;
    void *context_;
    uint8_t buffer_[64];
    size_t size_ = 0;
    uint32_t last_ = 0;
    void finish();
    void flush();
    void output(const char *s);
};
} // namespace vt
