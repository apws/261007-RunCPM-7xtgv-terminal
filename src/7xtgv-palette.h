#pragma once

#include <stdint.h>

// Shared 7xtgv TEXT-mode palette. Entries are RGB888 so every display
// backend can apply its own framebuffer conversion and gamma handling.
namespace mixworx {
struct RGB {
  uint8_t r, g, b;
};

static constexpr RGB Paper[4] = {
  {255, 255, 255}, {255, 230, 230}, {205, 255, 205}, {230, 230, 255},
};

// Even entries are normal inks; odd entries are their darker/bold companions.
static constexpr RGB Ink[16] = {
    {100, 100, 100}, // normal gray
    {0, 0, 0},       // dark gray / black
    {255, 0, 0},     // normal red
    {231, 37, 37},   // dark red
    {0, 242, 0},     // normal green
    {21, 200, 47},   // dark green
    {0, 0, 255},     // normal blue
    {37, 89, 221},   // dark blue
    {230, 230, 230}, // normal silver
    {215, 215, 215}, // dark silver
    {76, 242, 242},  // normal cyan
    {0, 184, 194},   // dark cyan
    {255, 80, 255},  // normal magenta
    {205, 47, 189},  // dark magenta
    {230, 230, 50},  // normal yellow
    {200, 194, 0},   // dark yellow
};
} // namespace mixworx
