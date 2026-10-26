#include "poligon/world/trample.h"

#include <algorithm>
#include <cmath>

#include "poligon/core/fatal_error.h"

namespace poligon {

TrampleMap::TrampleMap(float size, int resolution) : size_(size), resolution_(resolution) {
    if (size <= 0.0f || resolution < 2 || resolution > 4096) {
        FatalError("TrampleMap: the size must be above zero and the resolution from 2 to 4096");
    }
    cell_ = size / static_cast<float>(resolution);
    values_.assign(static_cast<size_t>(resolution) * static_cast<size_t>(resolution), 0.0f);
    origin_ = glm::dvec2(-static_cast<double>(size) * 0.5);
}

void TrampleMap::Recenter(const glm::dvec2& center) {
    // The new corner, in whole cells, so values keep their place on the land.
    const glm::dvec2 wanted = center - glm::dvec2(static_cast<double>(size_) * 0.5);
    const double cell = static_cast<double>(cell_);
    const int shift_x = static_cast<int>(std::floor((wanted.x - origin_.x) / cell + 0.5));
    const int shift_z = static_cast<int>(std::floor((wanted.y - origin_.y) / cell + 0.5));
    if (shift_x == 0 && shift_z == 0) {
        return;
    }
    if (remember_) {
        // Cells about to leave the grid are put away.
        for (int z = 0; z < resolution_; ++z) {
            for (int x = 0; x < resolution_; ++x) {
                const float value = values_[static_cast<size_t>(z) * static_cast<size_t>(resolution_) + static_cast<size_t>(x)];
                const int to_x = x - shift_x;
                const int to_z = z - shift_z;
                if (value > 0.0f && (to_x < 0 || to_z < 0 || to_x >= resolution_ || to_z >= resolution_)) {
                    remembered_[{origin_cell_.first + x, origin_cell_.second + z}] = {value, recovered_total_};
                }
            }
        }
    }
    std::vector<float> moved(values_.size(), 0.0f);
    for (int z = 0; z < resolution_; ++z) {
        const int from_z = z + shift_z;
        if (from_z < 0 || from_z >= resolution_) {
            continue;
        }
        for (int x = 0; x < resolution_; ++x) {
            const int from_x = x + shift_x;
            if (from_x >= 0 && from_x < resolution_) {
                moved[static_cast<size_t>(z) * static_cast<size_t>(resolution_) + static_cast<size_t>(x)] =
                    values_[static_cast<size_t>(from_z) * static_cast<size_t>(resolution_) + static_cast<size_t>(from_x)];
            }
        }
    }
    values_ = std::move(moved);
    origin_ += glm::dvec2(static_cast<double>(shift_x), static_cast<double>(shift_z)) * cell;
    origin_cell_.first += shift_x;
    origin_cell_.second += shift_z;
    if (remember_ && !remembered_.empty()) {
        // Cells that came back get their values, less what they recovered meanwhile.
        for (auto it = remembered_.begin(); it != remembered_.end();) {
            const std::int64_t x = it->first.first - origin_cell_.first;
            const std::int64_t z = it->first.second - origin_cell_.second;
            if (x >= 0 && z >= 0 && x < resolution_ && z < resolution_) {
                const float value = it->second.first - static_cast<float>(recovered_total_ - it->second.second);
                values_[static_cast<size_t>(z) * static_cast<size_t>(resolution_) + static_cast<size_t>(x)] = std::max(value, 0.0f);
                it = remembered_.erase(it);
            } else {
                ++it;
            }
        }
    }
    ++version_;
}

void TrampleMap::Trample(const glm::dvec2& position, float radius, float amount) {
    if (radius <= 0.0f || amount <= 0.0f) {
        return;
    }
    const glm::vec2 local((position - origin_) / static_cast<double>(cell_));
    const float reach = radius / cell_;
    const int first_x = std::max(static_cast<int>(std::floor(local.x - reach)), 0);
    const int last_x = std::min(static_cast<int>(std::ceil(local.x + reach)), resolution_ - 1);
    const int first_z = std::max(static_cast<int>(std::floor(local.y - reach)), 0);
    const int last_z = std::min(static_cast<int>(std::ceil(local.y + reach)), resolution_ - 1);
    bool changed = false;
    for (int z = first_z; z <= last_z; ++z) {
        for (int x = first_x; x <= last_x; ++x) {
            const float distance = glm::length(glm::vec2(static_cast<float>(x) + 0.5f, static_cast<float>(z) + 0.5f) - local) / reach;
            if (distance >= 1.0f) {
                continue;
            }
            const float falloff = 1.0f - distance * distance;
            float& value = values_[static_cast<size_t>(z) * static_cast<size_t>(resolution_) + static_cast<size_t>(x)];
            value = std::min(value + amount * falloff, 1.0f);
            changed = true;
        }
    }
    if (changed) {
        ++version_;
    }
}

void TrampleMap::SetRememberOutside(bool remember) {
    remember_ = remember;
    if (!remember) {
        remembered_.clear();
    }
}

void TrampleMap::Recover(float amount) {
    if (amount <= 0.0f) {
        return;
    }
    recovered_total_ += static_cast<double>(amount);
    // Cells put away that have stood up fully meanwhile are forgotten.
    if (!remembered_.empty()) {
        for (auto it = remembered_.begin(); it != remembered_.end();) {
            if (static_cast<double>(it->second.first) <= recovered_total_ - it->second.second) {
                it = remembered_.erase(it);
            } else {
                ++it;
            }
        }
    }
    bool changed = false;
    for (float& value : values_) {
        if (value > 0.0f) {
            value = std::max(value - amount, 0.0f);
            changed = true;
        }
    }
    if (changed) {
        ++version_;
    }
}

float TrampleMap::GetValue(const glm::dvec2& position) const {
    const glm::vec2 local((position - origin_) / static_cast<double>(cell_));
    const int x = static_cast<int>(std::floor(local.x));
    const int z = static_cast<int>(std::floor(local.y));
    if (x < 0 || z < 0 || x >= resolution_ || z >= resolution_) {
        return 0.0f;
    }
    return values_[static_cast<size_t>(z) * static_cast<size_t>(resolution_) + static_cast<size_t>(x)];
}

GroundMap TrampleMap::GetGroundMap() const {
    GroundMap map;
    map.values = GetBytes().data();
    map.resolution = resolution_;
    map.corner = origin_;
    map.size = size_;
    map.version = version_;
    return map;
}

const std::vector<std::uint8_t>& TrampleMap::GetBytes() const {
    if (bytes_version_ != version_) {
        bytes_.resize(values_.size());
        for (size_t i = 0; i < values_.size(); ++i) {
            bytes_[i] = static_cast<std::uint8_t>(std::lround(values_[i] * 255.0f));
        }
        bytes_version_ = version_;
    }
    return bytes_;
}

} // namespace poligon
