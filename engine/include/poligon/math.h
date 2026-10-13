#pragma once

// Minimal 3D math for the renderer.
// Conventions: right-handed world, +Y is up, the camera looks down its local -Z axis.
// Matrices are 4x4, column-major (element(row, column) = m[column * 4 + row]) and
// transform column vectors: world_position = model * local_position.
// Clip space depth runs from 0 (near) to 1 (far).

namespace poligon {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3 Add(Vec3 a, Vec3 b);
Vec3 Subtract(Vec3 a, Vec3 b);
Vec3 Scale(Vec3 v, float factor);
float Dot(Vec3 a, Vec3 b);
Vec3 Cross(Vec3 a, Vec3 b);
float Length(Vec3 v);
// Returns the zero vector when v has zero length.
Vec3 Normalize(Vec3 v);

struct Mat4 {
    // Identity by default.
    float m[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
};

// Returns a * b: the transform that applies b first, then a.
Mat4 Multiply(const Mat4& a, const Mat4& b);
Mat4 MakeTranslation(Vec3 offset);
Mat4 MakeScale(Vec3 factors);
// Rotation around the +Y axis; positive angles turn +Z towards +X.
Mat4 MakeRotationY(float angle_radians);
// View matrix for a camera at `eye` looking at `target`.
Mat4 MakeLookAt(Vec3 eye, Vec3 target, Vec3 up);
// Perspective projection. aspect_ratio is width / height.
Mat4 MakePerspective(float vertical_fov_radians, float aspect_ratio, float near_plane, float far_plane);

float DegreesToRadians(float degrees);

} // namespace poligon
