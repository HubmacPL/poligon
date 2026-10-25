#pragma once

// 2D over the finished picture (Renderer::DrawRect, DrawSprite, DrawText): the quads recorded
// this frame, the fonts and their glyph atlases. Recording and fonts: resources/overlay.cpp
// and resources/fonts.cpp; drawing: passes/overlay_pass.cpp.

#include <cstdint>
#include <vector>

#include "dynamic_buffer.h"
#include "poligon/core/image.h"
#include "poligon/core/math.h"
#include "poligon/render/renderer.h"

namespace poligon {
namespace render_internal {

// One quad as the overlay shader reads it (48 bytes; see overlay.vert.hlsl).
struct OverlayQuad {
    glm::vec4 rect;   // x, y, width, height in pixels of the output image
    glm::vec4 uv;     // texture rectangle: left, top, right, bottom
    glm::vec4 color;  // linear color and alpha
};

// Consecutive quads with the same texture, drawn by one call.
struct OverlayBatch {
    SDL_GPUTexture* texture = nullptr;
    bool pixelated = false;  // sampled without smoothing (the built-in font)
    Uint32 first = 0;
    Uint32 count = 0;
};

struct FontGlyph {
    std::uint32_t codepoint = 0;
    // Where the glyph's image lies relative to the pen on the baseline (left, top, right,
    // bottom, in pixels of the size the atlas was made at), and where it is in the atlas.
    glm::vec4 box{0.0f};
    glm::vec4 uv{0.0f};
    float advance = 0.0f;
};

struct Font {
    TextureHandle texture;
    std::vector<FontGlyph> glyphs;  // sorted by code point
    float line_height = 0.0f;       // pixels at the atlas's size
    float ascent = 0.0f;            // from the top of a line to the baseline
    bool pixelated = false;
    bool alive = false;
};

struct OverlayState {
    std::vector<Font> fonts;  // FontHandle::id - 1; the first is the built-in font
    std::vector<OverlayQuad> quads;
    std::vector<OverlayBatch> batches;
    DynamicBuffer buffer;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
};

// The glyph atlas of the built-in font (stb_easy_font: ASCII, 12 pixels a line) and its glyphs.
void BuildBuiltinFont(Image& atlas, Font& font);
// The same from TrueType data at `pixel_height` (ASCII and the Polish letters). False when
// the data is not a usable font.
bool BuildTrueTypeFont(const std::uint8_t* data, size_t size, float pixel_height, Image& atlas, Font& font);
// Lays out UTF-8 text at `size` pixels a line with its top-left corner at `position`: adds
// one quad per glyph to `glyph_quads` (unless null) and the outline of a box for every
// character the font lacks to `box_quads` (unless null). Returns the text's width and height.
glm::vec2 LayOutText(const Font& font, const char* utf8, glm::vec2 position, float size, const glm::vec4& color,
                     std::vector<OverlayQuad>* glyph_quads, std::vector<OverlayQuad>* box_quads);

} // namespace render_internal
} // namespace poligon
