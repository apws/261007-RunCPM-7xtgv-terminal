#include "presenter.h"
#include <SDL.h>
#include <stdexcept>
#include <iostream>

namespace
{
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    bool redraw = true;
    int shownDuty = -1;

    void require(bool ok)
    {
        if (!ok)
            throw std::runtime_error(SDL_GetError());
    }
}

void xtgvPresenterOpen(int width, int height, const char* title, int scale)
{
    if (scale < 1 || scale > 9)
        throw std::runtime_error("Window scale must be 1..9");
    shownDuty = -1;
    redraw = true;
    // Hyprland supplies borders but no title bar. Prefer client decorations;
    // an explicit SDL environment hint still takes precedence.
    if (SDL_getenv("HYPRLAND_INSTANCE_SIGNATURE"))
        SDL_SetHint("SDL_VIDEO_WAYLAND_PREFER_LIBDECOR", "1");
#if defined(_WIN32)
    SDL_SetMainReady(); // The shared application owns main(), not SDL2main.
#endif
    require(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, "1");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    window = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width * scale, height * scale,
                              SDL_WINDOW_HIDDEN | SDL_WINDOW_ALWAYS_ON_TOP);
    require(window);
    SDL_SetWindowMinimumSize(window, width * scale, height * scale);
    SDL_SetWindowMaximumSize(window, width * scale, height * scale);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    require(renderer);
    require(SDL_RenderSetLogicalSize(renderer, width, height) == 0);
    require(SDL_RenderSetIntegerScale(renderer, SDL_TRUE) == 0);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, width, height);
    require(texture);
    SDL_ShowWindow(window);
    SDL_RendererInfo info{};
    require(SDL_GetRendererInfo(renderer, &info) == 0);
    std::cout << "SDL panel: " << width << 'x' << height << " window-scale=" << scale << " fixed=" << width * scale
              << 'x' << height * scale << " renderer=" << info.name << std::endl;
}

void xtgvPresenterPublish(XtgvDesktopCanvas& canvas)
{
    // Copy now: future writes to the drawing Canvas must stay invisible until
    // the next publication. The texture is our last published panel image.
    require(SDL_UpdateTexture(texture, nullptr, canvas.getFramebuffer(), canvas.width() * 2) == 0);
    redraw = true;
}

void xtgvPresenterPoll()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
            xtgvDesktopRequestStop();
        if (event.type == SDL_WINDOWEVENT)
            redraw = true;
        // Deliberately no keyboard translation: input belongs to the mirror.
    }
    const auto duty = xtgvDesktopBacklight();
    if (duty != shownDuty)
    {
        const auto brightness = xtgvDesktopBrightness(duty);
        require(SDL_SetTextureColorMod(texture, brightness, brightness, brightness) == 0);
        std::cout << "SDL backlight: duty=" << unsigned(duty) << " rgb=" << unsigned(brightness) << std::endl;
        shownDuty = duty;
        redraw = true;
    }
    if (redraw)
    {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        require(SDL_RenderClear(renderer) == 0);
        require(SDL_RenderCopy(renderer, texture, nullptr, nullptr) == 0);
        SDL_RenderPresent(renderer);
        redraw = false;
    }
}

void xtgvPresenterClose()
{
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    texture = nullptr;
    renderer = nullptr;
    window = nullptr;
    SDL_Quit();
}
