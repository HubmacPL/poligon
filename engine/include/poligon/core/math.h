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
//
// The configuration macros below are set for every target that links poligon_engine
// (see engine/CMakeLists.txt). They must never differ between files of one program.
#if !defined(GLM_FORCE_DEPTH_ZERO_TO_ONE) || !defined(GLM_FORCE_CTOR_INIT) || !defined(GLM_FORCE_XYZW_ONLY)
#error "GLM configuration is missing. Link the poligon_engine target; do not define GLM_FORCE_* by hand."
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
