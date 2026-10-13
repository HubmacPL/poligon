#include "poligon/free_camera.h"

#include <cmath>

namespace poligon {

glm::vec3 FreeCamera::GetForward() const {
    const float yaw = glm::radians(yaw_degrees);
    const float pitch = glm::radians(pitch_degrees);
    return {-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

void FreeCamera::Turn(float yaw_delta_degrees, float pitch_delta_degrees) {
    yaw_degrees = std::fmod(yaw_degrees + yaw_delta_degrees, 360.0f);
    pitch_degrees = glm::clamp(pitch_degrees + pitch_delta_degrees, -89.0f, 89.0f);
}

void FreeCamera::Move(float forward, float right, float up) {
    const float yaw = glm::radians(yaw_degrees);
    const glm::vec3 right_direction{std::cos(yaw), 0.0f, -std::sin(yaw)};
    position += GetForward() * forward + right_direction * right + glm::vec3(0.0f, up, 0.0f);
}

void FreeCamera::SetFromCamera(const Camera& camera) {
    position = camera.position;
    vertical_fov_degrees = camera.vertical_fov_degrees;
    const glm::vec3 direction = glm::normalize(camera.target - camera.position);
    yaw_degrees = glm::degrees(std::atan2(-direction.x, -direction.z));
    pitch_degrees = glm::clamp(glm::degrees(std::asin(glm::clamp(direction.y, -1.0f, 1.0f))), -89.0f, 89.0f);
}

Camera FreeCamera::ToCamera(const Camera& base) const {
    Camera camera = base;
    camera.position = position;
    camera.target = position + GetForward();
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.vertical_fov_degrees = vertical_fov_degrees;
    return camera;
}

} // namespace poligon
