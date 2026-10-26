#pragma once

// Leaving out what hills hide (occlusion culling by terrain): the horizon of the land as seen
// from the camera, built on the processor from a coarse height map, and a test whether a box
// (a plant cell, a terrain patch) lies wholly below it. Vegetation and Terrain take one
// (SetOcclusion) and draw what it hides as shadow casters only, so shadows do not change.
//
// The horizon is built from heights that are never above the land (LandInfo::occlusion_floor):
// it may be lower than the real one, never higher, so nothing visible is left out. It is
// deterministic and needs nothing back from the graphics card.

#include <vector>

#include "poligon/core/math.h"
#include "poligon/world/height_map.h"

namespace poligon {

class TerrainOcclusion {
public:
    // Directions around the camera, and how far the horizon is followed (meters).
    static constexpr int kColumns = 512;
    static constexpr float kReach = 4096.0f;

    // The heights the horizon is built from (kept by pointer; usually LandInfo::occlusion_floor)
    // and the world position of their first point. nullptr: nothing is ever hidden.
    void SetFloor(const HeightMap* floor, const glm::dvec3& origin);

    // Builds the horizon around `eye` (the camera's position). Cheap to call every frame: it
    // is rebuilt only when the eye has moved by more than 0.25 m.
    void Update(const glm::dvec3& eye);

    // True when the box (world positions) lies wholly below the horizon in every direction it
    // covers, behind land nearer than it. False when there is no horizon yet, or the eye is
    // inside or near the box's footprint.
    bool IsHidden(const glm::dvec3& box_min, const glm::dvec3& box_max) const;

private:
    const HeightMap* floor_ = nullptr;
    glm::dvec3 origin_{0.0};
    glm::dvec3 eye_{0.0};
    bool built_ = false;
    float step_ = 32.0f;  // meters between the samples along each direction
    int steps_ = 0;
    // Per column, per step: the highest slope (rise over distance) of the land seen from the
    // eye up to that step's distance.
    std::vector<float> horizon_;
};

} // namespace poligon
