#pragma once

#include "poligon/core/color.h"
#include "poligon/core/math.h"

namespace poligon {

// One particle as the renderer draws it (Renderer::DrawParticles): a card facing the camera.
// The renderer only draws; moving particles is the game's (poligon/world/particles.h has an
// emitter that does it repeatably).
struct Particle {
    glm::vec3 position{0.0f};
    // Width and height of the card in meters.
    float size = 0.1f;
    // Meters per second; used only to stretch the card along it (ParticleOptions::stretch).
    glm::vec3 velocity{0.0f};
    // Radians, turning the card in its own plane.
    float rotation = 0.0f;
    // Linear color (as a material's) times the texture; alpha for transparency.
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    // Picture of the texture's atlas, counted row by row from the top-left (see
    // ParticleOptions::atlas_columns); the fraction is ignored.
    float frame = 0.0f;
};

// How particles are blended over the scene: Alpha covers by alpha (dust, smoke, leaves,
// rain), Additive adds light (sparks, glowing insects).
enum class ParticleBlend { Alpha, Additive };

// Simple: lit by the sun with its shadows (and the clouds') and by the sky light, without
// normals (a card is lit the same all over). None: drawn in its own colors (glow).
enum class ParticleLighting { Simple, None };

struct ParticleOptions {
    ParticleBlend blend = ParticleBlend::Alpha;
    ParticleLighting lighting = ParticleLighting::Simple;
    // The texture holds atlas_columns x atlas_rows pictures (for animation or variety).
    int atlas_columns = 1;
    int atlas_rows = 1;
    // Above 0: the card is stretched along its velocity on screen to size + speed * stretch
    // meters (rain streaks: about the exposure time, 0.02 s), and is not rotated.
    float stretch = 0.0f;
    // Meters over which a card fades out where it nears the scene behind it, so that it does
    // not show a hard line where it cuts into the ground or a wall (0 = hard).
    float soft_distance = 0.3f;
};

} // namespace poligon
