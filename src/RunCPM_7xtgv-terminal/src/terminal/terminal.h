#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../../hardware/arduino_7xtgv/board.h"
#include "7xtgv-palette.h"
#include "key_chords.h"
#include "text_palette.h"

namespace vt
{
    // Host-only, versioned private keyboard event. Unknown/malformed events must
    // remain ordinary input bytes rather than disappearing from a CP/M program.
    bool decodePrivateKeyEvent(const uint8_t* bytes, size_t length, KeyChord& chord, uint8_t fallback[16],
                               size_t& fallbackLength);
// Backing allocation comes from the selected board, independently of the
// currently visible zoom/pan rectangle.
#if defined(XTGV_WEB)
    constexpr int MaxVisible = xtgvWebBoards::maxVisible(), MaxCols = xtgvWebBoards::maxColumns(),
                  MaxRows = xtgvWebBoards::maxRows(), MaxCells = MaxCols * MaxRows;
#else
    constexpr int MaxVisible = XTGV_TERMINAL_VISIBLE_COLS, MaxCols = XTGV_DISPLAY_DEFAULTS.logicalMaxCols,
                  MaxRows = XTGV_DISPLAY_DEFAULTS.logicalMaxRows, MaxCells = MaxCols * MaxRows;
#endif
    // Compatibility names identify backing allocation only.  Parser and renderer
    // code must use Terminal::cols/rows/visibleCols for runtime geometry.
    constexpr int Visible = MaxVisible, Cols = MaxCols, Rows = MaxRows, Cells = MaxCells;

    // Allocation stride is fixed; profile limits remain those of the selected board.
    inline int columnLimit()
    {
        return XTGV_DISPLAY_DEFAULTS.logicalMaxCols;
    }

    inline int rowLimit()
    {
        return XTGV_DISPLAY_DEFAULTS.logicalMaxRows;
    }

    using RGB = mixworx::RGB;
    extern const RGB (&Ink)[16];
    extern const RGB (&Paper)[4];

    enum Flag : uint8_t
    {
        Underline = 1,
        Reverse = 2,
        Graphics = 4,
        Blink = 8
    };

    class Terminal
    {
    public:
        // Original 7xtgv planes and packed GGPPIIII attributes.
        uint8_t paperRAM[MaxCells], charRAM[MaxCells], attrRAM[MaxCells], flags[MaxCells];
        bool dirty[MaxCells], mirrorDirty[MaxRows];
        int x = 0, y = 0, viewport = 0, rowViewport = 0;
        int cols = MaxCols, rows = MaxRows, visibleCols = MaxVisible, visibleRows = MaxRows, zoom = 1;
        bool ansi = true, cursorVisible = true, applicationCursor = false, applicationKeypad = false;
        bool mirrorCursorDirty = true, indicatorDirty = true, compositionDirty = true;
        unsigned bells = 0;
        using Reply = void (*)(void*, const uint8_t*, size_t);

        // xterm-256 plot samples are terminal syntax, but their RGB565 destination
        // belongs to the display runtime.  This narrow callback keeps the parser
        // usable by direct and buffered Arduino_GFX targets alike.
        enum PlotEvent : uint8_t
        {
            PlotConfigZoom,
            PlotConfigX,
            PlotConfigY,
            PlotBegin,
            PlotSample,
            PlotRow,
            PlotEnd,
            PlotAbort,
            PlotScroll
        };

        using Plot = void (*)(void*, PlotEvent, uint16_t);

        // Private `CSI >68;n v` changes only the TFT view. Guest `>72/73` controls
        // query the display or deliberately select logical geometry.
        enum ControlEvent : uint8_t
        {
            Backlight,
            Zoom,
            FullClear,
            Palette,
            QueryInfo,
            LoadPreset,
            SetGuestGeometry
        };

        using Control = void (*)(void*, ControlEvent, uint8_t, uint16_t, uint16_t);
        explicit Terminal(Reply reply = nullptr, void* context = nullptr);
        void reset();
        // Change only the active terminal surface; backing memory always retains
        // the MaxCols stride.  The overlapping old/new rectangle is preserved.
        bool setGeometry(int activeCols, int activeRows, int visibleColumns, int visibleRowCount, int scale);
        bool setView(int visibleColumns, int visibleRowCount, int scale);

        int index(int column, int row) const
        {
            return row * MaxCols + column;
        }

        int viewportLimit() const
        {
            return cols > visibleCols ? cols - visibleCols : 0;
        }

        int rowViewportLimit() const
        {
            return rows > visibleRows ? rows - visibleRows : 0;
        }

