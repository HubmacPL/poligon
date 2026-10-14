#pragma once

#include <cstdint>

#include "poligon/ecs.h"
#include "poligon/input.h"
#include "poligon/math.h"
#include "poligon/physics.h"
#include "poligon/renderer.h"

// Components and systems of the toy game: boxes that fall, tumble, pile up and expire.
//
// Movement is simulated by a poligon::PhysicsWorld. Every tick the physics world is stepped
// and where it has put the bodies is copied into Position, Rotation and Velocity, so the
// state dump of the flecs world shows the whole state of the game.
namespace toy {

// Center of a box, in world space.
struct Position {
    glm::vec3 value;
};

struct Rotation {
    glm::quat value;
};

struct Velocity {
    glm::vec3 value;
};

struct Box {
    glm::vec3 size;
    poligon::Color color;
};

// The flat ground square; its top is at height 0.
struct Ground {
    float size;
    poligon::Color color;
};

// The entity's body in the physics world (the id of a poligon::BodyHandle).
struct Body {
    std::uint32_t handle;
};

// Tag: the body moves (falls, is pushed). Entities without it stand still forever.
struct Falling {};

// The entity is deleted when ticks_left reaches zero.
struct Lifetime {
    std::int32_t ticks_left;
};

// Throws a small box into the air every spawn_interval ticks.
struct Spawner {
    std::uint32_t random_state;  // state of the spawner's own random number generator
    std::int32_t ticks_until_spawn;
    std::int32_t spawn_interval;
    std::int32_t spawned_count;
};

// Singleton: one value for the whole world.
struct ToyStats {
    std::int32_t expired_count;
};

// Tag: the box the player steers.
struct Player {};

// Singleton: what the player wants in the current tick. Filled from poligon::Input by
// UpdatePlayerInput before every world.progress; systems read only this, never Input itself.
struct PlayerInput {
    float move_x;       // -1 = left, +1 = right
    float move_z;       // -1 = away from the starting camera, +1 = towards it
    bool jump_pressed;  // true only on the tick the jump key went down
    float turn_camera;  // degrees to turn the camera this tick
};

// Singleton: the camera circles the middle of the scene at this angle.
struct CameraRig {
    float yaw_degrees;
};

// Handles of the toy's input actions.
struct ToyActions {
    poligon::InputAction move_x;
    poligon::InputAction move_z;
    poligon::InputAction jump;
    poligon::InputAction look_x;     // mouse movement, pixels
    poligon::InputAction look_hold;  // the camera turns only while this is down
};

// Registers components, creates all systems and the starting entities, and their bodies in
// `physics`. The physics world must be empty, must outlive the flecs world's last
// progress() call, and must not be stepped by anyone else.
void SetUpToyWorld(flecs::world& world, poligon::PhysicsWorld& physics);

// Creates the toy's actions and their key bindings:
// A/D or Left/Right, W/S or Up/Down, Space to jump, hold the right mouse button and move the mouse to look.
ToyActions SetUpToyInput(poligon::Input& input);

// Copies this tick's action values into the PlayerInput singleton. Call once per tick, before world.progress.
void UpdatePlayerInput(flecs::world& world, const poligon::Input& input, const ToyActions& actions);

} // namespace toy
