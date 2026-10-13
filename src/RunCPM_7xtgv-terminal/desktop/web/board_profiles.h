#pragma once

// Included by board.h after its common value types. Only the existing data
// sections are compiled; XTGV_DESKTOP suppresses all physical board drivers.
namespace xtgvWebBoards
{
    namespace es335
    {
#include "../../hardware/arduino_7xtgv/boards/jc8048w535.h"
    }

    namespace ec6147
    {
#include "../../hardware/arduino_7xtgv/boards/waveshare_esp32c6_lcd_1_47.h"
    }

    namespace ep410
    {
#include "../../hardware/arduino_7xtgv/boards/jc8012p4a1c.h"
    }

    namespace es35
    {
#include "../../hardware/arduino_7xtgv/boards/jc8048w550.h"
    }

    namespace rp2147
    {
#include "../../hardware/arduino_7xtgv/boards/pico_rp2350_lcd_1_47_a.h"
    }

    namespace rp22
    {
#include "../../hardware/arduino_7xtgv/boards/pico_rp2350_touch_lcd_2.h"
    }
}

struct XtgvWebBoardProfile
{
    const char* target;
    vt::DisplayHalDefaults display;
    int visibleColumns, offsetX, offsetY;
    bool needsFlush, fullFrameBudget;
    const uint8_t (&duties)[32];
};

namespace xtgvWebBoards
{
    static constexpr XtgvWebBoardProfile catalog[] = {
    {es335::XTGV_BOARD_TARGET_NAME, es335::XTGV_DISPLAY_DEFAULTS,
      es335::XTGV_TERMINAL_VISIBLE_COLS, es335::XTGV_DISPLAY_OFFSET_X, es335::XTGV_DISPLAY_OFFSET_Y,
      es335::XTGV_DISPLAY_NEEDS_FLUSH, es335::XTGV_DISPLAY_FULL_FRAME_BUDGET,
      es335::XTGV_BACKLIGHT_DUTIES},
    {ec6147::XTGV_BOARD_TARGET_NAME, ec6147::XTGV_DISPLAY_DEFAULTS,
      ec6147::XTGV_TERMINAL_VISIBLE_COLS, ec6147::XTGV_DISPLAY_OFFSET_X, ec6147::XTGV_DISPLAY_OFFSET_Y,
      ec6147::XTGV_DISPLAY_NEEDS_FLUSH, ec6147::XTGV_DISPLAY_FULL_FRAME_BUDGET,
      ec6147::XTGV_BACKLIGHT_DUTIES},
    {ep410::XTGV_BOARD_TARGET_NAME, ep410::XTGV_DISPLAY_DEFAULTS,
      ep410::XTGV_TERMINAL_VISIBLE_COLS, ep410::XTGV_DISPLAY_OFFSET_X, ep410::XTGV_DISPLAY_OFFSET_Y,
      ep410::XTGV_DISPLAY_NEEDS_FLUSH, ep410::XTGV_DISPLAY_FULL_FRAME_BUDGET,
      ep410::XTGV_BACKLIGHT_DUTIES},
    {es35::XTGV_BOARD_TARGET_NAME, es35::XTGV_DISPLAY_DEFAULTS,
      es35::XTGV_TERMINAL_VISIBLE_COLS, es35::XTGV_DISPLAY_OFFSET_X, es35::XTGV_DISPLAY_OFFSET_Y,
      es35::XTGV_DISPLAY_NEEDS_FLUSH, es35::XTGV_DISPLAY_FULL_FRAME_BUDGET,
      es35::XTGV_BACKLIGHT_DUTIES},
    {rp2147::XTGV_BOARD_TARGET_NAME, rp2147::XTGV_DISPLAY_DEFAULTS,
      rp2147::XTGV_TERMINAL_VISIBLE_COLS, rp2147::XTGV_DISPLAY_OFFSET_X, rp2147::XTGV_DISPLAY_OFFSET_Y,
      rp2147::XTGV_DISPLAY_NEEDS_FLUSH, rp2147::XTGV_DISPLAY_FULL_FRAME_BUDGET,
      rp2147::XTGV_BACKLIGHT_DUTIES},
    {rp22::XTGV_BOARD_TARGET_NAME, rp22::XTGV_DISPLAY_DEFAULTS,
      rp22::XTGV_TERMINAL_VISIBLE_COLS, rp22::XTGV_DISPLAY_OFFSET_X, rp22::XTGV_DISPLAY_OFFSET_Y,
      rp22::XTGV_DISPLAY_NEEDS_FLUSH, rp22::XTGV_DISPLAY_FULL_FRAME_BUDGET,
      rp22::XTGV_BACKLIGHT_DUTIES},
};

    static constexpr int maxColumns()
    {
        int result = 0;
        for (const auto& p : catalog)
            if (p.display.logicalMaxCols > result)
                result = p.display.logicalMaxCols;
        return result;
    }

    static constexpr int maxRows()
    {
        int result = 0;
        for (const auto& p : catalog)
            if (p.display.logicalMaxRows > result)
                result = p.display.logicalMaxRows;
        return result;
    }

    static constexpr int maxVisible()
    {
        int result = 0;
        for (const auto& p : catalog)
            if (p.visibleColumns > result)
                result = p.visibleColumns;
        return result;
    }
}

const XtgvWebBoardProfile& xtgvWebBoard();
bool xtgvWebSelectBoard(const char* target);
void xtgvWebLockBoard();
#define XTGV_BOARD_TARGET_NAME (xtgvWebBoard().target)
#define XTGV_DISPLAY_DEFAULTS (xtgvWebBoard().display)
#define XTGV_TERMINAL_VISIBLE_COLS (xtgvWebBoard().visibleColumns)
#define XTGV_TERMINAL_ROWS (xtgvWebBoard().display.logicalMaxRows)
#define XTGV_DISPLAY_OFFSET_X (xtgvWebBoard().offsetX)
#define XTGV_DISPLAY_OFFSET_Y (xtgvWebBoard().offsetY)
#define XTGV_BACKLIGHT_DUTIES (xtgvWebBoard().duties)
