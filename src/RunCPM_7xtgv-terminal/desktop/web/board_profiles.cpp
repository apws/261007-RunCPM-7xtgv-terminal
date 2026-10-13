#include "../../hardware/arduino_7xtgv/board.h"
#include <cstring>
#include <emscripten.h>

namespace
{
    // Constant initialization is required: terminal globals query the default
    // descriptor before main. Selection finishes before the guest worker starts.
    size_t selected = 0;
    bool locked = false;
}

const XtgvWebBoardProfile& xtgvWebBoard()
{
    return xtgvWebBoards::catalog[selected];
}

bool xtgvWebSelectBoard(const char* target)
{
    if (locked || !target)
        return false;
    for (size_t i = 0; i < sizeof(xtgvWebBoards::catalog) / sizeof(xtgvWebBoards::catalog[0]); ++i)
    {
        const auto& p = xtgvWebBoards::catalog[i];
        if (!std::strcmp(target, p.target) || !std::strcmp(target, p.display.id))
        {
            selected = i;
            return true;
        }
    }
    return false;
}

void xtgvWebLockBoard()
{
    locked = true;
}

extern "C" EMSCRIPTEN_KEEPALIVE int web_select_profile(const char* target)
{
    return xtgvWebSelectBoard(target);
}

EM_JS(void, clearProfiles, (), { Module.boardProfiles = []; });
EM_JS(void, publishProfile,
      (const char* target, const char* id, const char* name, int width, int height, int columns, int rows, int zoom,
       const uint8_t* duties, const uint16_t* presets),
      {
          Module.boardProfiles.push({
              target : UTF8ToString(target),
              id : UTF8ToString(id),
              name : UTF8ToString(name),
              width,
              height,
              columns,
              rows,
              zoom,
              duties : Array.from(HEAPU8.subarray(duties, duties + 32)),
              presets : Array.from(HEAPU16.subarray(presets >> 1, (presets >> 1) + 8))
          });
      });

extern "C" EMSCRIPTEN_KEEPALIVE void web_profiles()
{
    clearProfiles();
    for (const auto& p : xtgvWebBoards::catalog)
    {
        const auto& d = p.display;
        publishProfile(p.target, d.id, d.name, d.physicalWidth, d.physicalHeight, d.logicalMaxCols, d.logicalMaxRows,
                       d.defaultZoom, p.duties, &d.zoomGeometry[0].cols);
    }
}
