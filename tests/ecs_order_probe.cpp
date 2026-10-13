// Probe: is flecs repeatable for the same sequence of operations?
//
// Runs a fixed, pseudo-random script of entity creation, deletion from the middle, adding and
// removing components, parent/child deletion, and systems that add components and delete
// entities while iterating. Systems fold the ORDER in which they visit entities into a hash,
// so a different iteration order changes the output even when the final values agree.
//
// The script is run twice in this process (two separate worlds) and the results must be
// identical. The program then prints the result; a second test runs the program twice and
// compares the output between processes, where memory addresses differ.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "poligon/ecs.h"
#include "poligon/state_dump.h"

namespace {

struct Counter {
    std::int32_t value;
};

struct Weight {
    float value;
};

struct Mark {
    std::uint64_t hash;
};

struct Tagged {};

// Order-sensitive record of what the systems did. Lives outside the world on purpose:
// it is printed separately from the state dump.
struct Trace {
    std::uint64_t order_hash = 1469598103934665603ull;
    std::int64_t visits = 0;

    void Visit(std::uint64_t entity_id, std::uint64_t value) {
        order_hash = (order_hash ^ entity_id) * 1099511628211ull;
        order_hash = (order_hash ^ value) * 1099511628211ull;
        ++visits;
    }
};

// Small deterministic random number generator (xorshift32).
struct Random {
    std::uint32_t state = 2463534242u;

    std::uint32_t Next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    std::uint32_t Below(std::uint32_t limit) { return Next() % limit; }
};

struct ProbeResult {
    Trace trace;
    std::string state_json;
};

ProbeResult RunProbe(std::uint32_t seed) {
    ProbeResult result;
    Trace* trace = &result.trace;
    Random random;
    random.state = seed;

    flecs::world world;
    poligon::RegisterEngineTypes(world);
    world.component<Counter>().member<std::int32_t>("value");
    world.component<Weight>().member<float>("value");
    world.component<Mark>().member<std::uint64_t>("hash");
    world.component<Tagged>();

    // All systems are created up front, before any entity is deleted, each with an explicit phase.
    world.system<Counter>("CountSystem").kind(flecs::OnUpdate).each([trace](flecs::entity entity, Counter& counter) {
        counter.value += 1;
        trace->Visit(entity.id(), static_cast<std::uint64_t>(counter.value));
    });

    world.system<const Counter, Weight>("WeightSystem")
        .kind(flecs::OnUpdate)
        .each([trace](flecs::entity entity, const Counter& counter, Weight& weight) {
            weight.value = weight.value * 1.0001f + static_cast<float>(counter.value) * 0.5f;
            std::uint32_t bits = 0;
            std::memcpy(&bits, &weight.value, sizeof(bits));
            trace->Visit(entity.id(), bits);
        });

    // Adds and removes components while iterating (these changes are queued and applied later).
    world.system<const Weight>("MarkSystem")
        .kind(flecs::OnUpdate)
        .with<Tagged>()
        .each([trace](flecs::entity entity, const Weight& weight) {
            trace->Visit(entity.id(), 3);
            if (!entity.has<Mark>()) {
                entity.set<Mark>({entity.id()});
            } else if (weight.value > 50.0f) {
                entity.remove<Tagged>();
            }
        });

    // Deletes entities while iterating.
    world.system<Mark>("ExpireSystem").kind(flecs::PostUpdate).each([trace](flecs::entity entity, Mark& mark) {
        mark.hash = (mark.hash ^ entity.id()) * 1099511628211ull;
        trace->Visit(entity.id(), mark.hash);
        if (mark.hash % 97 == 0) {
            entity.destruct();
        }
    });

    std::vector<flecs::entity> entities;
    const auto create_entity = [&] {
        flecs::entity entity = world.entity();
        const std::uint32_t shape = random.Below(8);
        if (shape & 1) {
            entity.set<Counter>({static_cast<std::int32_t>(random.Below(100))});
        }
        if (shape & 2) {
            entity.set<Weight>({static_cast<float>(random.Below(1000)) * 0.01f});
        }
        if (shape & 4) {
            entity.add<Tagged>();
        }
        entities.push_back(entity);
    };
    // Picks a living entity, forgetting handles of entities that systems or parents deleted.
    const auto pick_alive = [&]() -> flecs::entity {
        while (!entities.empty()) {
            const size_t index = random.Below(static_cast<std::uint32_t>(entities.size()));
            if (entities[index].is_alive()) {
                return entities[index];
            }
            entities.erase(entities.begin() + static_cast<std::ptrdiff_t>(index));
        }
        return flecs::entity();
    };

    for (int i = 0; i < 200; ++i) {
        create_entity();
    }

    for (int tick = 0; tick < 300; ++tick) {
        const std::uint32_t operation_count = 1 + random.Below(6);
        for (std::uint32_t i = 0; i < operation_count; ++i) {
            const std::uint32_t operation = random.Below(7);
            flecs::entity target = pick_alive();
            if (operation == 0 || !target.is_valid()) {
                create_entity();
            } else if (operation == 1) {
                target.destruct();  // deletes from the middle; its id is reused by later entities
            } else if (operation == 2) {
                target.set<Counter>({static_cast<std::int32_t>(random.Below(100))});
            } else if (operation == 3) {
                target.remove<Counter>();
            } else if (operation == 4) {
                target.add<Tagged>();
                target.set<Weight>({static_cast<float>(random.Below(1000)) * 0.01f});
            } else if (operation == 5) {
                // A child is deleted together with its parent.
                flecs::entity child = world.entity().child_of(target);
                child.set<Counter>({1});
                entities.push_back(child);
            } else {
                target.remove<Weight>();
            }
        }
        world.progress(1.0f / 60.0f);
    }

    result.state_json = poligon::DumpWorldStateToJson(world);
    return result;
}

} // namespace

// Usage: ecs_order_probe [SEED]. A different seed gives a different script; it exists to
// check by hand that the printed result really depends on what the script does.
int main(int argc, char** argv) {
    std::uint32_t seed = 2463534242u;
    if (argc > 1) {
        seed = static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10));
        if (seed == 0) {
            std::printf("usage: ecs_order_probe [SEED], where SEED is a non-zero number\n");
            return 2;
        }
    }
    const ProbeResult first = RunProbe(seed);
    const ProbeResult second = RunProbe(seed);

    std::printf("order_hash=%llu visits=%lld\n", static_cast<unsigned long long>(first.trace.order_hash),
                static_cast<long long>(first.trace.visits));
    std::printf("%s", first.state_json.c_str());

    if (first.trace.order_hash != second.trace.order_hash || first.trace.visits != second.trace.visits) {
        std::printf("FAIL: two runs in one process visited entities in a different order\n");
        return 1;
    }
    if (first.state_json != second.state_json) {
        std::printf("FAIL: two runs in one process ended in different states\n");
        return 1;
    }
    if (first.trace.visits < 10000) {
        std::printf("FAIL: the script did too little work to prove anything\n");
        return 1;
    }
    return 0;
}
