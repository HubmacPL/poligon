// Example: land from a height map, drawn with levels of detail, with physics on it, then
// colored by ground layers named by number at each map point (the layer index mode).
// Usage: example_terrain OUTPUT.png   (also writes OUTPUT_indices.png)
#include <cstdio>
#include <string>

#include "poligon/world/height_map.h"
#include "poligon/physics/physics.h"
#include "poligon/render/renderer.h"
#include "poligon/world/terrain.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_terrain OUTPUT.png\n");
        return 2;
    }

    // A height map: a grid of heights. Here 1025 x 1025 points, 2 m apart, made from noise:
    // hills about 400 m across and up to 90 m high. Real land comes from a file instead
    // (LoadHeightMap; see docs/en/terrain.md for where such files come from).
    const poligon::HeightMap map = poligon::MakeNoiseHeightMap(1025, 1025, 2.0f, 11, 400.0f, 90.0f);

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.post.exposure = 0.5f;
    poligon::Renderer renderer(config);

    // The terrain: where the map's first point is in the world, and how the land looks.
    // Without textures the two ground layers are plain white, tinted here by the light only.
    poligon::TerrainConfig terrain_config;
    terrain_config.origin = {-1024.0f, 0.0f, -1024.0f};  // the map's middle at the world's origin
    terrain_config.material.steep_color = {0.45f, 0.36f, 0.24f, 1.0f};
    terrain_config.material.steep_start_degrees = 20.0f;
    terrain_config.material.steep_end_degrees = 32.0f;
    poligon::Terrain terrain(renderer, map, terrain_config);

    // The same heights as ground for physics. It takes a square grid.
    poligon::PhysicsWorld physics;
    physics.AddHeightField(map.heights.data(), map.width, map.spacing, terrain_config.origin);

    // Asking the land: how high is it, how steep, and does physics agree?
    const double height = terrain.GetHeight(0.0, 0.0);
    const poligon::RayHit hit = physics.CastRay({0.0f, 500.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 1000.0f);
    std::printf("land at the origin: %.2f m high, slope %.1f degrees; a ray from above meets it at %.2f m\n",
                static_cast<double>(height), static_cast<double>(terrain.GetSlopeDegrees(0.0f, 0.0f)),
                static_cast<double>(hit.position.y));

    // Stand 2 m above the land and look across it.
    poligon::Camera camera;
    camera.position = {0.0f, height + 30.0f, 0.0f};
    camera.target = {300.0f, height - 20.0f, -200.0f};
    camera.far_plane = 4000.0f;
    poligon::DirectionalLight sun;
    sun.direction = {-0.6f, -0.5f, 0.3f};
    sun.color = {2.8f, 2.6f, 2.3f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    environment.shadow_distance = 600.0f;
    environment.fog_density = 0.0006f;

    renderer.BeginFrame(camera, sun, environment);
    // Every frame: choose patches by distance from the camera and hand them to the renderer.
    terrain.Draw(camera);
    renderer.EndFrame();

    const poligon::TerrainStats& stats = terrain.GetStats();
    std::printf("%d patches: %d of the finest size, %d, %d, %d of the next larger ones\n", stats.patches, stats.patches_per_level[0],
                stats.patches_per_level[1], stats.patches_per_level[2], stats.patches_per_level[3]);

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);

    // Ground layers by number (TerrainMaterial::layer_indices), for more than the eight layers
    // weights can hold or for maps made elsewhere as "material number per point". Here 12
    // layers of plain color, one per 8 m of height; each point names the layer of its
    // height band and the next one up, weighted by where it lies in the band.
    constexpr int kLayers = 12;
    poligon::Image colors[kLayers];
    for (int layer = 0; layer < kLayers; ++layer) {
        colors[layer] = poligon::MakeSolidImage(4, 4, static_cast<std::uint8_t>(40 + layer * 18), static_cast<std::uint8_t>(200 - layer * 12),
                                                static_cast<std::uint8_t>(60 + layer * 8));
    }
    poligon::Image indices = poligon::MakeSolidImage(map.width, map.depth, 0, 0, 0, 0);
    poligon::Image weights = poligon::MakeSolidImage(map.width, map.depth, 0, 0, 0, 0);
    for (size_t i = 0; i < map.heights.size(); ++i) {
        const float band = glm::clamp(map.heights[i] / 8.0f, 0.0f, static_cast<float>(kLayers - 1));
        const int below = static_cast<int>(band);
        const float up = band - static_cast<float>(below);
        indices.pixels[i * 4] = static_cast<std::uint8_t>(below);  // red: a layer's number
        indices.pixels[i * 4 + 1] = static_cast<std::uint8_t>(glm::min(below + 1, kLayers - 1));
        weights.pixels[i * 4] = static_cast<std::uint8_t>((1.0f - up) * 255.0f);  // red: how much of it
        weights.pixels[i * 4 + 1] = static_cast<std::uint8_t>(up * 255.0f);
    }
    poligon::TextureOptions data;  // numbers and weights, not colors: no sRGB, no mipmaps
    data.is_color = false;
    data.generate_mipmaps = false;
    poligon::TerrainMaterial material = terrain_config.material;
    material.colors = renderer.CreateTextureArray(colors, kLayers);
    material.layer_count = kLayers;
    material.layer_indices = renderer.CreateTexture(indices, data);
    material.layer_weights = renderer.CreateTexture(weights, data);
    terrain.SetMaterial(material);

    renderer.BeginFrame(camera, sun, environment);
    terrain.Draw(camera);
    renderer.EndFrame();
    std::string indexed_path = argv[1];
    indexed_path.insert(indexed_path.size() >= 4 ? indexed_path.size() - 4 : indexed_path.size(), "_indices");
    if (!renderer.SaveFrameToPng(indexed_path.c_str())) {
        std::fprintf(stderr, "cannot write %s\n", indexed_path.c_str());
        return 1;
    }
    std::printf("saved %s\n", indexed_path.c_str());
    return 0;
}
