#pragma once

// Poligon uses flecs as its entity component system (ECS) and does not wrap it:
// game code works with flecs::world, flecs::entity and flecs systems directly.
//
// Project rules (see docs/ecs.md):
//   - Typed C++ API only. No query strings, no flecs script.
//   - Every component registers its members, otherwise its values are missing from state dumps.
//   - All systems are created at startup, before any entity is deleted, each with an explicit phase.
//   - Single-threaded: never call world.set_threads.

#include <flecs.h>

namespace poligon {

// Registers the members of engine value types (glm::vec2, vec3, vec4, quat and Color) so that components
// containing them can be written to a state dump. Call once, right after creating the world.
void RegisterEngineTypes(flecs::world& world);

} // namespace poligon
