// Checks world streaming (poligon/world/streaming.h): the grid's windows and their order,
// the window following the observer (patches come in nearest ring first and leave when out
// of it), and that the simulation does not depend on loading speed: two runs whose background
// loads take 0 and 15 ms give the same physics state on every tick.
// Usage: streaming_test
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <vector>

#include "poligon/physics/physics.h"
#include "poligon/world/height_map.h"
#include "poligon/world/streaming.h"
#include "test_support.h"

namespace {

// A source that only remembers what it was given.
struct NumberData : poligon::PatchData {
    int value = 0;
};

class CountingSource : public poligon::PatchSource {
public:
    std::unique_ptr<poligon::PatchData> Load(poligon::PatchCoord patch) override {
        auto data = std::make_unique<NumberData>();
        data->value = patch.x * 1000 + patch.z;
        return data;
    }
    void Attach(poligon::PatchCoord patch, std::unique_ptr<poligon::PatchData> data) override {
        values[patch] = static_cast<NumberData&>(*data).value;
    }
    void Detach(poligon::PatchCoord patch) override { values.erase(patch); }

    std::map<poligon::PatchCoord, int> values;
};

// Land for physics: each patch is a height field cut from one noise map (points shared on
// the borders, so neighbors meet exactly).
struct HeightsData : poligon::PatchData {
    poligon::HeightMap heights;
};

class LandPhysicsSource : public poligon::PatchSource {
public:
    LandPhysicsSource(const poligon::HeightMap& map, const poligon::StreamingGrid& grid, poligon::PhysicsWorld& physics)
        : map_(map), grid_(grid), physics_(physics) {}
    std::unique_ptr<poligon::PatchData> Load(poligon::PatchCoord patch) override {
        const int points = static_cast<int>(grid_.GetSettings().patch_size / map_.spacing);
        auto data = std::make_unique<HeightsData>();
        data->heights = poligon::CutHeightMap(map_, patch.x * points, patch.z * points, points + 1, points + 1);
        return data;
    }
    void Attach(poligon::PatchCoord patch, std::unique_ptr<poligon::PatchData> data) override {
        const poligon::HeightMap& heights = static_cast<HeightsData&>(*data).heights;
        const glm::dvec2 corner = grid_.GetCorner(patch);
        bodies_[patch] = physics_.AddHeightField(heights.heights.data(), heights.width, heights.spacing, {corner.x, 0.0, corner.y});
    }
    void Detach(poligon::PatchCoord patch) override {
        physics_.RemoveBody(bodies_.at(patch));
        bodies_.erase(patch);
    }
    size_t GetCount() const { return bodies_.size(); }

private:
    const poligon::HeightMap& map_;
    const poligon::StreamingGrid& grid_;
    poligon::PhysicsWorld& physics_;
    std::map<poligon::PatchCoord, poligon::BodyHandle> bodies_;
};

// A walk over 1 km of hills cut into 128 m patches, with crates thrown ahead now and then.
// Returns the physics state hash after every tick.
std::vector<std::uint64_t> Walk(const poligon::HeightMap& map, int load_delay_milliseconds, int& most_physics_patches,
                                bool& outside_never_attached, int& drawn_attached) {
    poligon::StreamingGridSettings settings;
    settings.patch_size = 128.0;
    settings.patch_count_x = 8;
    settings.patch_count_z = 8;
    const poligon::StreamingGrid grid(settings);
    poligon::PhysicsWorld physics;
    LandPhysicsSource land(map, grid, physics);
    CountingSource drawn_a;
    CountingSource drawn_b;
    poligon::WorldStreamer streamer(grid);
    const int land_source = streamer.AddSource(land, 1, poligon::StreamingMode::Immediate);
    const int drawn_source = streamer.AddSource(drawn_a, 3, poligon::StreamingMode::Background);
    streamer.AddSource(drawn_b, 2, poligon::StreamingMode::Background);
    streamer.SetLoadDelayMilliseconds(load_delay_milliseconds);
    streamer.SetMaxAttachPerFrame(1);

    poligon::CharacterSettings walker_settings;
    walker_settings.position = {100.0, map.GetHeight(100.0f, 100.0f) + 1.0, 100.0};
    const poligon::CharacterHandle walker = physics.AddCharacter(walker_settings);
    std::vector<poligon::BodyHandle> crates;
    std::vector<std::uint64_t> hashes;
    most_physics_patches = 0;
    outside_never_attached = true;
    for (int tick = 0; tick < 600; ++tick) {
        std::vector<glm::dvec3> positions = {physics.GetCharacterPosition(walker)};
        for (const poligon::BodyHandle crate : crates) {
            positions.push_back(physics.GetPosition(crate));
        }
        streamer.RequirePatches(positions);
        if (tick % 150 == 75) {
            poligon::BodySettings crate;
            crate.dynamic = true;
            crate.mass = 5.0f;
            crate.position = physics.GetCharacterPosition(walker) + glm::dvec3(2.0, 2.0, 2.0);
            crate.velocity = {6.0f, 2.0f, 6.0f};
            crates.push_back(physics.AddBox({0.3f, 0.3f, 0.3f}, crate));
        }
        // Diagonally across the patches, 8 m/s: 600 ticks cross four patch borders each way.
        physics.SetCharacterInput(walker, {5.6f, 0.0f, 5.6f}, false);
        physics.Step(1.0f / 60.0f);
        hashes.push_back(physics.HashState());
        // A frame: the drawn data follows the walker, at whatever speed it loads.
        streamer.Update(physics.GetCharacterPosition(walker));
        most_physics_patches = std::max(most_physics_patches, static_cast<int>(land.GetCount()));
        const poligon::PatchCoord here = grid.GetPatchAt(physics.GetCharacterPosition(walker));
        for (const poligon::PatchCoord patch : streamer.GetAttached(land_source)) {
            bool needed = poligon::StreamingGrid::GetRing(here, patch) <= 1;
            for (const poligon::BodyHandle crate : crates) {
                needed = needed || poligon::StreamingGrid::GetRing(grid.GetPatchAt(physics.GetPosition(crate)), patch) <= 1;
            }
            outside_never_attached = outside_never_attached && needed;
        }
    }
    drawn_attached = static_cast<int>(streamer.GetAttachOrder(drawn_source).size());
    return hashes;
}

} // namespace

