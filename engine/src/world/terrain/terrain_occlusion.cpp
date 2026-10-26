// The horizon of the land around the camera, and boxes hidden below it
// (poligon/world/terrain_occlusion.h). Our own code: there is no library for this shape of
// the problem (a height field seen from one point, tested against boxes on the processor).

#include "poligon/world/terrain_occlusion.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace poligon {

namespace {

constexpr float kTwoPi = 6.28318531f;
// Below any slope the land can have: "no land here".
constexpr float kNoSlope = -1.0e9f;

} // namespace

void TerrainOcclusion::SetFloor(const HeightMap* floor, const glm::dvec3& origin) {
    floor_ = floor != nullptr && !floor->IsEmpty() ? floor : nullptr;
    origin_ = origin;
    built_ = false;
}

void TerrainOcclusion::Update(const glm::dvec3& eye) {
    if (floor_ == nullptr) {
        built_ = false;
        return;
    }
    if (built_ && glm::length(eye - eye_) <= 0.25) {
        return;
    }
    eye_ = eye;
    built_ = true;
    // Half the floor's spacing apart, so no point of it is stepped over.
    step_ = std::max(floor_->spacing * 0.5f, 1.0f);
    steps_ = static_cast<int>(std::ceil(kReach / step_));
    horizon_.assign(static_cast<size_t>(kColumns) * static_cast<size_t>(steps_), kNoSlope);
    // Map coordinates of the eye (the floor counts from its own first point).
    const float eye_x = static_cast<float>(eye.x - origin_.x);
    const float eye_z = static_cast<float>(eye.z - origin_.z);
    const float eye_y = static_cast<float>(eye.y - origin_.y);
    const float size_x = floor_->GetSizeX();
    const float size_z = floor_->GetSizeZ();
    for (int column = 0; column < kColumns; ++column) {
        // Each column is followed along its middle direction.
        const float angle = (static_cast<float>(column) + 0.5f) / kColumns * kTwoPi;
        const float dx = std::cos(angle);
        const float dz = std::sin(angle);
        float highest = kNoSlope;
        float* row = &horizon_[static_cast<size_t>(column) * static_cast<size_t>(steps_)];
        for (int step = 0; step < steps_; ++step) {
            const float distance = static_cast<float>(step + 1) * step_;
            const float x = eye_x + dx * distance;
            const float z = eye_z + dz * distance;
            if (x >= 0.0f && z >= 0.0f && x <= size_x && z <= size_z) {
                highest = std::max(highest, (floor_->GetHeight(x, z) - eye_y) / distance);
            }
            row[step] = highest;
        }
    }
}

bool TerrainOcclusion::IsHidden(const glm::dvec3& box_min, const glm::dvec3& box_max) const {
    if (!built_) {
        return false;
    }
    // The footprint relative to the eye.
    const float min_x = static_cast<float>(box_min.x - eye_.x);
    const float max_x = static_cast<float>(box_max.x - eye_.x);
    const float min_z = static_cast<float>(box_min.z - eye_.z);
    const float max_z = static_cast<float>(box_max.z - eye_.z);
    const float near_x = std::max({min_x, -max_x, 0.0f});
    const float near_z = std::max({min_z, -max_z, 0.0f});
    const float nearest = std::sqrt(near_x * near_x + near_z * near_z);
    // Only land nearer than the box may hide it: steps that end before its nearest point.
    const int last_step = static_cast<int>(std::floor(nearest / step_)) - 2;
    if (last_step < 0) {
        return false;
    }
    const float far_x = std::max(std::fabs(min_x), std::fabs(max_x));
    const float far_z = std::max(std::fabs(min_z), std::fabs(max_z));
    const float farthest = std::sqrt(far_x * far_x + far_z * far_z);
    // The steepest slope from the eye to any point of the box: to its top, at its nearest
    // distance when the top is above the eye and its farthest when below.
    const float rise = static_cast<float>(box_max.y - eye_.y);
    const float slope = rise / (rise > 0.0f ? nearest : farthest);

    // The directions the footprint covers, measured from the direction of its middle (the
    // eye is outside it, so they span less than half a turn).
    const float middle = std::atan2((min_z + max_z) * 0.5f, (min_x + max_x) * 0.5f);
    float low = 0.0f;
    float high = 0.0f;
    for (int corner = 0; corner < 4; ++corner) {
        const float x = (corner & 1) != 0 ? max_x : min_x;
        const float z = (corner & 2) != 0 ? max_z : min_z;
        float turn = std::atan2(z, x) - middle;
        turn = std::remainder(turn, kTwoPi);
        low = std::min(low, turn);
        high = std::max(high, turn);
    }
    const float to_column = kColumns / kTwoPi;
    const int first = static_cast<int>(std::floor((middle + low) * to_column - 0.5f));
    const int last = static_cast<int>(std::ceil((middle + high) * to_column - 0.5f));
    const int step = std::min(last_step, steps_ - 1);
    for (int column = first; column <= last; ++column) {
        const int wrapped = ((column % kColumns) + kColumns) % kColumns;
        if (horizon_[static_cast<size_t>(wrapped) * static_cast<size_t>(steps_) + static_cast<size_t>(step)] <= slope) {
            return false;
        }
    }
    return true;
}

} // namespace poligon
