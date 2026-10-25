#pragma once

#include <functional>
#include <vector>

#include "poligon/core/math.h"
#include "poligon/render/decal.h"
#include "poligon/render/renderer.h"

namespace poligon {

// Prints left behind a walker (the player, an animal) as decals (Renderer::DrawDecal): every
// `stride` meters walked a print is laid, left and right of the path in turn, turned the way
// the walker went; when more than `max_prints` lie, the oldest goes. Part of the game's state
// (call Update in FixedUpdate); the prints are plain transforms, so a replay lays the same ones.
struct FootprintSettings {
    // Meters walked between two prints, and how far each lies to the side of the path.
    float stride = 0.75f;
    float step_width = 0.12f;
    // Width and length of a print in meters, and the height of its decal box (it reaches
    // half of it above and below the ground, so prints follow uneven ground).
    glm::vec2 print_size{0.12f, 0.28f};
    float box_height = 0.4f;
    int max_prints = 50;
};

class FootprintTrail {
public:
    explicit FootprintTrail(const FootprintSettings& settings = FootprintSettings{});

    // `feet`: where the walker stands, on the ground. `takes_prints(x, z)`: whether the ground
    // there keeps a print (bare earth, mud, snow; not grass or rock); null = everywhere.
    // A jump of more than 5 m (a teleport) starts a new trail without a print in between.
    void Update(const glm::vec3& feet, const std::function<bool(float x, float z)>& takes_prints = nullptr);
    // Decal transforms, oldest first.
    const std::vector<glm::mat4>& GetPrints() const;
    void Clear();
    // Draws every print with `material` (its picture lies along the box's -z, the way walked).
    void Draw(Renderer& renderer, const DecalMaterial& material) const;

private:
    FootprintSettings settings_;
    std::vector<glm::mat4> prints_;
    glm::vec3 last_feet_{0.0f};
    bool has_last_ = false;
    float walked_ = 0.0f;
    bool left_ = true;
};

} // namespace poligon