int main() {
    using test::Expect;

    // --- The grid ---
    poligon::StreamingGridSettings settings;
    settings.patch_size = 512.0;
    settings.origin = {-1000.0, 2000.0};
    settings.patch_count_x = 64;
    settings.patch_count_z = 64;
    const poligon::StreamingGrid grid(settings);
    const poligon::PatchCoord at = grid.GetPatchAt({-1000.0 + 512.0 * 10.5, 0.0, 2000.0 + 512.0 * 20.25});
    Expect(at.x == 10 && at.z == 20, "a position falls into the patch that holds it");
    Expect(grid.GetPatchAt({-1000.5, 0.0, 2000.0}).x == -1 && !grid.Contains(grid.GetPatchAt({-1000.5, 0.0, 2000.0})),
           "outside the world there are no patches");
    const std::vector<poligon::PatchCoord> window = grid.GetWindow({-1000.0 + 512.0 * 10.5, 0.0, 2000.0 + 512.0 * 20.5}, 4);
    bool rings_in_order = window.size() == 81 && window.front() == at;
    for (size_t i = 1; i < window.size(); ++i) {
        rings_in_order = rings_in_order && poligon::StreamingGrid::GetRing(at, window[i - 1]) <= poligon::StreamingGrid::GetRing(at, window[i]);
    }
    Expect(rings_in_order, "a 9 x 9 window lists its 81 patches nearest ring first");
    Expect(grid.GetWindow({-1000.0 + 10.0, 0.0, 2000.0 + 10.0}, 4).size() == 25, "at the world's corner the window holds only patches that exist");

    // --- The window follows the observer ---
    {
        poligon::WorldStreamer streamer(grid);
        CountingSource source;
        const int index = streamer.AddSource(source, 2, poligon::StreamingMode::Background);
        const glm::dvec3 first_place(-1000.0 + 512.0 * 30.5, 0.0, 2000.0 + 512.0 * 30.5);
        streamer.Update(first_place);
        streamer.FinishLoading();
        const std::vector<poligon::PatchCoord> order = streamer.GetAttachOrder(index);
        const poligon::PatchCoord center = grid.GetPatchAt(first_place);
        bool nearest_first = order.size() == 25 && order.front() == center;
        for (size_t i = 1; i < order.size(); ++i) {
            nearest_first = nearest_first && poligon::StreamingGrid::GetRing(center, order[i - 1]) <= poligon::StreamingGrid::GetRing(center, order[i]);
        }
        Expect(nearest_first, "a 5 x 5 window comes in nearest ring first");
        Expect(source.values.size() == 25 && source.values.at({30, 30}) == 30030, "every patch is handed over with its own data");

        streamer.Update(first_place + glm::dvec3(512.0, 0.0, 0.0));
        streamer.FinishLoading();
        bool shifted = source.values.size() == 25;
        for (int z = 28; z <= 32; ++z) {
            shifted = shifted && source.values.count({28, z}) == 0 && source.values.count({33, z}) == 1;
        }
        const std::vector<poligon::PatchCoord> new_order = streamer.GetAttachOrder(index);
        bool only_new_column = new_order.size() == 30;
        for (size_t i = 25; i < new_order.size(); ++i) {
            only_new_column = only_new_column && new_order[i].x == 33;
        }
        Expect(shifted, "a step east lets the west column go and brings the east one");
        Expect(only_new_column, "only the new column is loaded");
        const poligon::StreamingStats stats = streamer.GetStats();
        Expect(stats.attached == 30 && stats.detached == 5 && stats.pending == 0, "attached and detached patches are counted");

        // Patches that leave before their load is handed over are dropped, not attached.
        streamer.SetLoadDelayMilliseconds(5);
        streamer.Update(first_place + glm::dvec3(512.0 * 10.0, 0.0, 0.0));
        streamer.Update(first_place);
        streamer.FinishLoading();
        bool back_home = source.values.size() == 25;
        for (const auto& [patch, value] : source.values) {
            back_home = back_home && poligon::StreamingGrid::GetRing(center, patch) <= 2;
        }
        Expect(back_home, "after a jump away and back only the home window is attached");
    }

    // --- The simulation does not depend on loading speed ---
    {
        const poligon::HeightMap map = poligon::MakeNoiseHeightMap(1025, 1025, 1.0f, 11u, 120.0f, 12.0f);
        int most_fast = 0;
        int most_slow = 0;
        bool outside_fast = false;
        bool outside_slow = false;
        int drawn_fast = 0;
        int drawn_slow = 0;
        const std::vector<std::uint64_t> fast = Walk(map, 0, most_fast, outside_fast, drawn_fast);
        const std::vector<std::uint64_t> slow = Walk(map, 15, most_slow, outside_slow, drawn_slow);
        int first_difference = -1;
        for (size_t i = 0; i < fast.size() && i < slow.size(); ++i) {
            if (fast[i] != slow[i]) {
                first_difference = static_cast<int>(i);
                break;
            }
        }
        std::printf("  walk: %zu ticks, at most %d physics patches at once, first different tick %d; drawn patches handed over: %d "
                    "with fast loads, %d with slow ones\n",
                    fast.size(), most_fast, first_difference, drawn_fast, drawn_slow);
        Expect(drawn_slow < drawn_fast, "the slow run really loaded the drawn data later (fewer patches handed over)");
        Expect(fast.size() == 600 && slow.size() == 600 && first_difference < 0,
               "loads of 0 and 15 ms give the same physics state on every tick");
        Expect(outside_fast && outside_slow, "no patch outside the required windows has physics");
        Expect(most_fast <= 18 && most_fast >= 4, "the physics windows hold only the patches around the walker and the crates");
    }

    return test::ExitCode();
}
