// Fonts for 2D text: glyph atlases made from TrueType data (stb_truetype) or for the built-in
// font (stb_easy_font, which needs no file), and laying out UTF-8 text into quads.

#include "../renderer/renderer_internal.h"

// The stb libraries come as headers; this file compiles the parts it uses. Static, so that
// Dear ImGui's own copies of the same code do not clash with them.
#define STBRP_STATIC
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <stb_easy_font.h>

namespace poligon {
namespace render_internal {

namespace {

// The letters a TrueType atlas holds besides ASCII: Polish, and the degree sign.
constexpr int kExtraCodepoints[] = {0x00B0, 0x00D3, 0x00F3, 0x0104, 0x0105, 0x0106, 0x0107, 0x0118, 0x0119, 0x0141,
                                    0x0142, 0x0143, 0x0144, 0x015A, 0x015B, 0x0179, 0x017A, 0x017B, 0x017C};

// White everywhere; the alpha channel holds the glyphs.
Image MakeAtlasImage(int width, int height, const std::vector<std::uint8_t>& coverage) {
    Image image;
    image.width = width;
    image.height = height;
    image.pixels.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 255);
    for (size_t i = 0; i < coverage.size(); ++i) {
        image.pixels[i * 4 + 3] = coverage[i];
    }
    return image;
}

} // namespace

void BuildBuiltinFont(Image& atlas, Font& font) {
    // stb_easy_font draws each character from small rectangles, about 6 x 12 pixels a line.
    // Each printable ASCII character goes into a cell of 16 x 16 pixels, 16 cells a row.
    constexpr int kCell = 16;
    constexpr int kWidth = 16 * kCell;
    constexpr int kHeight = 6 * kCell;
    std::vector<std::uint8_t> coverage(static_cast<size_t>(kWidth) * kHeight, 0);
    char text[2] = {0, 0};
    text[0] = 'A';
    font.line_height = static_cast<float>(stb_easy_font_height(text));
    font.ascent = font.line_height;
    font.pixelated = true;
    font.glyphs.clear();
    std::vector<float> vertices(4 * 4 * 64);
    for (int c = 32; c < 127; ++c) {
        text[0] = static_cast<char>(c);
        const int cell = c - 32;
        const int cell_x = (cell % 16) * kCell;
        const int cell_y = (cell / 16) * kCell;
        const int quads = stb_easy_font_print(0.0f, 0.0f, text, nullptr, vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));
        for (int q = 0; q < quads; ++q) {
            // Four vertices of 16 bytes (x, y, z, color) per quad; corners 0 and 2 are opposite.
            const float* first = &vertices[static_cast<size_t>(q) * 16];
            const int x0 = static_cast<int>(first[0]);
            const int y0 = static_cast<int>(first[1]);
            const int x1 = static_cast<int>(first[8]);
            const int y1 = static_cast<int>(first[9]);
            for (int y = std::max(std::min(y0, y1), 0); y < std::min(std::max(y0, y1), kCell); ++y) {
                for (int x = std::max(std::min(x0, x1), 0); x < std::min(std::max(x0, x1), kCell); ++x) {
                    coverage[static_cast<size_t>(cell_y + y) * kWidth + static_cast<size_t>(cell_x + x)] = 255;
                }
            }
        }
        FontGlyph glyph;
        glyph.codepoint = static_cast<std::uint32_t>(c);
        glyph.advance = static_cast<float>(stb_easy_font_width(text));
        const float width = glyph.advance;
        glyph.box = glm::vec4(0.0f, -font.ascent, width, 0.0f);
        glyph.uv = glm::vec4(static_cast<float>(cell_x) / kWidth, static_cast<float>(cell_y) / kHeight,
                             (static_cast<float>(cell_x) + width) / kWidth, (static_cast<float>(cell_y) + font.line_height) / kHeight);
        font.glyphs.push_back(glyph);
    }
    atlas = MakeAtlasImage(kWidth, kHeight, coverage);
}

