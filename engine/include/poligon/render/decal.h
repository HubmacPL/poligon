#pragma once

#include "poligon/core/color.h"
#include "poligon/render/renderer.h"

namespace poligon {

// How a decal looks (Renderer::DrawDecal): a picture pressed onto whatever opaque surfaces lie
// inside its box (hoof prints, blood, bullet holes, puddles), lit like a material there.
struct DecalMaterial {
    // Color with alpha: the decal shows where alpha is above 0. Multiplied by `color`.
    TextureHandle texture;
    // Optional normal map (TextureOptions::is_color = false), in the decal's picture.
    TextureHandle normal_texture;
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    // Wet blood or a puddle: low; dry earth: high.
    float roughness = 0.8f;
    // Multiplies alpha everywhere: fade a decal out over time with it.
    float opacity = 1.0f;
    // Share of the box's half size, at each of its faces, over which the decal fades out, so
    // that it ends softly on curved ground instead of being cut at the box.
    float edge_fade = 0.2f;
    // Surfaces turned further than this from the box's up axis are left out, so that a print
    // on the ground does not stretch up a trunk or a rock standing in its box.
    float max_angle_degrees = 60.0f;
};

} // namespace poligon
