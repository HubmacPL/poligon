// Checks a streamed terrain (poligon/world/land.h, poligon/world/streaming.h): generated land
// cut into 128 m patches and drawn patch by patch looks like the same land drawn whole, gives
// the whole land's heights to the bit, has no step at patch borders, and has drawing and
// collisions only where the window holds patches.
// Usage: terrain_streaming_test d3d12|vulkan
#include <cmath>
#include <cstdio>
#include <cstring>

#include "poligon/physics/physics.h"
#include "poligon/render/renderer.h"
#include "poligon/world/ground_cover.h"
#include "poligon/world/land.h"
#include "poligon/world/plant_rules.h"
#include "poligon/world/plants.h"
#include "poligon/world/sky_occlusion.h"
#include "poligon/world/streaming.h"
#include "poligon/world/terrain.h"
#include "poligon/world/trample.h"
#include "poligon/world/water.h"
#include "test_support.h"

namespace {

poligon::TerrainGeneratorSettings SmallLand() {
    poligon::TerrainGeneratorSettings settings;
    settings.seed = 7;
    settings.size = 512.0f;
    settings.erosion_steps = 40;
    settings.initial_hill_size = 250.0f;
    settings.initial_relief = 25.0f;
    settings.relief = 35.0f;
    settings.basin_count = 1;
    settings.basin_radius = 100.0f;
    settings.thermal_iterations = 6;
    settings.droplets_per_square_meter = 0.05f;
    return settings;
}

poligon::Image Draw(poligon::Renderer& renderer, poligon::Terrain& terrain, const poligon::Camera& camera) {
    poligon::DirectionalLight sun;
    sun.direction = {-0.5f, -0.7f, -0.3f};
    sun.color = {1.0f, 1.0f, 1.0f, 1.0f};
    sun.ambient = {0.3f, 0.3f, 0.3f, 1.0f};
    poligon::Environment environment;
    environment.shadows = false;
    renderer.BeginFrame(camera, sun, environment);
    terrain.Draw(camera);
    renderer.EndFrame();
    poligon::Image frame;
    renderer.CaptureFrame(frame);
    return frame;
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;

    poligon::GraphicsBackend backend = poligon::GraphicsBackend::Default;
    if (argc == 2 && std::strcmp(argv[1], "d3d12") == 0) {
        backend = poligon::GraphicsBackend::Direct3D12;
    } else if (argc == 2 && std::strcmp(argv[1], "vulkan") == 0) {
        backend = poligon::GraphicsBackend::Vulkan;
    } else {
        std::printf("usage: terrain_streaming_test d3d12|vulkan\n");
        return 2;
    }

    poligon::TerrainMaps maps;
    poligon::GenerateTerrain(SmallLand(), maps);
    // Two ground layers by slope and moisture, so that the weights matter in the picture.
    poligon::TerrainLayerRule rules[2];
    rules[1].conditions.slope_degrees = {12.0f, 90.0f, 4.0f};
    rules[1].patch_size = 30.0f;
    const glm::dvec3 origin(-256.0, 40.0, -256.0);
    poligon::LandFromMaps land(maps, 128.0, origin, rules, 2, 3u, 16.0f);
    const poligon::LandInfo& info = land.GetInfo();
    Expect(info.grid.patch_count_x == 4 && info.grid.patch_count_z == 4, "512 m of land in 128 m patches is 4 x 4 patches");

    poligon::RendererConfig config;
    config.width = 320;
    config.height = 200;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.post.tone_mapping = poligon::ToneMapping::None;
    poligon::Renderer renderer(config);

    // Two layer colors, flat textures of one color each.
    const poligon::Image layer_images[2] = {poligon::MakeSolidImage(4, 4, 90, 140, 60), poligon::MakeSolidImage(4, 4, 150, 120, 90)};
    poligon::TerrainConfig terrain_config;
    terrain_config.origin = origin;
    terrain_config.detail = 8.0f;  // fine everywhere: both trees end in squares of the same sizes
    terrain_config.material.colors = renderer.CreateTextureArray(layer_images, 2);
    terrain_config.material.layer_count = 2;
    terrain_config.material.steep_start_degrees = 90.0f;

    // The land drawn whole...
    poligon::Image whole_weights[2];
    poligon::MakeTerrainLayerWeights(maps, rules, 2, 3u, whole_weights);
    poligon::TextureOptions plain;
    plain.is_color = false;
    plain.generate_mipmaps = false;
    poligon::TerrainConfig whole_config = terrain_config;
    whole_config.material.weights[0] = renderer.CreateTexture(whole_weights[0], plain);
    whole_config.material.weights[1] = renderer.CreateTexture(whole_weights[1], plain);
    poligon::Terrain whole(renderer, maps.height, whole_config);

    // ...and streamed, every patch through the streamer.
    poligon::Terrain streamed(renderer, info, terrain_config, 20);
    poligon::TerrainPatchSource source(streamed, land);
    const poligon::StreamingGrid grid([&] {
        poligon::StreamingGridSettings settings = info.grid;
        settings.origin = {origin.x, origin.z};
        return settings;
    }());
    {
        poligon::WorldStreamer streamer(grid);
        streamer.AddSource(source, 3, poligon::StreamingMode::Background);
        streamer.Update({0.0, 0.0, 0.0});
        streamer.FinishLoading();
        Expect(streamed.GetPatchCount() == 16, "all 16 patches are loaded");
    }

    // Heights: to the bit inside every patch and on its borders.
    bool heights_same = true;
    double largest_step = 0.0;
    for (int i = 0; i < 400; ++i) {
        const double x = -255.0 + 510.0 * static_cast<double>((i * 37) % 400) / 400.0;
        const double z = -255.0 + 510.0 * static_cast<double>(i) / 400.0;
        heights_same = heights_same && streamed.GetHeight(x, z) == whole.GetHeight(x, z) &&
                       streamed.GetNormal(x, z) == whole.GetNormal(x, z);
    }
    for (int border = 1; border < 4; ++border) {
        const double at = -256.0 + 128.0 * border;
        for (int i = 0; i < 64; ++i) {
            const double along = -250.0 + 8.0 * i;
            for (const double side : {-0.001, 0.0, 0.001}) {
                heights_same = heights_same && streamed.GetHeight(at + side, along) == whole.GetHeight(at + side, along) &&
                               streamed.GetHeight(along, at + side) == whole.GetHeight(along, at + side);
            }
            largest_step = std::max(largest_step, std::fabs(streamed.GetHeight(at - 0.001, along) - streamed.GetHeight(at + 0.001, along)));
            largest_step = std::max(largest_step, std::fabs(streamed.GetHeight(along, at - 0.001) - streamed.GetHeight(along, at + 0.001)));
        }
    }
    std::printf("  largest height step across a patch border over 2 mm: %.6f m\n", largest_step);
    Expect(heights_same, "a streamed terrain gives the whole land's heights and normals to the bit, on patch borders too");
    Expect(largest_step < 0.002, "there is no step in the land at patch borders");

    // Pictures: the same land seen low across several patch borders, and from above.
    poligon::Camera across;
    across.position = {-200.0, whole.GetHeight(-200.0, 180.0) + 12.0, 180.0};
    across.target = {60.0, whole.GetHeight(60.0, -60.0), -60.0};
    across.far_plane = 2000.0f;
    poligon::Camera above;
    above.position = {0.0, 400.0, 0.0};
    above.target = {0.0, 0.0, 0.0};
    above.up = {0.0f, 0.0f, -1.0f};
    const poligon::Camera cameras[2] = {across, above};
    const char* const camera_names[2] = {"across", "above"};
    for (int view = 0; view < 2; ++view) {
        const poligon::Camera& camera = cameras[view];
        const poligon::Image from_whole = Draw(renderer, whole, camera);
        const poligon::Image from_patches = Draw(renderer, streamed, camera);
        int largest = 0;
        int over_two = 0;
        for (size_t i = 0; i < from_whole.pixels.size(); ++i) {
            const int difference = std::abs(static_cast<int>(from_whole.pixels[i]) - static_cast<int>(from_patches.pixels[i]));
            largest = std::max(largest, difference);
            over_two += (difference > 2 && i % 4 == 0) ? 1 : 0;
        }
        const double share = 100.0 * over_two / (from_whole.width * from_whole.height);
        std::printf("  %s: largest channel difference %d, %.3f%% of pixels over 2\n", camera_names[view], largest, share);
        Expect(share < 0.5, "the land drawn patch by patch looks like the land drawn whole");
    }

    // A window of 3 x 3 patches around a corner patch: only those are drawn and collide.
    {
        poligon::PhysicsWorld physics;
        poligon::TerrainPhysicsSource collisions(physics, land);
        poligon::Terrain windowed(renderer, info, terrain_config, 16);
        poligon::TerrainPatchSource windowed_source(windowed, land);
        poligon::WorldStreamer streamer(grid);
        streamer.AddSource(windowed_source, 1, poligon::StreamingMode::Background);
        streamer.AddSource(collisions, 1, poligon::StreamingMode::Immediate);
        streamer.SetSynchronous(true);
        const glm::dvec3 corner(-200.0, 0.0, -200.0);
        streamer.Update(corner);
        streamer.RequirePatches({corner});
        Expect(windowed.GetPatchCount() == 4 && collisions.GetBodyCount() == 4, "at the land's corner the window holds 2 x 2 patches");
        const poligon::RayHit near_hit = physics.CastRay({-150.0, 500.0, -150.0}, {0.0f, -1.0f, 0.0f}, 1000.0f);
        const poligon::RayHit far_hit = physics.CastRay({150.0, 500.0, 150.0}, {0.0f, -1.0f, 0.0f}, 1000.0f);
        Expect(near_hit.hit && std::fabs(near_hit.position.y - whole.GetHeight(-150.0, -150.0)) < 0.05,
               "land inside the window collides at its height");
        Expect(!far_hit.hit, "land outside the window has no collisions");
        // Rays straddling the border between patches (0, 0) and (1, 0) at x = -128.
        double worst_ray = 0.0;
        for (int i = 0; i < 20; ++i) {
            const double x = -128.0 + (i - 10) * 0.05;
            const poligon::RayHit hit = physics.CastRay({x, 500.0, -180.0 + i}, {0.0f, -1.0f, 0.0f}, 1000.0f);
            worst_ray = hit.hit ? std::max(worst_ray, std::fabs(hit.position.y - whole.GetHeight(x, -180.0 + i))) : 1e9;
        }
        std::printf("  rays across a patch border: largest error %.4f m\n", worst_ray);
        Expect(worst_ray < 0.05, "collisions are continuous across patch borders");

        poligon::Camera wide = above;
        wide.position = {0.0, 700.0, 0.0};
        const poligon::Image windowed_picture = Draw(renderer, windowed, wide);
        Expect(windowed.GetStats().patches > 0, "the window's patches are drawn");
        // Beyond the window the land is drawn from the coarse background heights (16 m here).
        const poligon::Image whole_picture = Draw(renderer, whole, wide);
        int background_differs = 0;
        for (size_t i = 0; i < whole_picture.pixels.size(); i += 4) {
            background_differs += std::abs(static_cast<int>(whole_picture.pixels[i + 1]) - static_cast<int>(windowed_picture.pixels[i + 1])) > 16 ? 1 : 0;
        }
        const double background_share = 100.0 * background_differs / (whole_picture.width * whole_picture.height);
        std::printf("  far background: %d squares, %.2f%% of pixels differ from the whole land by more than 16\n",
                    windowed.GetStats().background_patches, background_share);
        Expect(windowed.GetStats().background_patches > 0 && background_share < 5.0,
               "beyond the window the background draws the land, coarse but in place");
        windowed.SetBackgroundVisible(false);
        Draw(renderer, windowed, wide);
        Expect(windowed.GetStats().background_patches == 0, "the background can be switched off");
        windowed.SetBackgroundVisible(true);
        // Move the window to the opposite corner: the old patches go, drawing and collisions with them.
        const glm::dvec3 other(200.0, 0.0, 200.0);
        streamer.Update(other);
        streamer.RequirePatches({other});
        const poligon::RayHit old_hit = physics.CastRay({-150.0, 500.0, -150.0}, {0.0f, -1.0f, 0.0f}, 1000.0f);
        Expect(!windowed.HasPatch({0, 0}) && windowed.HasPatch({3, 3}) && collisions.GetBodyCount() == 4 && !old_hit.hit,
               "a patch that leaves the window is neither drawn nor collides");
    }
    // --- The other sources: water, plants, colliders, sky occlusion, ground cover ---
    {
        poligon::StreamingGridSettings world_grid = info.grid;
        world_grid.origin = {origin.x, origin.z};
        const poligon::StreamingGrid all_grid(world_grid);
        const poligon::MeshHandle cube = renderer.CreateCubeMesh();
        poligon::Material leaf;
        leaf.color = {0.1f, 0.5f, 0.1f, 1.0f};

        // A kind of plant: a 9 m box with a trunk collider, planted by a rule.
        poligon::Vegetation whole_plants(32.0f);
        poligon::Vegetation patch_plants(32.0f);
        whole_plants.SetCellOrigin({origin.x, origin.z});
        patch_plants.SetCellOrigin({origin.x, origin.z});
        poligon::PlantKind kind;
        kind.settings.collider = poligon::PlantCollider::Trunk;
        poligon::PlantVariant variant;
        variant.species.levels.resize(1);
        variant.species.levels[0].parts.push_back({cube, leaf, glm::scale(glm::mat4(1.0f), {3.0f, 9.0f, 3.0f})});
        variant.species.levels[0].max_distance = 1000.0f;
        variant.height = 9.0f;
        variant.half_width = {1.5f, 1.5f};
        variant.species_id = whole_plants.AddSpecies(variant.species);
        patch_plants.AddSpecies(variant.species);
        kind.variants.push_back(variant);
        poligon::PlantRule rule;
        rule.spacing = 6.0f;
        rule.density = 0.01f;
        rule.conditions.slope_degrees = {0.0f, 25.0f, 5.0f};
        const glm::vec2 middle(256.0f, 256.0f);
        const poligon::Planter planter = [&](const poligon::LandPatch& patch, glm::vec2 low, glm::vec2 high,
                                             std::vector<poligon::PlacedPlant>& plants) {
            for (const auto& [index, placement] : poligon::PlacePlantGroupInRectangle(patch.maps, {&rule}, middle, 1000.0f, low, high, {1})) {
                poligon::PlacedPlant plant;
                plant.kind = &kind;
                plant.foot = origin + glm::dvec3(placement.position.x, placement.height, placement.position.y);
                plant.turn = placement.turn;
                plant.scale = placement.scale;
                plants.push_back(plant);
            }
        };
        int whole_count = 0;
        for (const auto& [index, placement] : poligon::PlacePlantGroup(maps, {&rule}, middle, 1000.0f, {1})) {
            whole_plants.AddInstance(variant.species_id,
                                     poligon::Transform(origin + glm::dvec3(placement.position.x, placement.height, placement.position.y),
                                                        glm::angleAxis(placement.turn, glm::vec3(0.0f, 1.0f, 0.0f)),
                                                        glm::vec3(placement.scale)));
            ++whole_count;
        }

        poligon::PhysicsWorld physics;
        poligon::PlantPatchSource plants(patch_plants, land, planter);
        poligon::PlantColliderSource colliders(physics, land, planter);
        poligon::WaterPatchSource water(renderer, land);
        poligon::SkyOcclusionPatchSource sky(land, 3, 4.0f, 120.0f);  // reach within a 128 m patch's neighbors
        poligon::WorldStreamer streamer(all_grid);
        streamer.AddSource(plants, 3, poligon::StreamingMode::Background);
        streamer.AddSource(water, 3, poligon::StreamingMode::Background);
        streamer.AddSource(sky, 3, poligon::StreamingMode::Background);
        streamer.AddSource(colliders, 3, poligon::StreamingMode::Immediate);
        streamer.Update({0.0, 0.0, 0.0});
        streamer.FinishLoading();
        streamer.RequirePatches({{0.0, 0.0, 0.0}});
        std::printf("  plants: %d from the whole land, %d from patches, %d trunks\n", whole_count, patch_plants.GetInstanceCount(),
                    colliders.GetBodyCount());
        Expect(whole_count > 50 && patch_plants.GetInstanceCount() == whole_count && colliders.GetBodyCount() == whole_count,
               "plants and their trunks from patches are the whole land's plants");

        // Plants seen across patch borders look the same.
        poligon::DirectionalLight sun;
        sun.direction = {-0.5f, -0.7f, -0.3f};
        poligon::Environment environment;
        environment.shadows = false;
        const auto picture = [&](poligon::Vegetation& vegetation) {
            renderer.BeginFrame(across, sun, environment);
            whole.Draw(across);
            vegetation.Draw(renderer, across);
            renderer.EndFrame();
            poligon::Image frame;
            renderer.CaptureFrame(frame);
            return frame;
        };
        const poligon::Image whole_picture = picture(whole_plants);
        const poligon::Image patch_picture = picture(patch_plants);
        int plants_differ = 0;
        for (size_t i = 0; i < whole_picture.pixels.size(); i += 4) {
            plants_differ += std::abs(static_cast<int>(whole_picture.pixels[i + 1]) - static_cast<int>(patch_picture.pixels[i + 1])) > 2 ? 1 : 0;
        }
        Expect(plants_differ == 0, "plants from patches stand exactly where the whole land's do, also at patch borders");

        // Water: the same squares as the whole land's lakes.
        const std::vector<poligon::WaterSurface> lakes = poligon::CreateLakeSurfaces(renderer, maps, origin);
        const auto water_picture = [&](bool from_patches) {
            renderer.BeginFrame(above, sun, environment);
            whole.Draw(above);
            if (from_patches) {
                water.Draw(renderer, poligon::WaterMaterial{});
            } else {
                for (const poligon::WaterSurface& lake : lakes) {
                    renderer.DrawWater(lake.mesh, lake.origin, glm::mat4(1.0f), poligon::WaterMaterial{});
                }
            }
            renderer.EndFrame();
            poligon::Image frame;
            renderer.CaptureFrame(frame);
            return frame;
        };
        const poligon::Image whole_water = water_picture(false);
        const poligon::Image patch_water = water_picture(true);
        int water_differs = 0;
        for (size_t i = 0; i < whole_water.pixels.size(); i += 4) {
            water_differs += std::abs(static_cast<int>(whole_water.pixels[i + 2]) - static_cast<int>(patch_water.pixels[i + 2])) > 2 ? 1 : 0;
        }
        std::printf("  water: %zu lakes, %d surfaces from patches, %d pixels differ\n", lakes.size(), water.GetSurfaceCount(), water_differs);
        Expect(!lakes.empty() && water.GetSurfaceCount() >= static_cast<int>(lakes.size()) && water_differs == 0,
               "water from patches covers the same squares as the whole land's lakes");

        // Sky occlusion of the land: the joined patches equal the map made from the whole terrain.
        const poligon::SkyOcclusionMap whole_sky =
            poligon::MakeTerrainSkyOcclusion(whole, glm::dvec2(origin.x, origin.z) + glm::dvec2(254.0), 508.0f, 4.0f, 120.0f);
        const poligon::GroundMap joined = sky.GetGroundMap();
        bool sky_same = joined.resolution == whole_sky.resolution && joined.values != nullptr;
        int sky_differ = 0;
        for (int i = 0; sky_same && i < whole_sky.resolution * whole_sky.resolution; ++i) {
            sky_differ += joined.values[i] != whole_sky.values[static_cast<size_t>(i)] ? 1 : 0;
        }
        sky_same = sky_same && sky_differ == 0;
        std::printf("  sky occlusion: %d x %d points joined, %d x %d whole, %d differ\n", joined.resolution, joined.resolution,
                    whole_sky.resolution, whole_sky.resolution, sky_differ);
        Expect(sky_same, "sky occlusion joined from patches equals the whole land's, point for point");

        // With the crowns of the plants: each patch adds those on it and around it. Bytes are
        // rounded after each crown, so the order of crowns may change a value by one or two.
        poligon::SkyOcclusionPatchSource sky_with_crowns(land, 3, 4.0f, 120.0f);
        sky_with_crowns.SetCanopy(planter);
        {
            poligon::WorldStreamer crown_streamer(all_grid);
            crown_streamer.AddSource(sky_with_crowns, 3, poligon::StreamingMode::Background);
            crown_streamer.Update({0.0, 0.0, 0.0});
            crown_streamer.FinishLoading();
        }
        poligon::SkyOcclusionMap whole_crowns = whole_sky;
        for (const auto& [index, placement] : poligon::PlacePlantGroup(maps, {&rule}, middle, 1000.0f, {1})) {
            poligon::AddCanopy(whole_crowns, glm::dvec2(origin.x + placement.position.x, origin.z + placement.position.y),
                               1.5f * placement.scale * 0.9f, 0.55f);
        }
        const poligon::GroundMap with_crowns = sky_with_crowns.GetGroundMap();
        int crowns_largest = 0;
        int crowns_changed = 0;
        for (int i = 0; i < whole_crowns.resolution * whole_crowns.resolution; ++i) {
            crowns_largest = std::max(crowns_largest, std::abs(static_cast<int>(with_crowns.values[i]) - static_cast<int>(whole_crowns.values[static_cast<size_t>(i)])));
            crowns_changed += whole_crowns.values[static_cast<size_t>(i)] != whole_sky.values[static_cast<size_t>(i)] ? 1 : 0;
        }
        std::printf("  sky occlusion with crowns: %d points under crowns, largest difference %d of 255\n", crowns_changed, crowns_largest);
        Expect(crowns_changed > 100 && crowns_largest <= 2, "crowns near patch borders darken the sky on both sides, as on the whole land");

        // Ground cover on the streamed terrain with the land's maps grows the same plants.
        poligon::GroundCoverLayer layer;
        layer.species = variant.species;
        layer.species.levels[0].max_distance = 60.0f;
        layer.density = 0.3f;
        layer.conditions.slope_degrees = {0.0f, 20.0f, 4.0f};
        poligon::GroundCover whole_cover(whole, 60.0f);
        whole_cover.SetMaps(&maps, -glm::dvec2(origin.x, origin.z));
        whole_cover.AddLayer(layer);
        poligon::GroundCover streamed_cover(streamed, 60.0f);
        streamed_cover.SetLand(&land);
        streamed_cover.AddLayer(layer);
        const glm::dvec3 standing(-128.0, 0.0, -128.0);  // on a corner of four patches
        whole_cover.Update(standing);
        streamed_cover.Update(standing);
        std::vector<glm::mat4> whole_tufts;
        std::vector<glm::mat4> streamed_tufts;
        whole_cover.CollectInstances(0, whole_tufts);
        streamed_cover.CollectInstances(0, streamed_tufts);
        bool cover_same = !whole_tufts.empty() && whole_tufts.size() == streamed_tufts.size();
        for (size_t i = 0; cover_same && i < whole_tufts.size(); ++i) {
            cover_same = whole_tufts[i] == streamed_tufts[i];
        }
        std::printf("  ground cover: %zu tufts from the whole land, %zu from patches\n", whole_tufts.size(), streamed_tufts.size());
        Expect(cover_same, "ground cover on a streamed terrain grows the whole land's plants, across patch borders too");
    }

    // --- Trampled paths are remembered outside the grid ---
    {
        poligon::TrampleMap trample(64.0f, 64);
        trample.SetRememberOutside(true);
        trample.Recenter({0.0, 0.0});
        trample.Trample({5.0, 5.0}, 2.0f, 1.0f);
        const float trodden = trample.GetValue({5.0, 5.0});
        trample.Recenter({1000.0, 0.0});
        trample.Recover(0.25f);
        trample.Recenter({0.0, 0.0});
        const float back = trample.GetValue({5.0, 5.0});
        std::printf("  trample: %.4f trodden, %.4f on return, %d cells kept outside\n", static_cast<double>(trodden), static_cast<double>(back),
                    trample.GetRememberedCount());
        Expect(trodden > 0.5f && std::fabs(back - (trodden - 0.25f)) < 1e-6f,
               "a path left behind is there again on return, less what it recovered meanwhile");
    }
    return test::ExitCode();
}
