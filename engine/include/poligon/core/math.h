#pragma once

// Poligon uses GLM for math and does not wrap it: engine and game code work with
// glm::vec3, glm::mat4, glm::quat and GLM functions directly.
// Include this header instead of GLM headers, so that every file sees the same configuration.
//
// Conventions (checked by tests/core/math_test.cpp):
//   - Right-handed world, +Y is up, a camera looks down its local -Z axis.
//   - Matrices are column-major and transform column vectors: world = model * local,
//     and (a * b) applies b first.
//   - Clip space depth runs from 0 (near) to 1 (far).
//   - Angles passed to GLM functions are in radians; use glm::radians(degrees).
//   - A default-constructed vector is zero, a matrix is identity, a quaternion is "no rotation".
//   - Positions in the world are double (glm::dvec3, Transform::position), so that a world can
//     reach tens of kilometers from its origin without objects shaking; everything else is
//     float: rotations, scales, normals, velocities, positions inside a model, vertices,
//     joints, and positions stored relative to a nearby origin (a cell, a patch, an emitter,
//     the camera). Like Unreal's large world coordinates, no wider. Converting a world
//     position to float is done in one place at the boundary, as an offset from such an origin.
//
// The configuration macros below are set for every target that links poligon_engine
// (see engine/CMakeLists.txt). They must never differ between files of one program.
#if !defined(GLM_FORCE_DEPTH_ZERO_TO_ONE) || !defined(GLM_FORCE_CTOR_INIT) || !defined(GLM_FORCE_XYZW_ONLY)
#error "GLM configuration is missing. Link the poligon_engine target; do not define GLM_FORCE_* by hand."
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace poligon {

// Where a thing stands in the world: a double position, a rotation and a scale (applied in
// that order: scale, then rotate, then move). Use it wherever a world placement is passed;
// glm::mat4 stays for what is local (inside a model, relative to a nearby origin).
struct Transform {
    glm::dvec3 position{0.0};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    Transform() = default;
    explicit Transform(const glm::dvec3& position_, const glm::quat& rotation_ = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                       const glm::vec3& scale_ = glm::vec3(1.0f))
        : position(position_), rotation(rotation_), scale(scale_) {}

    // Rotation and scale as a matrix with no translation.
    glm::mat4 GetLocalMatrix() const {
        return glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
    }
    // The whole transform as a float matrix relative to `origin` (the camera, a cell): the
    // position minus the origin is taken in double, then the small offset becomes float.
    glm::mat4 ToMatrixRelativeTo(const glm::dvec3& origin) const {
        glm::mat4 matrix = GetLocalMatrix();
        matrix[3] = glm::vec4(glm::vec3(position - origin), 1.0f);
        return matrix;
    }
};

// A double world origin with a float matrix relative to it, as a float matrix relative to
// another origin: the two origins are subtracted in double.
inline glm::mat4 RelativeTo(const glm::dvec3& origin, const glm::mat4& local, const glm::dvec3& new_origin) {
    glm::mat4 matrix = local;
    const glm::vec3 shift(origin - new_origin);
    matrix[3] += glm::vec4(shift, 0.0f);
    return matrix;
}

} // namespace poligon
