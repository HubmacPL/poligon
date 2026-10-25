#include "poligon/world/footprints.h"

#include <cmath>

namespace poligon {

FootprintTrail::FootprintTrail(const FootprintSettings& settings) : settings_(settings) {}

void FootprintTrail::Update(const glm::dvec3& feet, const std::function<bool(double x, double z)>& takes_prints) {
    if (!has_last_ || glm::distance(feet, last_feet_) > 5.0f) {
        last_feet_ = feet;
        has_last_ = true;
        walked_ = 0.0f;
        return;
    }
    const glm::vec2 step(glm::dvec2(feet.x - last_feet_.x, feet.z - last_feet_.z));
    const float length = glm::length(step);
    if (length < 1e-5f) {
        return;
    }
    walked_ += length;
    if (walked_ >= settings_.stride) {
        walked_ -= settings_.stride;
        const glm::vec2 forward = step / length;
        const glm::vec2 side(forward.y, -forward.x);  // to the left of the way walked
        const float offset = (left_ ? 1.0f : -1.0f) * settings_.step_width;
        const glm::dvec3 place(feet.x + static_cast<double>(side.x * offset), feet.y, feet.z + static_cast<double>(side.y * offset));
        left_ = !left_;
        if (!takes_prints || takes_prints(place.x, place.z)) {
            // The box's -z (the picture's top) points the way walked.
            const float yaw = std::atan2(-forward.x, -forward.y);
            prints_.emplace_back(place, glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)),
                                 glm::vec3(settings_.print_size.x, settings_.box_height, settings_.print_size.y));
            if (static_cast<int>(prints_.size()) > settings_.max_prints) {
                prints_.erase(prints_.begin());
            }
        }
    }
    last_feet_ = feet;
}

const std::vector<Transform>& FootprintTrail::GetPrints() const {
    return prints_;
}

void FootprintTrail::Clear() {
    prints_.clear();
    has_last_ = false;
    walked_ = 0.0f;
}

void FootprintTrail::Draw(Renderer& renderer, const DecalMaterial& material) const {
    for (const Transform& print : prints_) {
        renderer.DrawDecal(print, material);
    }
}

} // namespace poligon
