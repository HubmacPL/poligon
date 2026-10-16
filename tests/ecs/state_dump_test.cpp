#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

#include "poligon/ecs/ecs.h"
#include "poligon/core/math.h"
#include "poligon/ecs/state_dump.h"
#include "test_support.h"

namespace {

enum class Kind : std::int32_t { Small = 3, Large = 7 };

struct Inner {
    float a;
    double b;
};

struct Sample {
    bool flag;
    std::int8_t i8;
    std::uint8_t u8;
    std::int16_t i16;
    std::uint16_t u16;
    std::int32_t i32;
    std::uint32_t u32;
    std::int64_t i64;
    std::uint64_t u64;
    float f32;
    double f64;
    Inner inner;
    float values[3];
    flecs::entity_t target;
    Kind kind;
    glm::vec3 vector;
};

struct Precise {
    double value;
};

struct Marker {};

struct Unregistered {
    int secret;
};

struct Settings {
    std::int32_t level;
};

void RegisterTestComponents(flecs::world& world) {
    poligon::RegisterEngineTypes(world);
    world.component<Kind>();
    world.component<Inner>().member<float>("a").member<double>("b");
    world.component<Sample>()
        .member<bool>("flag")
        .member<std::int8_t>("i8")
        .member<std::uint8_t>("u8")
        .member<std::int16_t>("i16")
        .member<std::uint16_t>("u16")
        .member<std::int32_t>("i32")
        .member<std::uint32_t>("u32")
        .member<std::int64_t>("i64")
        .member<std::uint64_t>("u64")
        .member<float>("f32")
        .member<double>("f64")
        .member<Inner>("inner")
        .member<float>("values", 3)
        .member(flecs::Entity, "target")
        .member<Kind>("kind")
        .member<glm::vec3>("vector");
    world.component<Precise>().member<double>("value");
    world.component<Marker>();
    world.component<Settings>().member<std::int32_t>("level");
}

bool Contains(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

std::string DumpWithPrecise(double value) {
    flecs::world world;
    RegisterTestComponents(world);
    world.entity("probe").set<Precise>({value});
    return poligon::DumpWorldStateToJson(world);
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;

    {
        flecs::world world;
        RegisterTestComponents(world);
        Expect(Contains(poligon::DumpWorldStateToJson(world), "\"entity_count\": 0"),
               "a world with only definitions has no game entities");
        Expect(!Contains(poligon::DumpWorldStateToJson(world), "flecs."), "nothing internal to flecs is dumped");

        const flecs::entity other = world.entity("other").add<Marker>();
        Sample sample{};
        sample.flag = true;
        sample.i8 = -8;
        sample.u8 = 200;
        sample.i16 = -1600;
        sample.u16 = 60000;
        sample.i32 = -320000;
        sample.u32 = 4000000000u;
        sample.i64 = -6400000000000ll;
        sample.u64 = 18000000000000000000ull;
        sample.f32 = 0.1f;
        sample.f64 = 0.1;
        sample.inner = {1.5f, 2.5};
        sample.values[0] = 1.0f;
        sample.values[1] = 2.0f;
        sample.values[2] = 3.0f;
        sample.target = other.id();
        sample.kind = Kind::Large;
        sample.vector = {4.0f, 5.0f, 6.0f};
        world.entity("thing").set<Sample>(sample);
        world.set<Settings>({42});

        const std::string dump = poligon::DumpWorldStateToJson(world);
        Expect(Contains(dump, "\"entity_count\": 2"), "both entities are counted");
        Expect(Contains(dump, "\"name\": \"thing\""), "entity names are written");
        Expect(Contains(dump, "\"flag\": true"), "bool");
        Expect(Contains(dump, "\"i8\": -8") && Contains(dump, "\"u8\": 200"), "8-bit integers");
        Expect(Contains(dump, "\"i16\": -1600") && Contains(dump, "\"u16\": 60000"), "16-bit integers");
        Expect(Contains(dump, "\"i32\": -320000") && Contains(dump, "\"u32\": 4000000000"), "32-bit integers");
        Expect(Contains(dump, "\"i64\": -6400000000000") && Contains(dump, "\"u64\": 18000000000000000000"),
               "64-bit integers");
        Expect(Contains(dump, "\"f32\": 0.100000001"), "float has 9 significant digits");
        Expect(Contains(dump, "\"f64\": 0.10000000000000001"), "double has 17 significant digits");
        Expect(Contains(dump, "\"inner\": {\"a\": 1.5, \"b\": 2.5}"), "nested struct");
        Expect(Contains(dump, "\"values\": [1, 2, 3]"), "fixed-size array");
        Expect(Contains(dump, ("\"target\": " + std::to_string(other.id())).c_str()), "entity member is its id");
        Expect(Contains(dump, "\"kind\": 7"), "enum is its number");
        Expect(Contains(dump, "\"vector\": {\"x\": 4, \"y\": 5, \"z\": 6}"), "engine value type");
        Expect(Contains(dump, "Marker\": true"), "tag is written as true");
        Expect(Contains(dump, "Settings\": {\"level\": 42}"), "singleton value is written");
        Expect(Contains(dump, "\"unreflected_components\": []"), "everything here is registered");
        Expect(dump == poligon::DumpWorldStateToJson(world), "dumping twice gives the same text");

        if (argc > 1) {
            Expect(poligon::SaveWorldStateToJson(world, argv[1]), "the dump can be saved to a file");
            std::FILE* file = nullptr;
            std::string from_file;
            if (fopen_s(&file, argv[1], "rb") == 0 && file != nullptr) {
                char buffer[4096];
                size_t count = 0;
                while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
                    from_file.append(buffer, count);
                }
                std::fclose(file);
            }
            Expect(from_file == dump, "the saved file holds exactly the dump");
        }
    }

    {
        // The smallest possible change of a double must change the dump and the hash.
        const double value = 1234.5678;
        const double next_value = std::nextafter(value, 2000.0);
        Expect(DumpWithPrecise(value) != DumpWithPrecise(next_value), "adjacent doubles give different dumps");

        flecs::world first;
        RegisterTestComponents(first);
        first.entity("probe").set<Precise>({value});
        flecs::world second;
        RegisterTestComponents(second);
        second.entity("probe").set<Precise>({next_value});
        Expect(poligon::HashWorldState(first) != poligon::HashWorldState(second), "and different hashes");

        // For information only: flecs' own JSON writer keeps fewer digits, which is why
        // Poligon has its own dump. If this ever prints "different", flecs has changed.
        const bool flecs_json_same = first.to_json() == second.to_json();
        std::printf("info: flecs world.to_json() for adjacent doubles: %s\n", flecs_json_same ? "same" : "different");
    }

    {
        Expect(Contains(DumpWithPrecise(std::numeric_limits<double>::quiet_NaN()), "\"value\": \"nan\""),
               "NaN is written as a string");
        Expect(Contains(DumpWithPrecise(std::numeric_limits<double>::infinity()), "\"value\": \"inf\""),
               "infinity is written as a string");
    }

    {
        flecs::world world;
        RegisterTestComponents(world);
        world.entity("hidden").set<Unregistered>({5});
        const std::string dump = poligon::DumpWorldStateToJson(world);
        Expect(Contains(dump, "{\"unreflected_bytes\": 4}"), "a component without members shows its size only");
        Expect(Contains(dump, "\"unreflected_components\": [\"") && Contains(dump, "Unregistered\"]"),
               "and is listed as unreflected");
    }

    {
        // A deleted entity disappears; the entity that reuses its id has a higher generation.
        flecs::world world;
        RegisterTestComponents(world);
        const flecs::entity first = world.entity().add<Marker>();
        const std::uint32_t index = static_cast<std::uint32_t>(first.id());
        first.destruct();
        Expect(Contains(poligon::DumpWorldStateToJson(world), "\"entity_count\": 0"), "deleted entity is gone");
        const flecs::entity second = world.entity().add<Marker>();
        Expect(static_cast<std::uint32_t>(second.id()) == index, "the id index is reused");
        Expect(Contains(poligon::DumpWorldStateToJson(world), "\"generation\": 1"), "with generation 1");
    }

    return test::ExitCode();
}
