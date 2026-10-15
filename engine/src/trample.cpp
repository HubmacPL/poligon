#include "poligon/trample.h"

#include <algorithm>
#include <cmath>

#include "poligon/fatal_error.h"

namespace poligon {

TrampleMap::TrampleMap(float size, int resolution) : size_(size), resolution_(resolution) {
    if (size <= 0.0f || resolution < 2 || resolution > 4096) {
        FatalError("TrampleMap: the size must be above zero and the resolution from 2 to 4096");
    }
    cell_ = size / static_cast<float>(resolution);
    values_.assign(static_cast<size_t>(resolution) * static_cast<size_t>(resolution), 0.0f);
    origin_ = glm::vec2(-size * 0.5f);
}

void TrampleMap::Recenter(const glm::vec2& center) {
    // The new corner, in whole cells, so values keep their place on the land.
    const glm::vec2 wanted = center - glm::vec2(size_ * 0.5f);
    const int shift_x = static_cast<int>(std::floor((wanted.x - origin_.x) / cell_ + 0.5f));
    const int shift_z = static_cast<int>(std::floor((wanted.y - origin_.y) / cell_ + 0.5f));
    if (shift_x == 0 && shift_z == 0) {
        return;
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
    origin_ += glm::vec2(static_cast<float>(shift_x), static_cast<float>(shift_z)) * cell_;
    ++version_;
}

void TrampleMap::Trample(const glm::vec2& position, float radius, float amount) {
    if (radius <= 0.0f || amount <= 0.0f) {
        return;
    }
    const glm::vec2 local = (position - origin_) / cell_;
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

void TrampleMap::Recover(float amount) {
    if (amount <= 0.0f) {
        return;
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

float TrampleMap::GetValue(const glm::vec2& position) const {
    const glm::vec2 local = (position - origin_) / cell_;
    const int x = static_cast<int>(std::floor(local.x));
    const int z = static_cast<int>(std::floor(local.y));
    if (x < 0 || z < 0 || x >= resolution_ || z >= resolution_) {
        return 0.0f;
    }
    return values_[static_cast<size_t>(z) * static_cast<size_t>(resolution_) + static_cast<size_t>(x)];
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
