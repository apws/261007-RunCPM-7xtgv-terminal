#pragma once
#include "7xtgv-palette.h"

namespace vt
{
    // Runtime text/UI palette. Direct xterm256 plot colors are separate.
    struct TextPalette
    {
        mixworx::RGB paper[4], ink[16];
        mixworx::RGB activeLine = {205, 255, 205};
        mixworx::RGB cursor = {0, 150, 40};
        mixworx::RGB gutter = {128, 128, 128};
        mixworx::RGB activeGutter = {205, 255, 205};
        bool activeGutterFollowsLine = true;

        TextPalette()
        {
            for (unsigned i = 0; i < 4; ++i)
                paper[i] = mixworx::Paper[i];
            for (unsigned i = 0; i < 16; ++i)
                ink[i] = mixworx::Ink[i];
        }
    };
} // namespace vt