        void feed(uint8_t c);
        void setMode(bool ansiMode);
        void pan(int offset);
        void panRows(int offset);
        void panHorizontal(int direction);
        void panVertical(int direction);
        void panOverview(int direction);
        void invalidate();
        void invalidateMirror();

        void setPlotSink(Plot plot, void* context)
        {
            plot_ = plot;
            plotContext_ = context;
        }

        void setControlSink(Control control, void* context)
        {
            control_ = control;
            controlContext_ = context;
        }

        void beginPlot(uint8_t plotZoom = 1, uint16_t plotX = 0, uint16_t plotY = 0);
        void abortPlot();
        // Forced COM return ends a partial plot while retaining its pixels, and
        // discards any unfinished escape sequence without resetting terminal state.
        void finishTransientOutput();

        bool plotting() const
        {
            return plotState != PlotOff;
        }

        RGB foreground(int i) const;
        RGB background(int i) const;
        TextPalette palette;

        bool graphics(int i) const
        {
            return flags[i] & Graphics;
        }

    private:
        enum State
        {
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
        void* context_;
        Plot plot_ = nullptr;
        void* plotContext_ = nullptr;
        Control control_ = nullptr;
        void* controlContext_ = nullptr;

        enum PlotState : uint8_t
        {
            PlotOff,
            PlotReady,
            PlotSampleArmed
        } plotState = PlotOff;

        uint16_t plotInk = 0, plotPaper = 0xffff;
        bool plotStarted = false;
        bool plotLogicalRow = false;
        int top = 0, bottom = MaxRows - 1, savedX = 0, savedY = 0, addressRow = 0;
        uint8_t ink = 0, paper = 0, rendition = 0, savedInk = 0, savedPaper = 0, savedRendition = 0;
        bool bold = false, savedBold = false, origin = false, autowrap = true, wrapPending = false;
        bool savedOrigin = false, savedWrap = false, savedG0 = false, savedG1 = false, savedUseG1 = false;
        bool g0 = false, g1 = false, useG1 = false, designateG1 = false;
        bool tabs[MaxCols];
        int params[16], count = 0;
        char prefix = 0;
        bool invalid = false;
        void changed(int i);
        void move(int col, int row);
        void blank(int i);
        void erase(int firstRow, int firstCol, int lastRow, int lastCol);
        void eraseDisplay(bool beforeCursor, bool afterCursor);
        void shiftLines(int firstRow, int count, bool insert);
        void scroll(int delta);
        void index(bool reverse = false);
        void put(uint8_t c);
        void beginCSI();
        void dispatch(uint8_t final);
        void dispatchPrivate(uint8_t final);
        void sgr();
        void plot(PlotEvent event, uint16_t value = 0);

        void control(ControlEvent event, uint8_t value, uint16_t cols = 0, uint16_t rows = 0)
        {
            if (control_)
                control_(controlContext_, event, value, cols, rows);
        }

        static uint16_t xterm565(int index);
        void save();
        void restore();
        void send(const char* s);
        int param(int n, int fallback = 1) const;
        void designate(uint8_t c);
    };

    // Incremental PC key decoder. Unknown sequences pass through unchanged.
    class Keyboard
    {
    public:
        using Send = void (*)(void*, const uint8_t*, size_t);
        using KeyEvent = void (*)(void*, KeyChord, const uint8_t*, size_t);

        enum Action : uint8_t
        {
            HistoryPrevious,
            HistoryNext,
            HistoryDelete,
            HistoryBankPrevious,
            HistoryBankNext,
            HistoryBankShow,
            RawToggle,
            RawOn,
            RawOff,
            ProfileExit,
            ProfileClear,
            MirrorSnapshot,
            ProfileBackspace,
            HistoryBank0,
            HistoryBank9 = HistoryBank0 + 9,
            ProfileKey0,
            ProfileKey29 = ProfileKey0 + 29,
            Backlight0,
            Backlight34 = Backlight0 + 34,
            ZoomIn,
            ZoomOut
        };

        using Act = void (*)(void*, Action);

        Keyboard(Terminal& t, Send send, void* context, Act act = nullptr, KeyEvent keyEvent = nullptr)
            : t_(t), send_(send), context_(context), act_(act), keyEvent_(keyEvent)
        {
        }

        void feed(uint8_t c, uint32_t now);
        void tick(uint32_t now);

        void discardPending()
        {
            size_ = 0;
        }

    private:
        Terminal& t_;
        Send send_;
        void* context_;
        Act act_;
        KeyEvent keyEvent_;
        uint8_t buffer_[64];
        size_t size_ = 0;
        uint32_t last_ = 0;
        void finish();
        void flush();
        void output(const char* s);
    };
} // namespace vt
