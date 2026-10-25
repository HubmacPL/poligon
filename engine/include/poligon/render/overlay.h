#pragma once

#include <cstdint>

namespace poligon {

// 2D drawing over the finished picture (HUD, crosshair, markers, subtitles): see
// Renderer::DrawRect, DrawSprite, DrawText. Positions and sizes are in pixels of the output
// image, (0, 0) at its top-left corner, x to the right, y down; colors are given as they
// should look on screen (sRGB), with alpha for transparency.

// A rectangle: its top-left corner and its size. In pixels for positions, in 0..1 texture
// coordinates (top-left (0, 0)) for the part of a texture a sprite shows.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// Identifies a font made by Renderer::CreateFont, or the built-in one
// (Renderer::GetBuiltinFont). An id of 0 means "none".
struct FontHandle {
    std::uint32_t id = 0;
};

} // namespace poligon
