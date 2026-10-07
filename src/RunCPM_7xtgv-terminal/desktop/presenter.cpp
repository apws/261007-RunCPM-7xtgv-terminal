#include "presenter.h"
#include <SDL.h>
#include <stdexcept>

namespace {
SDL_Window *window = nullptr;
SDL_Renderer *renderer = nullptr;
SDL_Texture *texture = nullptr;
bool redraw = true;
int shownDuty = -1;
void require(bool ok) { if (!ok) throw std::runtime_error(SDL_GetError()); }
}
void xtgvPresenterOpen(int width, int height, const char *title) {
    require(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, "1");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Rect area{};
    float scale = 1.0f;
    if (!SDL_GetDisplayUsableBounds(0, &area))
        scale = std::min(1.0f, std::min(float(area.w - 80) / width, float(area.h - 100) / height));
    scale = std::max(0.1f, scale);
    window = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              int(width * scale), int(height * scale),
                              SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALWAYS_ON_TOP);
    require(window);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    require(renderer);
    require(SDL_RenderSetLogicalSize(renderer, width, height) == 0);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, width, height);
    require(texture);
    SDL_ShowWindow(window);
}
void xtgvPresenterPublish(XtgvDesktopCanvas &canvas) {
    // Copy now: future writes to the drawing Canvas must stay invisible until
    // the next publication. The texture is our last published panel image.
    require(SDL_UpdateTexture(texture, nullptr, canvas.getFramebuffer(), canvas.width() * 2) == 0);
    redraw = true;
}
void xtgvPresenterPoll() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) xtgvDesktopRequestStop();
        if (event.type == SDL_WINDOWEVENT) redraw = true;
        // Deliberately no keyboard translation: input belongs to the mirror.
    }
    const auto duty = xtgvDesktopBacklight();
    if (duty != shownDuty) {
        const auto brightness = xtgvDesktopBrightness(duty);
        require(SDL_SetTextureColorMod(texture, brightness, brightness, brightness) == 0);
        shownDuty = duty; redraw = true;
    }
    if (redraw) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        require(SDL_RenderClear(renderer) == 0);
        require(SDL_RenderCopy(renderer, texture, nullptr, nullptr) == 0);
        SDL_RenderPresent(renderer);
        redraw = false;
    }
}
void xtgvPresenterClose() {
    SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    texture = nullptr; renderer = nullptr; window = nullptr;
    SDL_Quit();
}
