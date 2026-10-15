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

#include "poligon/plant_rules.h"
#include "poligon/plants.h"
#include "poligon/procedural_plants.h"
#include "poligon/trample.h"
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
            const size_t expected = model == &grass ? 6 : 3;
            Expect(poligon::MakePlantKind(loaded, settings, vegetation).variants.size() == expected, "each tuft or clump is a variant");
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
