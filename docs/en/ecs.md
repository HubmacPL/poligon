# ECS (flecs) and the state dump

Headers: `poligon/ecs/ecs.h`, `poligon/ecs/state_dump.h`.
Example: `examples/state_dump`. Pattern for a larger game: `game/src/toy_world.cpp`.

## Terms

- **Entity**: just an identifier. **Component**: data attached to an entity. **Tag**: a component without data. **System**: a function run every tick for all entities with a given set of components. **Singleton**: a component that exists once for the whole world.
- **Phase**: a stage of a tick (`flecs::OnUpdate`, `flecs::PostUpdate`, ...). Phases run in a fixed order, systems within one phase in creation order.

The engine does not wrap flecs: the game uses `flecs::world`, `flecs::entity` and systems directly (version 4.1.6).

## Project rules

1. Typed C++ API only. No query strings, no flecs script.
2. Every component registers its members: `world.component<T>().member<float>("x")...`. Without that its values are missing from state dumps.
3. All systems are created at startup, before any entity is deleted, each with an explicit phase `.kind(...)`.
4. One thread: do not call `world.set_threads`.
5. Time only from the step: `world.progress(delta)` always with the step given; inside systems `it.delta_time()`.
6. Game state lives in components. Random numbers: a generator whose state is a component member (see `Spawner` in the toy), never `rand()` or the clock.
7. At the start: `poligon::RegisterEngineTypes(world)`, so that `glm::vec2`, `vec3`, `vec4`, `quat` and `Color` can be dumped. Use them as members: `.member<glm::vec3>("position")`.

## Changes during a system

Creating and deleting entities and adding and removing components inside a system are queued and applied after the system. Changing the value of an existing component takes effect at once.

## State dump

- `DumpWorldStateToJson(world)` returns the whole game state as JSON; `SaveWorldStateToJson(world, path)` writes it to a file; `HashWorldState(world)` gives a 64-bit hash.
- Program flag: `--dump-state FILE.json` (written when the run ends). The toy game also prints `state_hash=...`.
- The dump holds all living game entities (id, generation, name, components) and singletons. It holds no definitions: components, systems, modules or anything internal to flecs.
- **Generation** counts how many times an entity index has been reused after its predecessor was deleted.
- Numbers are exact: `float` has 9 significant digits, `double` 17. Two dumps are byte-identical exactly when the states are identical.
- A component without registered members appears as `{"unreflected_bytes": N}` and is listed under `unreflected_components`. The game's tests require that list to be empty.
- Supported member types: bool, integers, float, double, `flecs::entity_t` (through `.member(flecs::Entity, "name")`), enums, nested structs, fixed-size arrays. Other types (strings, vectors) end the process with a `FATAL` error naming the component; `state_dump.cpp` must then be extended.

Why our own dump instead of flecs' `world.to_json()`: the built-in writer truncates floating-point numbers to 10 decimal places, so two different states can give the same text. `tests/state_dump_test.cpp` prints, for information, whether that is still the case.

## Repeatability

- The flecs documentation guarantees the order of systems (given rule 3), but does not say outright that the same history of operations gives the same data layout in two runs.
- `tests/ecs_order_probe.cpp` checks it: a fixed pseudo-random script of creation, deletion from the middle, adding and removing components, deleting parents with children, and systems that change entities while iterating. Systems fold the visiting order into a hash. The test requires an identical result twice in one process and between two processes.
- `game/tests/toy_world_test.cpp` does the same for the toy's simulation: two worlds after 600 ticks must give an identical dump.

## Not there yet

- Loading state from JSON.
- The flecs remote viewer (REST); to be enabled only in development builds, when needed.
