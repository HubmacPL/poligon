#pragma once

#include <cstdint>
#include <memory>

#include "poligon/math.h"

namespace poligon {

// Physics: bodies that fall, collide and come to rest, a character that walks on them, and
// rays for asking "what is in this direction". The work is done by the Jolt Physics
// library; this header is all of it that shows.
//
// Units: meters, kilograms, seconds. +Y is up.
//
// Determinism: a PhysicsWorld that receives the same calls in the same order, with the same
// step length, gives bit-identical results on the same machine with the same program. Call
// Step once per tick with the tick's length, never with measured time.

// Identifies a body in a PhysicsWorld. An id of 0 is never a valid body.
struct BodyHandle {
    std::uint32_t id = 0;
};

// Identifies a character in a PhysicsWorld. An id of 0 is never a valid character.
struct CharacterHandle {
    std::uint32_t id = 0;
};

struct PhysicsConfig {
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    // The most bodies the world can ever hold at once.
    int max_bodies = 16384;
};

// Where a new body is and how it behaves.
struct BodySettings {
    // The center of the body's shape.
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    // false: the body never moves (ground, trees, walls). true: it falls and is pushed around.
    bool dynamic = false;
    // Kilograms; dynamic bodies only.
    float mass = 10.0f;
    // 0 = ice, 1 = rubber on concrete.
    float friction = 0.5f;
    // 0 = does not bounce, 1 = bounces back as fast as it arrived. When two bodies meet,
    // the bouncier of the two decides.
    float bounciness = 0.0f;
    glm::vec3 velocity{0.0f};
};

struct CharacterSettings {
    // Where the character's feet are.
    glm::vec3 position{0.0f};
    // The character is a capsule (a cylinder with rounded ends) standing upright.
    float height = 1.8f;
    float radius = 0.3f;
    // Slopes steeper than this cannot be walked up; the character slides down them.
    float max_slope_degrees = 50.0f;
    // Ledges up to this height are stepped onto without jumping.
    float step_height = 0.4f;
    float jump_speed = 4.5f;
};

struct RayHit {
    bool hit = false;
    // From the ray's origin to the point that was hit.
    float distance = 0.0f;
    glm::vec3 position{0.0f};
    // Direction the hit surface faces.
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    BodyHandle body;
};

class PhysicsWorld {
public:
    explicit PhysicsWorld(const PhysicsConfig& config = PhysicsConfig{});
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // --- Bodies. Sizes are half extents: a box of 2 x 1 x 4 m has half_size (1, 0.5, 2). ---
    BodyHandle AddBox(const glm::vec3& half_size, const BodySettings& settings);
    BodyHandle AddSphere(float radius, const BodySettings& settings);
    // Upright along the body's Y axis.
    BodyHandle AddCylinder(float half_height, float radius, const BodySettings& settings);
    // half_height is that of the straight middle part; the rounded ends add `radius` each.
    BodyHandle AddCapsule(float half_height, float radius, const BodySettings& settings);
    // Any triangles, for ground and other things that never move (settings.dynamic must be
    // false). Vertices are relative to settings.position. Triangles are solid from the side
    // their corners appear counter-clockwise, like the meshes the renderer draws.
    BodyHandle AddMesh(const glm::vec3* vertices, int vertex_count, const std::uint32_t* indices, int index_count,
                       const BodySettings& settings);
    // Land from a square grid of heights: `size` x `size` values, row by row, `spacing`
    // meters apart (see poligon/height_map.h). `origin` is the world position of the first
    // point; its y is added to every height. Never moves. `size` must be at least 4.
    BodyHandle AddHeightField(const float* heights, int size, float spacing, const glm::vec3& origin, float friction = 0.5f);
    // The handle must not be used afterwards; its number is not given out again.
    void RemoveBody(BodyHandle body);
    // Bodies currently in the world.
    int GetBodyCount() const;
    // Handles are numbered 1, 2, 3... in the order bodies were added. To visit every body,
    // go from 1 to GetLastBodyId() and skip the ids for which IsBody is false (removed ones).
    std::uint32_t GetLastBodyId() const;
    bool IsBody(BodyHandle body) const;
    bool IsDynamic(BodyHandle body) const;
    // The box, aligned with the world's axes, that encloses the body where it is now.
    void GetBounds(BodyHandle body, glm::vec3& bounds_min, glm::vec3& bounds_max) const;

    glm::vec3 GetPosition(BodyHandle body) const;
    glm::quat GetRotation(BodyHandle body) const;
    glm::vec3 GetVelocity(BodyHandle body) const;
    // Radians per second around each axis.
    glm::vec3 GetAngularVelocity(BodyHandle body) const;
    // Position and rotation as one matrix, ready for Renderer::DrawMesh.
    glm::mat4 GetTransform(BodyHandle body) const;
    // A body that has come to rest stops being simulated until something touches it.
    bool IsAsleep(BodyHandle body) const;

    // Moves a body at once, without regard to what is in the way.
    void SetPosition(BodyHandle body, const glm::vec3& position, const glm::quat& rotation);
    void SetVelocity(BodyHandle body, const glm::vec3& velocity);
    // A sudden push through the body's center: impulse = mass times the change of velocity wanted.
    void AddImpulse(BodyHandle body, const glm::vec3& impulse);

    // --- Characters: the player and anything else that walks. A character is moved by
    // telling it where it wants to go; the world keeps it on the ground, out of walls and
    // off slopes that are too steep. Bodies do not feel a character pushing them. ---
    CharacterHandle AddCharacter(const CharacterSettings& settings);
    void RemoveCharacter(CharacterHandle character);
    // What the character tries to do during the next Step: move with this velocity along
    // the ground (y is ignored), and jump if it stands on something.
    void SetCharacterInput(CharacterHandle character, const glm::vec3& walk_velocity, bool jump);
    // Where the character's feet are.
    glm::vec3 GetCharacterPosition(CharacterHandle character) const;
    glm::vec3 GetCharacterVelocity(CharacterHandle character) const;
    bool IsCharacterOnGround(CharacterHandle character) const;
    void SetCharacterPosition(CharacterHandle character, const glm::vec3& position);

    // --- Time ---
    // Moves the world forward by one tick. delta_seconds must be the same on every call.
    void Step(float delta_seconds);

    // --- Questions ---
    // The first body along the ray. `direction` need not be normalized; characters are not hit.
    RayHit CastRay(const glm::vec3& origin, const glm::vec3& direction, float max_distance) const;

    // A hash of where everything is and how it moves. Two worlds with equal hashes are in
    // the same state (up to hash collisions); used to check that a run is repeatable.
    std::uint64_t HashState() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace poligon