bool BuildTrueTypeFont(const std::uint8_t* data, size_t size, float pixel_height, Image& atlas, Font& font) {
    // stb_truetype trusts the data and does not check its bounds: only the file's signature
    // and its table directory are checked here, so fonts must come from files the game ships.
    if (data == nullptr || size < 64 || pixel_height < 4.0f) {
        return false;
    }
    const int offset = stbtt_GetFontOffsetForIndex(data, 0);
    stbtt_fontinfo info;
    if (offset < 0 || static_cast<size_t>(offset) + 12 > size || stbtt_InitFont(&info, data, offset) == 0) {
        return false;
    }
    const float scale = stbtt_ScaleForPixelHeight(&info, pixel_height);
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    font.ascent = static_cast<float>(ascent) * scale;
    font.line_height = static_cast<float>(ascent - descent + line_gap) * scale;
    font.pixelated = false;

    constexpr int kAsciiCount = 127 - 32;
    constexpr int kExtraCount = static_cast<int>(sizeof(kExtraCodepoints) / sizeof(kExtraCodepoints[0]));
    std::vector<stbtt_packedchar> ascii(kAsciiCount);
    std::vector<stbtt_packedchar> extra(kExtraCount);
    int codepoints[kExtraCount];
    std::copy(std::begin(kExtraCodepoints), std::end(kExtraCodepoints), codepoints);
    // Twice the size in each direction smooths small text; the atlas grows until all fit.
    for (int side = 256; side <= 4096; side *= 2) {
        std::vector<std::uint8_t> coverage(static_cast<size_t>(side) * static_cast<size_t>(side), 0);
        stbtt_pack_context context;
        if (stbtt_PackBegin(&context, coverage.data(), side, side, 0, 1, nullptr) == 0) {
            return false;
        }
        stbtt_PackSetOversampling(&context, 2, 2);
        stbtt_pack_range ranges[2]{};
        ranges[0].font_size = pixel_height;
        ranges[0].first_unicode_codepoint_in_range = 32;
        ranges[0].num_chars = kAsciiCount;
        ranges[0].chardata_for_range = ascii.data();
        ranges[1].font_size = pixel_height;
        ranges[1].array_of_unicode_codepoints = codepoints;
        ranges[1].num_chars = kExtraCount;
        ranges[1].chardata_for_range = extra.data();
        const int packed = stbtt_PackFontRanges(&context, data, 0, ranges, 2);
        stbtt_PackEnd(&context);
        if (packed == 0) {
            continue;
        }
        font.glyphs.clear();
        const auto add = [&](std::uint32_t codepoint, const std::vector<stbtt_packedchar>& chars, int index) {
            float x = 0.0f;
            float y = 0.0f;
            stbtt_aligned_quad quad;
            stbtt_GetPackedQuad(chars.data(), side, side, index, &x, &y, &quad, 0);
            if (stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint)) == 0 && codepoint != 32) {
                return;  // the font does not have it: drawn as a box
            }
            FontGlyph glyph;
            glyph.codepoint = codepoint;
            glyph.box = glm::vec4(quad.x0, quad.y0, quad.x1, quad.y1);
            glyph.uv = glm::vec4(quad.s0, quad.t0, quad.s1, quad.t1);
            glyph.advance = x;
            font.glyphs.push_back(glyph);
        };
        for (int i = 0; i < kAsciiCount; ++i) {
            add(static_cast<std::uint32_t>(32 + i), ascii, i);
        }
        for (int i = 0; i < kExtraCount; ++i) {
            add(static_cast<std::uint32_t>(kExtraCodepoints[i]), extra, i);
        }
        std::sort(font.glyphs.begin(), font.glyphs.end(),
                  [](const FontGlyph& a, const FontGlyph& b) { return a.codepoint < b.codepoint; });
        atlas = MakeAtlasImage(side, side, coverage);
        return true;
    }
    return false;
}

glm::vec2 LayOutText(const Font& font, const char* utf8, glm::vec2 position, float size, const glm::vec4& color,
                     std::vector<OverlayQuad>* glyph_quads, std::vector<OverlayQuad>* box_quads) {
    if (utf8 == nullptr || font.line_height <= 0.0f) {
        return glm::vec2(0.0f);
    }
    const float scale = size / font.line_height;
    glm::vec2 pen(position.x, position.y + font.ascent * scale);
    float widest = 0.0f;
    int lines = 1;
    const char* cursor = utf8;
    size_t remaining = std::strlen(utf8);
    while (remaining > 0) {
        const Uint32 codepoint = SDL_StepUTF8(&cursor, &remaining);
        if (codepoint == 0) {
            break;
        }
        if (codepoint == '\n') {
            widest = std::max(widest, pen.x - position.x);
            pen.x = position.x;
            pen.y += font.line_height * scale;
            ++lines;
            continue;
        }
        const auto found = std::lower_bound(font.glyphs.begin(), font.glyphs.end(), codepoint,
                                            [](const FontGlyph& glyph, std::uint32_t wanted) { return glyph.codepoint < wanted; });
        if (found != font.glyphs.end() && found->codepoint == codepoint) {
            const FontGlyph& glyph = *found;
            if (glyph_quads != nullptr && glyph.box.z > glyph.box.x) {
                OverlayQuad quad;
                quad.rect = glm::vec4(pen.x + glyph.box.x * scale, pen.y + glyph.box.y * scale, (glyph.box.z - glyph.box.x) * scale,
                                      (glyph.box.w - glyph.box.y) * scale);
                quad.uv = glyph.uv;
                quad.color = color;
                glyph_quads->push_back(quad);
            }
            pen.x += glyph.advance * scale;
            continue;
        }
        // A character the font lacks: the outline of a box half a line wide.
        const float width = 0.5f * font.line_height * scale;
        const float height = 0.7f * font.ascent * scale;
        const float line = std::max(1.0f, size / 12.0f);
        if (box_quads != nullptr) {
            const float left = pen.x + line;
            const float top = pen.y - height;
            const glm::vec4 sides[4] = {{left, top, width - 2.0f * line, line},
                                        {left, pen.y - line, width - 2.0f * line, line},
                                        {left, top, line, height},
                                        {left + width - 3.0f * line, top, line, height}};
            for (const glm::vec4& side : sides) {
                box_quads->push_back({side, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), color});
            }
        }
        pen.x += width;
    }
    widest = std::max(widest, pen.x - position.x);
    return glm::vec2(widest, static_cast<float>(lines) * font.line_height * scale);
}

} // namespace render_internal
} // namespace poligon
