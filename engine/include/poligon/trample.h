#pragma once

#include <cstdint>
#include <vector>

#include "poligon/math.h"
#include "poligon/renderer.h"

namespace poligon {

// Where plants have been walked over: a square grid of values 0 (untouched) to 1 (flat)
// over the land around the player. The game writes into it as things walk (Trample), the
// plants slowly stand up again (Recover), and the renderer bends grass and small plants
// standing on trampled ground flat (Renderer::SetTrampleMap): walking the same way again
// and again leaves a path.
//
// The grid follows the player in whole cells (Recenter), so values stay where they were
// on the land; what falls outside is forgotten. Everything is plain arithmetic on the game's
// own ticks, so replays give the same paths.

class TrampleMap : public GroundMapSource {
public:
    // size: meters along each side; resolution: cells along each side.
    explicit TrampleMap(float size = 128.0f, int resolution = 256);

    // Moves the grid so that `center` (world x, z) is near its middle, keeping the values
    // of cells that stay inside.
    void Recenter(const glm::vec2& center);
    // Presses plants within `radius` meters of `position` down by `amount` (more in the
    // middle, softly less to the edge), up to fully flat.
    void Trample(const glm::vec2& position, float radius, float amount);
    // Lets every cell stand up by `amount` (for example 0.002 per tick: a path fades over
    // about a minute and a half at 60 ticks per second once nobody walks it).
    void Recover(float amount);

    // 0..1 at a world position (0 outside the grid).
    float GetValue(const glm::vec2& position) const;
    // The grid for the renderer: resolution x resolution bytes, row by row from the
    // corner with the smallest x and z, which lies at GetOrigin().
    const std::vector<std::uint8_t>& GetBytes() const;
    glm::vec2 GetOrigin() const { return origin_; }
    float GetSize() const { return size_; }
    int GetResolution() const { return resolution_; }
    // Raised by every change, so the renderer uploads only when needed.
    std::uint32_t GetVersion() const { return version_; }
    // The grid as the renderer reads it (Renderer::SetTrampleMap).
    GroundMap GetGroundMap() const override;

private:
    float size_;
    int resolution_;
    float cell_;
    glm::vec2 origin_{0.0f};
    std::vector<float> values_;
    mutable std::vector<std::uint8_t> bytes_;
    mutable std::uint32_t bytes_version_ = 0xFFFFFFFFu;
    std::uint32_t version_ = 0;
};

} // namespace poligon
