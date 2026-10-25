// Checks plant rules and the trample map: rules are read from level lines (and bad ones
// named by line), placements follow the maps' conditions, are the same on every run, and a
// group never puts two plants in one cell; clumps lie between 0 and 1; the water plants
// and the grass and flowers made by code split into their clumps; trampling, recovering and moving the grid behave.
// Usage: plant_rules_test
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

#include "poligon/world/plant_rules.h"
#include "poligon/world/plants.h"
#include "poligon/world/procedural_plants.h"
#include "poligon/world/trample.h"
#include "test_support.h"

namespace {

poligon::TerrainGeneratorSettings SmallLand() {
    poligon::TerrainGeneratorSettings settings;
    settings.seed = 5;
    settings.size = 512.0f;
    settings.erosion_steps = 40;
    settings.initial_hill_size = 300.0f;
    settings.initial_relief = 20.0f;
    settings.relief = 30.0f;
    settings.basin_count = 1;
    settings.basin_radius = 110.0f;
    settings.basin_sinking = 0.8f;
    settings.thermal_iterations = 6;
    settings.droplets_per_square_meter = 0.05f;
    settings.min_lake_area = 100.0f;
    settings.min_lake_depth = 0.2f;
    return settings;
}

} // namespace

int main() {
    using test::Expect;
    std::string error;

    // --- Reading rules ---
    std::vector<poligon::PlantRule> rules;
    const std::vector<std::pair<int, std::string>> lines = {
        {10, "plant trees/pine"}, {11, "density 0.01"}, {12, "spacing 5"},        {13, "water_depth 0 0 0.01"},
        {14, "group trees"},      {15, "plant reeds"},  {16, "spacing 1.5"},      {17, "density 0.4"},
        {18, "water_depth 0.2 1000 0.05"}, {19, "on_water 0"}, {20, "cover grass"}, {21, "every_nth 2"}};
    Expect(poligon::ParsePlantRules(lines, rules, error) && rules.size() == 3, "three rules are read");
    Expect(rules[0].kind == "trees/pine" && rules[0].group == "trees" && rules[0].spacing == 5.0f &&
               rules[0].conditions.water_depth.high == 0.0f,
           "a rule's values are read");
    Expect(rules[2].is_cover && rules[2].every_nth == 2, "a cover rule is read");
    Expect(!poligon::ParsePlantRules({{3, "density 1"}}, rules, error) && error.find("line 3") != std::string::npos,
           "a setting before any rule is an error that names its line");
    Expect(!poligon::ParsePlantRules({{1, "plant a"}, {2, "slope 30 10 1"}}, rules, error), "a range with low above high is an error");
    Expect(!poligon::ParsePlantRules({{1, "plant a"}, {2, "colour red"}}, rules, error), "an unknown word is an error");

    // --- Placing by the maps ---
    poligon::TerrainMaps maps;
    poligon::GenerateTerrain(SmallLand(), maps);
    Expect(!maps.lakes.empty(), "the test land has a lake");
    poligon::PlantRule wet;
    wet.kind = "reeds";
    wet.spacing = 1.5f;
    wet.density = 0.5f;
    wet.conditions.water_depth = {0.2f, 1000.0f, 0.0f};
    const glm::vec2 middle(256.0f, 256.0f);
    const auto in_water = poligon::PlacePlants(maps, wet, middle, 400.0f, 3);
    bool all_in_water = !in_water.empty();
    bool variants_in_range = true;
    for (const poligon::PlantPlacement& placement : in_water) {
        all_in_water = all_in_water && maps.GetWaterDepth(placement.position.x, placement.position.y) >= 0.199f;
        variants_in_range = variants_in_range && placement.variant >= 0 && placement.variant < 3;
    }
    Expect(all_in_water, "a rule for water deeper than 0.2 m places only there");
    Expect(variants_in_range, "variants are picked among those given");
    const auto again = poligon::PlacePlants(maps, wet, middle, 400.0f, 3);
    bool same = again.size() == in_water.size();
    for (size_t i = 0; same && i < again.size(); ++i) {
        same = again[i].position == in_water[i].position && again[i].scale == in_water[i].scale;
    }
    Expect(same, "the same rule gives the same places on every run");
    poligon::PlantRule floating = wet;
    floating.on_water = true;
    const auto lilies = poligon::PlacePlants(maps, floating, middle, 400.0f, 1);
    bool on_surface = !lilies.empty();
    for (const poligon::PlantPlacement& placement : lilies) {
        const float level = maps.height.GetHeight(placement.position.x, placement.position.y) +
                            maps.GetWaterDepth(placement.position.x, placement.position.y);
        on_surface = on_surface && std::fabs(placement.height - level) < 0.001f;
    }
    Expect(on_surface, "floating plants stand on the water's surface");

    // A group: two land rules sharing one 4 m grid never use one cell twice.
    poligon::PlantRule first;
    first.spacing = 4.0f;
    first.density = 0.04f;
    first.conditions.water_depth = {0.0f, 0.0f, 0.01f};
    poligon::PlantRule second = first;
    second.seed = 9;
    const auto group = poligon::PlacePlantGroup(maps, {&first, &second}, middle, 300.0f, {2, 2});
    std::set<std::pair<int, int>> cells;
    bool unique = true;
    int from_first = 0;
    int from_second = 0;
    for (const auto& [rule, placement] : group) {
        unique = unique && cells.insert({static_cast<int>(placement.position.x / 4.0f), static_cast<int>(placement.position.y / 4.0f)}).second;
        (rule == 0 ? from_first : from_second) += 1;
    }
    if (!unique || from_first == 0 || from_second == 0) {
        std::printf("  %zu placements: %d from the first rule, %d from the second\n", group.size(), from_first, from_second);
    }
    Expect(unique && from_first > 0 && from_second > 0, "a group shares its cells between its rules, one plant per cell");

    // A world cut into patches (poligon/world/streaming.h): each patch places from its own
    // window of the maps, and all patches together give the whole map's plants to the bit.
    // 100 m patches do not divide the 4 m grid of the group evenly, so cells straddle them.
    {
        poligon::PlantRule slope_rule = first;
        slope_rule.conditions.slope_degrees = {0.0f, 20.0f, 5.0f};
        slope_rule.conditions.moisture = {0.1f, 0.9f, 0.1f};
        slope_rule.patch_size = 40.0f;
        poligon::PlantRule water_rule = wet;
        water_rule.on_water = true;
        const std::vector<const poligon::PlantRule*> group_rules = {&slope_rule, &second};
        const auto whole_group = poligon::PlacePlantGroup(maps, group_rules, middle, 300.0f, {3, 2});
        const auto whole_water = poligon::PlacePlants(maps, water_rule, middle, 300.0f, 2);
        std::vector<std::pair<int, poligon::PlantPlacement>> patched_group;
        std::vector<poligon::PlantPlacement> patched_water;
        bool heights_same = true;
        const float size = maps.height.GetSizeX();
        // Patches in a scrambled order: the result must not depend on it.
        const int order[6] = {3, 0, 5, 1, 4, 2};
        for (const int pz : order) {
            for (const int px : order) {
                const glm::vec2 low(static_cast<float>(px) * 100.0f, static_cast<float>(pz) * 100.0f);
                const glm::vec2 high(std::min(low.x + 100.0f, size), std::min(low.y + 100.0f, size));
                if (low.x >= size || low.y >= size) {
                    continue;
                }
                const poligon::TerrainMaps window = poligon::CutTerrainMaps(maps, low, high, 2.0f * maps.map_spacing);
                for (const auto& placed : poligon::PlacePlantGroupInRectangle(window, group_rules, middle, 300.0f, low, high, {3, 2})) {
                    patched_group.push_back(placed);
                }
                for (const auto& placed : poligon::PlacePlantsInRectangle(window, water_rule, middle, 300.0f, low, high, 2)) {
                    patched_water.push_back(placed);
                }
                for (int i = 0; i < 50; ++i) {
                    const glm::vec2 place = low + (high - low) * glm::vec2(static_cast<float>(i % 7) / 7.0f, static_cast<float>(i) / 50.0f);
                    heights_same = heights_same && window.height.GetHeight(place.x, place.y) == maps.height.GetHeight(place.x, place.y) &&
                                   window.Sample(window.moisture, place.x, place.y) == maps.Sample(maps.moisture, place.x, place.y) &&
                                   window.GetWaterDepth(place.x, place.y) == maps.GetWaterDepth(place.x, place.y);
                }
            }
        }
        const auto by_place = [](const poligon::PlantPlacement& a, const poligon::PlantPlacement& b) {
            return a.position.y != b.position.y ? a.position.y < b.position.y : a.position.x < b.position.x;
        };
        const auto same_placement = [](const poligon::PlantPlacement& a, const poligon::PlantPlacement& b) {
            return a.position == b.position && a.height == b.height && a.scale == b.scale && a.turn == b.turn && a.variant == b.variant;
        };
        std::vector<std::pair<int, poligon::PlantPlacement>> sorted_whole = whole_group;
        const auto pair_by_place = [&](const auto& a, const auto& b) { return by_place(a.second, b.second); };
        std::sort(sorted_whole.begin(), sorted_whole.end(), pair_by_place);
        std::sort(patched_group.begin(), patched_group.end(), pair_by_place);
        bool group_same = sorted_whole.size() == patched_group.size();
        for (size_t i = 0; group_same && i < sorted_whole.size(); ++i) {
            group_same = sorted_whole[i].first == patched_group[i].first && same_placement(sorted_whole[i].second, patched_group[i].second);
        }
        std::vector<poligon::PlantPlacement> sorted_water = whole_water;
        std::sort(sorted_water.begin(), sorted_water.end(), by_place);
        std::sort(patched_water.begin(), patched_water.end(), by_place);
        bool water_same = sorted_water.size() == patched_water.size();
        for (size_t i = 0; water_same && i < sorted_water.size(); ++i) {
            water_same = same_placement(sorted_water[i], patched_water[i]);
        }
        std::printf("  patches: %zu group plants and %zu floating plants, whole map: %zu and %zu\n", patched_group.size(),
                    patched_water.size(), whole_group.size(), whole_water.size());
        Expect(heights_same, "a window of the maps gives the whole maps' heights, map values and water depths to the bit");
        Expect(group_same && !whole_group.empty(), "patches placed separately, in any order, give the whole map's group plants to the bit");
        Expect(water_same && !whole_water.empty(), "and the whole map's floating plants");
    }

    bool patches_in_range = true;
    for (int i = 0; i < 200; ++i) {
        const float factor = poligon::GetPatchFactor(30.0f, 0.5f, 7u, glm::vec2(static_cast<float>(i) * 3.1f, static_cast<float>(i) * 1.7f));
        patches_in_range = patches_in_range && factor >= 0.0f && factor <= 1.0f;
    }
    Expect(patches_in_range, "clumps lie between 0 and 1");

    // --- Water plants made by code: three clumps each, which split into three variants ---
    for (const poligon::Model& model : {poligon::MakeReedModel(1u), poligon::MakeCattailModel(2u), poligon::MakeWaterLilyModel(3u)}) {
        Expect(model.GetTriangleCount() > 100 && !model.materials.empty() && model.materials[0].base_color_texture.width > 0,
               "a water plant has triangles and its own textures");
        poligon::LoadedModel loaded;
        loaded.model = model;
        loaded.render.meshes.resize(model.meshes.size());
        loaded.render.materials.resize(model.materials.size());
        for (const poligon::ModelMesh& mesh : model.meshes) {
            loaded.render.mesh_material_index.push_back(mesh.material_index);
        }
        poligon::Vegetation vegetation;
        poligon::PlantSettings settings;
        settings.split_variants = true;
        Expect(poligon::MakePlantKind(loaded, settings, vegetation).variants.size() == 3, "its clumps become three variants");
    }
    // --- Grass and flowers made by code: one variant per tuft or clump ---
    {
        const poligon::Model grass = poligon::MakeGrassModel(5u, 6, 0.12f, 0.3f, 10, 16);
        const poligon::Model flowers = poligon::MakeFlowerModel(6u);
        // 10 to 16 blades of 5 triangles each (two quads and a tip) per tuft.
        Expect(grass.GetTriangleCount() >= 6 * 10 * 5 && grass.GetTriangleCount() <= 6 * 16 * 5, "six tufts of 10 to 16 blades");
        Expect(grass.materials.size() == 1 && grass.materials[0].double_sided && grass.materials[0].back_faces_keep_normal &&
                   !grass.materials[0].casts_ambient_occlusion,
               "grass is double-sided, keeps its normals on the back and casts no ambient occlusion");
        float top = 0.0f;
        bool normals_up = true;
        for (const poligon::ModelMesh& mesh : grass.meshes) {
            for (const poligon::MeshVertex& vertex : mesh.vertices) {
                top = std::max(top, vertex.position.y);
                normals_up = normals_up && vertex.normal.y > 0.5f;
            }
        }
        Expect(normals_up, "grass normals point mostly up");
        Expect(top > 0.1f && top <= 0.3f, "grass is no taller than asked");
        Expect(flowers.materials.size() == 4, "flowers have leaves and three petal colors");
        for (const poligon::Model* model : {&grass, &flowers}) {
            poligon::LoadedModel loaded;
            loaded.model = *model;
            loaded.render.meshes.resize(model->meshes.size());
            loaded.render.materials.resize(model->materials.size());
            for (const poligon::ModelMesh& mesh : model->meshes) {
                loaded.render.mesh_material_index.push_back(mesh.material_index);
            }
            poligon::Vegetation vegetation;
            poligon::PlantSettings settings;
            settings.split_variants = true;
            settings.normal_up = 0.7f;
            settings.casts_ambient_occlusion = false;
            const size_t expected = model == &grass ? 6 : 3;
            const poligon::PlantKind kind = poligon::MakePlantKind(loaded, settings, vegetation);
            Expect(kind.variants.size() == expected, "each tuft or clump is a variant");
            const poligon::Material& material = kind.variants[0].species.levels[0].parts[0].material;
            Expect(material.normal_up == 0.7f && !material.casts_ambient_occlusion, "the kind's lighting settings reach its materials");
        }
        Expect(poligon::MakeGrassModel(7u, 2, 0.4f, 0.8f, 8, 14).meshes[1].vertices[3].position ==
                   poligon::MakeGrassModel(7u, 2, 0.4f, 0.8f, 8, 14).meshes[1].vertices[3].position,
               "the same seed makes the same grass");
    }
    Expect(poligon::MakeReedModel(4u).GetTriangleCount() == poligon::MakeReedModel(4u).GetTriangleCount() &&
               poligon::MakeReedModel(4u).meshes[0].vertices[5].position == poligon::MakeReedModel(4u).meshes[0].vertices[5].position,
           "the same seed makes the same plant");

    // --- Trample map ---
    poligon::TrampleMap trample(64.0f, 128);
    trample.Recenter({100.0f, 100.0f});
    trample.Trample({100.0f, 100.0f}, 1.0f, 0.6f);
    const float pressed = trample.GetValue({100.0f, 100.0f});
    Expect(pressed > 0.5f && trample.GetValue({103.0f, 100.0f}) == 0.0f, "trampling presses plants where it happens only");
    trample.Trample({100.0f, 100.0f}, 1.0f, 0.6f);
    Expect(trample.GetValue({100.0f, 100.0f}) == 1.0f, "trampling again makes the ground fully flat");
    const std::uint32_t version = trample.GetVersion();
    trample.Recenter({110.0f, 95.0f});
    Expect(trample.GetValue({100.0f, 100.0f}) == 1.0f && trample.GetVersion() != version, "moving the grid keeps what was trampled");
    trample.Recover(0.25f);
    Expect(std::fabs(trample.GetValue({100.0f, 100.0f}) - 0.75f) < 0.001f, "plants stand up again little by little");
    trample.Recenter({500.0f, 500.0f});
    Expect(trample.GetValue({100.0f, 100.0f}) == 0.0f, "what the grid leaves behind is forgotten");
    Expect(trample.GetBytes().size() == 128u * 128u, "the grid has resolution x resolution bytes for the renderer");

    std::printf("plant_rules_test finished\n");
    return test::ExitCode();
}
