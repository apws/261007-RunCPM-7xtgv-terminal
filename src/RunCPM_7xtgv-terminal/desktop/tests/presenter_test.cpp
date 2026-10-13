// Real SDL software presentation on a disposable dummy display; no guest/card.
#include "../presenter.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
static Uint32 windowId;

static int watch(void*, SDL_Event* event)
{
    if (event->type == SDL_WINDOWEVENT)
        windowId = event->window.windowID;
    return 1;
}

static void require(bool ok, const char* why)
{
    if (!ok)
    {
        std::fprintf(stderr, "%s: %s\n", why, SDL_GetError());
        std::exit(1);
    }
}

int main()
{
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    XtgvDesktopCanvas canvas(16, 8);
    require(canvas.begin(), "canvas allocation");
    canvas.fillScreen(0x0000);
    canvas.fillRect(0, 0, 1, 1, 0xffff);
    canvas.fillRect(1, 0, 1, 1, 0xf800);
    for (int scale = 1; scale <= 9; ++scale)
    {
        require(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL init");
        windowId = 0;
        SDL_AddEventWatch(watch, nullptr);
        xtgvPresenterOpen(16, 8, "Disposable scale test", scale);
        SDL_Window* window = SDL_GetWindowFromID(windowId);
        require(window != nullptr, "window event identity");
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        require(w == 16 * scale && h == 8 * scale, "scaled client size");
        require(!(SDL_GetWindowFlags(window) & SDL_WINDOW_RESIZABLE), "resizing disabled");
        int minW, minH, maxW, maxH;
        SDL_GetWindowMinimumSize(window, &minW, &minH);
        SDL_GetWindowMaximumSize(window, &maxW, &maxH);
        require(minW == w && maxW == w && minH == h && maxH == h, "fixed size limits");
        SDL_Renderer* renderer = SDL_GetRenderer(window);
        require(SDL_RenderGetIntegerScale(renderer), "integer scale policy");
        xtgvDesktopSetBacklight(255);
        xtgvPresenterPublish(canvas);
        xtgvPresenterPoll();
        // Dummy/software renderer retains its backbuffer after Present.
        std::vector<Uint32> pixels(w * h);
        require(SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), w * 4) == 0,
                "software pixel readback");
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const Uint32 expected = y < scale && x < scale ? 0xffffff : y < scale && x < 2 * scale ? 0xff0000 : 0;
                require((pixels[y * w + x] & 0xffffff) == expected, "exact integer pixel replication");
            }
        xtgvDesktopSetBacklight(0);
        xtgvPresenterPoll();
        require(SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), w * 4) == 0,
                "off pixel readback");
        for (auto pixel : pixels)
            require(!(pixel & 0xffffff), "off must render black");
        SDL_DelEventWatch(watch, nullptr);
        xtgvPresenterClose();
    }
    std::puts("SDL fixed window, scales 1..9, exact pixels and backlight off: PASS");
}
