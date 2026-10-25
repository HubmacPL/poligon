// 2D over the finished picture: the public calls that record rectangles, sprites and text
// (Renderer::DrawRect, DrawSprite, DrawText), fonts, and the overlay pipeline. The quads are
// drawn by passes/overlay_pass.cpp.

#include "../renderer/renderer_internal.h"

#include "poligon_shaders/overlay_frag_dxil.h"
#include "poligon_shaders/overlay_frag_spirv.h"
#include "poligon_shaders/overlay_vert_dxil.h"
#include "poligon_shaders/overlay_vert_spirv.h"

namespace poligon {

namespace {

// Colors are given as they look on screen (sRGB); the shader works in linear light.
float ToLinear(float value) {
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

glm::vec4 ToLinear(Color color) {
    return glm::vec4(ToLinear(color.r), ToLinear(color.g), ToLinear(color.b), color.a);
}

} // namespace

void Renderer::Impl::CreateOverlayPipeline() {
    SDL_GPUShader* vertex = CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0, 1, kOverlayVertDxil, sizeof(kOverlayVertDxil), kOverlayVertSpirv,
                                         sizeof(kOverlayVertSpirv), 1);
    SDL_GPUShader* fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0, kOverlayFragDxil, sizeof(kOverlayFragDxil),
                                           kOverlayFragSpirv, sizeof(kOverlayFragSpirv));
    overlay.pipeline = CreateFullscreenPipeline(vertex, fragment, kOutputFormat, FullscreenBlend::Mix);
    SDL_ReleaseGPUShader(device, vertex);
    SDL_ReleaseGPUShader(device, fragment);
}

void Renderer::Impl::ReleaseOverlay() {
    SDL_ReleaseGPUGraphicsPipeline(device, overlay.pipeline);
    ReleaseDynamicBuffer(device, overlay.buffer);
}

void Renderer::Impl::AddOverlayQuads(SDL_GPUTexture* texture, bool pixelated, const OverlayQuad* quads, size_t count) {
    if (!in_frame) {
        FatalError("Renderer: 2D drawing (DrawRect, DrawSprite, DrawText) needs BeginFrame first");
    }
    if (count == 0) {
        return;
    }
    if (overlay.batches.empty() || overlay.batches.back().texture != texture || overlay.batches.back().pixelated != pixelated) {
        OverlayBatch batch;
        batch.texture = texture;
        batch.pixelated = pixelated;
        batch.first = static_cast<Uint32>(overlay.quads.size());
        overlay.batches.push_back(batch);
    }
    overlay.quads.insert(overlay.quads.end(), quads, quads + count);
    overlay.batches.back().count += static_cast<Uint32>(count);
}

const Font* Renderer::Impl::FindFont(FontHandle handle) const {
    if (handle.id == 0 || handle.id > overlay.fonts.size() || !overlay.fonts[handle.id - 1].alive) {
        return nullptr;
    }
    return &overlay.fonts[handle.id - 1];
}

void Renderer::DrawRect(const Rect& rect, Color color) {
    const OverlayQuad quad{glm::vec4(rect.x, rect.y, rect.width, rect.height), glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), ToLinear(color)};
    impl_->AddOverlayQuads(impl_->textures[impl_->white_texture.id - 1], false, &quad, 1);
}

void Renderer::DrawSprite(TextureHandle texture, const Rect& rect, const Rect& uv, Color color) {
    if (texture.id == 0 || texture.id > impl_->textures.size() || impl_->textures[texture.id - 1] == nullptr) {
        FatalError("Renderer::DrawSprite: invalid or destroyed texture handle");
    }
    const OverlayQuad quad{glm::vec4(rect.x, rect.y, rect.width, rect.height), glm::vec4(uv.x, uv.y, uv.x + uv.width, uv.y + uv.height),
                           ToLinear(color)};
    impl_->AddOverlayQuads(impl_->textures[texture.id - 1], false, &quad, 1);
}

void Renderer::DrawText(FontHandle font_handle, const char* utf8, glm::vec2 position, float size, Color color) {
    const Font* font = impl_->FindFont(font_handle);
    if (font == nullptr) {
        FatalError("Renderer::DrawText: invalid or destroyed font handle");
    }
    std::vector<OverlayQuad> glyphs;
    std::vector<OverlayQuad> boxes;
    LayOutText(*font, utf8, position, size, ToLinear(color), &glyphs, &boxes);
    impl_->AddOverlayQuads(impl_->textures[font->texture.id - 1], font->pixelated, glyphs.data(), glyphs.size());
    impl_->AddOverlayQuads(impl_->textures[impl_->white_texture.id - 1], false, boxes.data(), boxes.size());
}

glm::vec2 Renderer::MeasureText(FontHandle font_handle, const char* utf8, float size) const {
    const Font* font = impl_->FindFont(font_handle);
    if (font == nullptr) {
        FatalError("Renderer::MeasureText: invalid or destroyed font handle");
    }
    return LayOutText(*font, utf8, glm::vec2(0.0f), size, glm::vec4(1.0f), nullptr, nullptr);
}

FontHandle Renderer::GetBuiltinFont() const {
    return FontHandle{1};
}

FontHandle Renderer::CreateTrueTypeFont(const std::uint8_t* data, size_t size, float pixel_height) {
    Image atlas;
    Font font;
    if (!BuildTrueTypeFont(data, size, pixel_height, atlas, font)) {
        return FontHandle{};
    }
    TextureOptions options;
    options.generate_mipmaps = false;
    font.texture = CreateTexture(atlas, options);
    font.alive = true;
    impl_->overlay.fonts.push_back(std::move(font));
    return FontHandle{static_cast<std::uint32_t>(impl_->overlay.fonts.size())};
}

void Renderer::DestroyFont(FontHandle font_handle) {
    if (font_handle.id <= 1 || impl_->FindFont(font_handle) == nullptr) {
        FatalError("Renderer::DestroyFont: invalid or destroyed font handle (the built-in font cannot be destroyed)");
    }
    Font& font = impl_->overlay.fonts[font_handle.id - 1];
    DestroyTexture(font.texture);
    font = Font{};
}

void Renderer::CreateBuiltinFont() {
    Image atlas;
    Font font;
    BuildBuiltinFont(atlas, font);
    TextureOptions options;
    options.generate_mipmaps = false;
    font.texture = CreateTexture(atlas, options);
    font.alive = true;
    impl_->overlay.fonts.push_back(std::move(font));
}

} // namespace poligon
