// Example: a streamed world from a height map made elsewhere (real elevation data, a game's
// own maps), without the terrain generator. The map is cut into 256 m patches
// (LandFromHeightMap, poligon/world/land.h); each patch's ground comes from the game, here as
// layer indices by height; a WorldStreamer loads the terrain and its collisions around the
// camera. A game would cook the land once (CookLand) and read it with LandFromFiles.
// Usage: example_height_map_land OUTPUT.png
#include <cstdio>

#include "poligon/physics/physics.h"
#include "poligon/render/renderer.h"
#include "poligon/world/height_map.h"
#include "poligon/world/land.h"
#include "poligon/world/streaming.h"
#include "poligon/world/terrain.h"

namespace {

// The game's ground at one point: layer numbers and weights (here by height, from 8 layers,
// one band per 12 m, with the next layer mixed in towards each band's ends). Strongest first,
// the weights adding up to 255; an unused channel has weight 0.
void SetGround(float height, std::uint8_t* index, std::uint8_t* weight) {
    const float band = glm::clamp(height / 12.0f, 0.0f, 7.0f);
    const int below = static_cast<int>(band);
    const float up = band - static_cast<float>(below);
    index[0] = static_cast<std::uint8_t>(up < 0.5f ? below : glm::min(below + 1, 7));
    index[1] = static_cast<std::uint8_t>(up < 0.5f ? glm::min(below + 1, 7) : below);
    weight[0] = static_cast<std::uint8_t>(glm::max(up, 1.0f - up) * 255.0f + 0.5f);
    weight[1] = static_cast<std::uint8_t>(255 - weight[0]);
}

// Each patch's ground, at its ground points. Called from the loading thread.
class GroundByHeight : public poligon::PatchGroundSource {
public:
    explicit GroundByHeight(const poligon::HeightMap& map) : map_(map) {}

    void GetGround(poligon::PatchCoord patch, poligon::LandPatchGround& ground) const override {
        const int points = 65;  // 256 m patch / 4 m ground spacing + 1
        ground.layer_indices = poligon::MakeSolidImage(points, points, 0, 0, 0, 0);
        ground.layer_weights = poligon::MakeSolidImage(points, points, 0, 0, 0, 0);
        for (int z = 0; z < points; ++z) {
            for (int x = 0; x < points; ++x) {
                const float height = map_.GetHeight(static_cast<float>(patch.x * 256 + x * 4), static_cast<float>(patch.z * 256 + z * 4));
                const size_t pixel = (static_cast<size_t>(z) * points + static_cast<size_t>(x)) * 4;
                SetGround(height, &ground.layer_indices.pixels[pixel], &ground.layer_weights.pixels[pixel]);
            }
        }
    }

private:
    const poligon::HeightMap& map_;
};

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_height_map_land OUTPUT.png\n");
        return 2;
    }
    // 2 x 2 km, a height every 4 m (a game would load its own: LoadHeightMap, LoadTiffHeightMap).
    const poligon::HeightMap map = poligon::MakeNoiseHeightMap(513, 513, 4.0f, 3, 500.0f, 90.0f);
    const GroundByHeight ground(map);
    const glm::dvec3 origin(-1024.0, 0.0, -1024.0);
    // Patches of 256 m, ground points 4 m apart, background points every 64 m.
    poligon::LandFromHeightMap land(map, 256.0, origin, &ground, 4.0f, 64.0f);

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.post.exposure = 0.5f;
    poligon::Renderer renderer(config);
    poligon::Image colors[8];
    for (int layer = 0; layer < 8; ++layer) {
        colors[layer] = poligon::MakeSolidImage(4, 4, static_cast<std::uint8_t>(60 + layer * 24), static_cast<std::uint8_t>(170 - layer * 10),
                                                static_cast<std::uint8_t>(70 + layer * 12));
    }
    poligon::TerrainConfig terrain_config;
    terrain_config.material.colors = renderer.CreateTextureArray(colors, 8);
    terrain_config.material.layer_count = 8;
    // The far background, where no patch is held, takes its ground from the material: layer
    // indices over the whole land, here at the background's points (without them layer 0
    // covers it).
    const poligon::HeightMap& background = land.GetInfo().background;
    poligon::Image background_indices = poligon::MakeSolidImage(background.width, background.depth, 0, 0, 0, 0);
    poligon::Image background_weights = poligon::MakeSolidImage(background.width, background.depth, 0, 0, 0, 0);
    for (size_t i = 0; i < background.heights.size(); ++i) {
        SetGround(background.heights[i], &background_indices.pixels[i * 4], &background_weights.pixels[i * 4]);
    }
    poligon::TextureOptions data;  // numbers and weights, not colors: no sRGB, no mipmaps
    data.is_color = false;
    data.generate_mipmaps = false;
    terrain_config.material.layer_indices = renderer.CreateTexture(background_indices, data);
    terrain_config.material.layer_weights = renderer.CreateTexture(background_weights, data);
    poligon::Terrain terrain(renderer, land.GetInfo(), terrain_config, 16);
    poligon::TerrainPatchSource terrain_source(terrain, land);
    poligon::PhysicsWorld physics;
    poligon::TerrainPhysicsSource collisions(physics, land);

    poligon::StreamingGridSettings grid_settings = land.GetInfo().grid;
    grid_settings.origin = {origin.x, origin.z};
    const poligon::StreamingGrid grid(grid_settings);
    poligon::WorldStreamer streamer(grid);
    streamer.AddSource(terrain_source, 1, poligon::StreamingMode::Background);
    streamer.AddSource(collisions, 1, poligon::StreamingMode::Immediate);
    const glm::dvec3 player(0.0, 0.0, 0.0);
    streamer.Update(player);
    streamer.FinishLoading();
    streamer.RequirePatches({player});
    const double ground_height = terrain.GetHeight(player.x, player.z);
    std::printf("%d patches held, %d with collisions; land under the player at %.1f m\n", terrain.GetPatchCount(),
                collisions.GetBodyCount(), ground_height);

    poligon::Camera camera;
    camera.position = {0.0, ground_height + 60.0, 0.0};
    camera.target = {400.0, ground_height, -300.0};
    camera.far_plane = 4000.0f;
    poligon::DirectionalLight sun;
    sun.direction = {-0.6f, -0.5f, 0.3f};
    sun.color = {2.8f, 2.6f, 2.3f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    renderer.BeginFrame(camera, sun, environment);
    terrain.Draw(camera);  // held patches near, the background beyond them
    renderer.EndFrame();
    std::printf("%d squares from patches, %d from the far background\n", terrain.GetStats().patches,
                terrain.GetStats().background_patches);
    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}
