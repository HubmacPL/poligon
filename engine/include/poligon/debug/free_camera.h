#pragma once

#include "poligon/core/math.h"
#include "poligon/render/renderer.h"

namespace poligon {

// A camera that flies anywhere, for looking around a scene independently of the game.
// It holds plain numbers and reads no devices: the caller passes in how much to move and
// turn, usually from input actions. It is a viewing aid and not part of the game state.
struct FreeCamera {
    glm::vec3 position{0.0f, 2.0f, 5.0f};
    // Turn around the vertical axis: 0 looks along -Z, 90 along -X.
    float yaw_degrees = 0.0f;
    // Up and down: 0 is level, positive looks up. Kept within -89..89.
    float pitch_degrees = 0.0f;
    float vertical_fov_degrees = 60.0f;

    // The direction the camera looks in.
    glm::vec3 GetForward() const;

    // Turns by the given angles.
    void Turn(float yaw_delta_degrees, float pitch_delta_degrees);

    // Moves by the given distances in meters: `forward` along the view direction, `right`
    // sideways, `up` straight up regardless of where the camera looks.
    void Move(float forward, float right, float up);

    // Takes over position and direction from a camera, to start flying from where it is.
    void SetFromCamera(const Camera& camera);

    // `base` supplies everything a FreeCamera does not hold (near and far plane).
    Camera ToCamera(const Camera& base = Camera{}) const;
};

} // namespace poligon
