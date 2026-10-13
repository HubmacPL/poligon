// Example: components, a system, three fixed ticks and a complete state dump.
#include <cstdio>

#include "poligon/ecs.h"
#include "poligon/math.h"
#include "poligon/state_dump.h"

struct Position {
    glm::vec3 value;
};

struct Velocity {
    glm::vec3 value;
};

// A tag: a component without data.
struct Frozen {};

int main() {
    flecs::world world;
    poligon::RegisterEngineTypes(world);

    // Register every member, otherwise the dump cannot show the component's values.
    world.component<Position>().member<glm::vec3>("value");
    world.component<Velocity>().member<glm::vec3>("value");
    world.component<Frozen>();

    // Create all systems at startup, each with an explicit phase. Typed API only, no query strings.
    world.system<Position, const Velocity>("MoveSystem")
        .kind(flecs::OnUpdate)
        .without<Frozen>()
        .each([](flecs::iter& it, size_t, Position& position, const Velocity& velocity) {
            position.value += velocity.value * it.delta_time();
        });

    world.entity("runner").set<Position>({{0.0f, 0.0f, 0.0f}}).set<Velocity>({{6.0f, 0.0f, 0.0f}});
    world.entity("statue").set<Position>({{5.0f, 0.0f, 5.0f}}).set<Velocity>({{1.0f, 0.0f, 0.0f}}).add<Frozen>();

    // Three ticks of 1/60 s. Always pass the time step; flecs must not measure time itself.
    for (int tick = 0; tick < 3; ++tick) {
        world.progress(1.0f / 60.0f);
    }

    std::printf("%s", poligon::DumpWorldStateToJson(world).c_str());
    std::printf("state hash: %016llx\n", static_cast<unsigned long long>(poligon::HashWorldState(world)));
    return 0;
}
