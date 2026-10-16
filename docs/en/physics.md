# Physics

Bodies that fall, collide and come to rest, a character that walks on them, and rays for asking what lies in a direction.

Header: `poligon/physics/physics.h`. Example: `examples/physics`. Test: `tests/physics_test.cpp`.

## How it works

- The work is done by the Jolt Physics library (MIT). Nothing of it shows outside `physics.cpp`; `PhysicsWorld` is the whole interface.
- Units: meters, kilograms, seconds. +Y is up. Gravity is `PhysicsConfig::gravity`.
- The renderer knows nothing of physics. A game asks the world where a body is (`GetTransform`) and draws a mesh there.
- `Step(delta_seconds)` moves the world forward by one tick. Call it once per tick with the tick's length, never with measured time.

## Bodies

- A **static** body never moves (ground, trees, walls). A **dynamic** body (`BodySettings::dynamic = true`) has a `mass`, falls, and is pushed around.
- Shapes: `AddBox(half_size)`, `AddSphere(radius)`, `AddCylinder(half_height, radius)` (upright), `AddCapsule(half_height, radius)`, and `AddMesh(vertices, indices)` for any triangles, static only (ground). Sizes are half extents; `BodySettings::position` is the center of the shape.
- `friction`: 0 is ice, 1 is rubber. `bounciness`: 0 does not bounce, 1 bounces back as fast as it came; of two bodies that meet, the bouncier one decides.
- Each `Add...` returns a `BodyHandle`. Handles are numbered 1, 2, 3... in the order bodies are added, and a number is never reused. `GetLastBodyId()` and `IsBody(handle)` let a tool visit every body.
- Reading: `GetPosition`, `GetRotation`, `GetVelocity`, `GetAngularVelocity`, `GetTransform`, `GetBounds`, `IsAsleep`, `IsDynamic`. A body that has come to rest falls asleep and costs nothing until something touches it.
- Changing: `SetPosition` (at once, through anything), `SetVelocity`, `AddImpulse` (impulse = mass times the change of velocity wanted), `RemoveBody`.

## Characters

- A character is the player or anything else that walks: an upright capsule that is not pushed around like a body but told where it wants to go.
- `AddCharacter(settings)`: `position` is where the feet are; `height`, `radius`; `max_slope_degrees` (steeper slopes cannot be walked up); `step_height` (ledges this high are stepped onto without jumping); `jump_speed`.
- Every tick: `SetCharacterInput(character, walk_velocity, jump)`, then `Step`. The world keeps the character on the ground, out of walls, and falling when there is nothing below.
- `GetCharacterPosition` (feet), `GetCharacterVelocity`, `IsCharacterOnGround`, `SetCharacterPosition`.
- A character standing on a slope does not creep down it. Bodies do not feel a character: it cannot push a crate yet.

## Rays

- `CastRay(origin, direction, max_distance)` returns the first body along the ray: `hit`, `distance`, `position`, `normal` (which way the surface faces) and `body`. Characters are not hit.
- Uses: what a shot hits, how high the ground is under a point, whether something stands between two points.

## Determinism

- A `PhysicsWorld` that receives the same calls in the same order with the same step length gives bit-identical results, on the same machine with the same program. Jolt promises this under those conditions, and we check it: `physics_test` runs a script twice and compares `HashState()` on every tick, and the toy game's replay tests compare the whole state dump.
- What keeps it so: the fixed tick; one thread (`JobSystemSingleThreaded`); bodies added and removed in an order that depends only on the game state. In an ECS system, create a body at once and not from a deferred callback whose order might vary.
- `HashState()` is a hash of every body's and character's position, rotation and velocities.
- Not promised: the same results from a Debug and a Release build, from another compiler, or on another processor family. Jolt has an option for that (about 8% slower) which the vcpkg build does not turn on.

## In a game with flecs

`game/src/toy_world.cpp` is the pattern:

- The `PhysicsWorld` is not a component. Systems reach it through the flecs world's context pointer (`world.set_ctx(&physics)`).
- An entity with a body has a `Body { handle }` component.
- One system steps the world (`flecs::OnValidate`, after the game's own `OnUpdate` systems); the next copies position, rotation and velocity of moving bodies into components. The state dump of the flecs world then shows the whole state of the game, physics included.
- The system that deletes an entity removes its body first.

## Known limits

- No joints (hinges, ropes), no ragdolls, no vehicles, no soft bodies yet; Jolt has them all.
- No contact events yet ("this touched that"); a game can ask with rays and positions only.
- A dynamic body cannot have a triangle mesh as its shape.
- Collision shapes are not drawn as wireframes. `forest_scene` (key B) draws each body's bounding box as a solid colored box instead.
- No sphere or cylinder meshes in the renderer yet, so round bodies are drawn as boxes in the examples.
