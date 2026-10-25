// Checks the asset system: block compression (sizes, levels, repeatable bytes, the file
// format), the asset list (and its error messages), cooking only what changed, loading by
// name from the cooked folder and from an archive, drawing a cooked textured model, a cooked
// animated model whose clips come built (an ozz-animation archive) and play as the file's
// own, plant variants split from one model file, and the level file.
// Usage: assets_test d3d12|vulkan OUTPUT_DIRECTORY
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "poligon/animation/animation.h"
#include "poligon/animation/animation_build.h"
#include "poligon/assets/assets.h"
#include "poligon/assets/cooking.h"
#include "poligon/assets/model.h"
#include "poligon/assets/model_import.h"
#include "poligon/world/level.h"
#include "poligon/world/plants.h"
#include "poligon/render/renderer.h"
#include "poligon/core/texture_compression.h"
#include "test_support.h"

namespace {

namespace fs = std::filesystem;

void WriteText(const fs::path& path, const char* text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
}

// Two unit cubes 10 m apart along x, both using the material with the texture.
const char* kTwoCubesObj = R"(mtllib cubes.mtl
o first
v -0.5 0 -0.5
v 0.5 0 -0.5
v 0.5 0 0.5
v -0.5 0 0.5
v -0.5 1 -0.5
v 0.5 1 -0.5
v 0.5 1 0.5
v -0.5 1 0.5
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vn 0 1 0
usemtl painted
f 5/1/1 8/4/1 7/3/1 6/2/1
f 1/1/1 2/2/1 3/3/1 4/4/1
f 1/1/1 5/1/1 6/2/1 2/2/1
f 3/3/1 7/3/1 8/4/1 4/4/1
f 2/2/1 6/2/1 7/3/1 3/3/1
f 4/4/1 8/4/1 5/1/1 1/1/1
o second
v 9.5 0 -0.5
v 10.5 0 -0.5
v 10.5 0 0.5
v 9.5 0 0.5
v 9.5 1 -0.5
v 10.5 1 -0.5
v 10.5 1 0.5
v 9.5 1 0.5
usemtl painted
f 13/1/1 16/4/1 15/3/1 14/2/1
f 9/1/1 10/2/1 11/3/1 12/4/1
f 9/1/1 13/1/1 14/2/1 10/2/1
f 11/3/1 15/3/1 16/4/1 12/4/1
f 10/2/1 14/2/1 15/3/1 11/3/1
f 12/4/1 16/4/1 13/1/1 9/1/1
)";

const char* kCubesMtl = R"(newmtl painted
Kd 1 1 1
map_Kd paint.png
Ke 0 0.5 1
map_Ke paint.png
)";

// A skinned triangle with two joints, "root" and "arm" (1 m up; the loader adds the scene's
// root above them), and one clip, "lift", that turns the arm by 90 degrees about z in a
// second. Its data is in MakeArmBin's layout.
const char* kArmGltf = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0, 1]}],
  "nodes": [
    {"name": "body", "mesh": 0, "skin": 0},
    {"name": "root", "children": [2]},
    {"name": "arm", "translation": [0, 1, 0]}
  ],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2}, "indices": 3}]}],
  "skins": [{"joints": [1, 2], "inverseBindMatrices": 4}],
  "animations": [{"name": "lift", "channels": [{"sampler": 0, "target": {"node": 2, "path": "rotation"}}],
                  "samplers": [{"input": 5, "output": 6, "interpolation": "LINEAR"}]}],
  "buffers": [{"uri": "arm.bin", "byteLength": 272}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 12},
    {"buffer": 0, "byteOffset": 48, "byteLength": 48},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6},
    {"buffer": 0, "byteOffset": 104, "byteLength": 128},
    {"buffer": 0, "byteOffset": 232, "byteLength": 8},
    {"buffer": 0, "byteOffset": 240, "byteLength": 32}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 2, 0]},
    {"bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"},
    {"bufferView": 4, "componentType": 5126, "count": 2, "type": "MAT4"},
    {"bufferView": 5, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [0], "max": [1]},
    {"bufferView": 6, "componentType": 5126, "count": 2, "type": "VEC4"}
  ]
})";

// The binary part of kArmGltf: positions, joints, weights, indices, inverse bind matrices,
// key times, key rotations.
std::vector<std::uint8_t> MakeArmBin() {
    std::vector<std::uint8_t> bytes;
    const auto floats = [&bytes](std::initializer_list<float> values) {
        for (const float value : values) {
            const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
            bytes.insert(bytes.end(), raw, raw + sizeof(float));
        }
    };
    floats({0, 0, 0, 0, 2, 0, 1, 0, 0});                                // positions
    bytes.insert(bytes.end(), {0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0});    // joints
    floats({1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});                       // weights
    bytes.insert(bytes.end(), {0, 0, 1, 0, 2, 0, 0, 0});                // indices and 2 bytes of padding
    floats({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});           // root: identity
    floats({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1});          // arm: 1 m down
    floats({0, 1});                                                     // key times
    floats({0, 0, 0, 1, 0, 0, 0.70710678f, 0.70710678f});              // key rotations
    return bytes;
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;
    poligon::GraphicsBackend backend = poligon::GraphicsBackend::Default;
    if (argc == 3 && std::strcmp(argv[1], "d3d12") == 0) {
        backend = poligon::GraphicsBackend::Direct3D12;
    } else if (argc == 3 && std::strcmp(argv[1], "vulkan") == 0) {
        backend = poligon::GraphicsBackend::Vulkan;
    } else {
        std::printf("usage: assets_test d3d12|vulkan OUTPUT_DIRECTORY\n");
        return 2;
    }
    const fs::path root = fs::path(argv[2]) / (std::string("assets_test_") + argv[1]);
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path source = root / "source";
    const fs::path cooked = root / "cooked";
    fs::create_directories(source, ec);
    std::string error;

    // --- Block compression ---
    {
        const poligon::Image red = poligon::MakeSolidImage(64, 32, 255, 0, 0);
        poligon::TextureCompressionOptions options;
        options.format = poligon::TextureFormat::Bc1;
        const poligon::CompressedImage first = poligon::CompressImage(red, options);
        const poligon::CompressedImage second = poligon::CompressImage(red, options);
        Expect(first.levels.size() == 4, "a 64 x 32 chain ends at 4 x 4: 64x32, 32x16, 16x8, 8x4");
        Expect(first.levels[0].size() == 16 * 8 * 8 && first.levels[3].size() == 2 * 1 * 8, "BC1 keeps 8 bytes per 4 x 4 block");
        Expect(first.levels == second.levels, "the same image gives the same bytes");
        // A solid block stores the color as 5-6-5 bits: pure red is 0xF800.
        const std::uint16_t color0 = static_cast<std::uint16_t>(first.levels[0][0] | (first.levels[0][1] << 8));
        Expect(color0 == 0xF800, "a red block stores red");
        options.format = poligon::TextureFormat::Bc5;
        options.is_color = false;
        Expect(poligon::CompressImage(red, options).levels[0].size() == 16 * 8 * 16, "BC5 keeps 16 bytes per block");

        const fs::path file = root / "red.ptex";
        Expect(poligon::SaveCompressedImage(file.string().c_str(), first), "a compressed image is saved");
        poligon::CompressedImage loaded;
        Expect(poligon::LoadCompressedImage(file.string().c_str(), loaded, error) && loaded.levels == first.levels &&
                   loaded.width == 64 && loaded.height == 32 && loaded.format == poligon::TextureFormat::Bc1,
               "and loads back the same");
    }

    // --- The asset list ---
    {
        poligon::AssetList list;
        Expect(!poligon::ParseAssetList("poligon-assets 1\ntexture a\nformat bc9\nsource x.png\n", list, error) &&
                   error.find("line 3") != std::string::npos,
               "an unknown format is an error that names its line");
        Expect(!poligon::ParseAssetList("poligon-assets 1\ntexture a\n", list, error), "an asset without a source is an error");
        Expect(!poligon::ParseAssetList("poligon-assets 1\ntexture a\nsource x\ntexture a\nsource y\n", list, error),
               "a name used twice is an error");
        Expect(!poligon::ParseAssetList("poligon-assets 1\nmodel b\nsource m.obj\nformat bc1\n", list, error),
               "a texture setting on a model is an error");
        Expect(!poligon::ParseAssetList("poligon-assets 1\ntexture ../escape\nsource x\n", list, error), "names cannot leave the folder");
    }

    // --- Cooking: a texture and a model with two cubes ---
    poligon::SaveImageToPng((source / "paint.png").string().c_str(), poligon::MakeSolidImage(16, 16, 40, 200, 90));
    WriteText(source / "cubes.obj", kTwoCubesObj);
    WriteText(source / "cubes.mtl", kCubesMtl);
    const fs::path list_path = root / "assets.txt";
    WriteText(list_path, "poligon-assets 1\n\ntexture ground/paint\nsource paint.png\nformat bc1\n\nmodel things/cubes\nsource cubes.obj\n");
    poligon::AssetList list;
    Expect(poligon::LoadAssetList(list_path.string().c_str(), list, error) && list.entries.size() == 2, "the asset list loads");
    const poligon::CookReport first_cook =
        poligon::CookAssets(list, list_path.string().c_str(), source.string().c_str(), cooked.string().c_str());
    if (!first_cook.errors.empty()) {
        std::printf("  %s\n", first_cook.errors[0].c_str());
    }
    Expect(first_cook.cooked == 2 && first_cook.errors.empty(), "both assets are cooked");
    Expect(fs::exists(cooked / "ground" / "paint.ptex") && fs::exists(cooked / "things" / "cubes.pmodel"),
           "cooked files are named after the assets");
    const poligon::CookReport second_cook =
        poligon::CookAssets(list, list_path.string().c_str(), source.string().c_str(), cooked.string().c_str());
    Expect(second_cook.cooked == 0 && second_cook.up_to_date == 2, "nothing is cooked again when nothing changed");
    const fs::path archive = root / "assets.parchive";
    Expect(poligon::PackAssets(cooked.string().c_str(), archive.string().c_str(), error), "the cooked folder packs into an archive");

    // --- Loading by name, from the folder and from the archive, and drawing ---
    poligon::RendererConfig config;
    config.width = 64;
    config.height = 64;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.post.tone_mapping = poligon::ToneMapping::None;
    {
        poligon::Renderer renderer(config);
        for (const fs::path& place : {cooked, archive}) {
            poligon::AssetLibrary library;
            const std::string where = place == cooked ? "folder" : "archive";
            Expect(library.Open(place.string().c_str(), error), ("the " + where + " opens").c_str());
            Expect(library.Exists("things/cubes") && library.Exists("ground/paint") && !library.Exists("things/none"),
                   ("assets are found by name in the " + where).c_str());
            Expect(library.LoadTexture(renderer, "ground/paint", error).id != 0, ("a texture loads from the " + where).c_str());
            const poligon::LoadedModel* model = library.LoadModel(renderer, "things/cubes", error);
            // The OBJ reader adds a default material before the file's own.
            bool textured = false;
            for (size_t m = 0; model != nullptr && m < model->render.materials.size(); ++m) {
                const poligon::Material& material = model->render.materials[m];
                textured = textured || (model->model.materials[m].name == "painted" && material.texture.id != 0 &&
                                        material.emissive_texture.id != 0 && material.emissive.b == 1.0f);
            }
            Expect(model != nullptr && model->model.instances.size() == 2 && textured,
                   ("a model with its textures and emission loads from the " + where).c_str());
            Expect(library.LoadModel(renderer, "things/cubes", error) == model, "a second load gives the same model");
            Expect(library.LoadModel(renderer, "things/none", error) == nullptr && !error.empty(), "a missing model is an error");
            if (model == nullptr) {
                continue;
            }

            // Plant variants: split, the two cubes are two variants; unsplit, one.
            poligon::Vegetation vegetation;
            poligon::PlantSettings settings;
            settings.split_variants = true;
            const poligon::PlantKind split = poligon::MakePlantKind(*model, settings, vegetation);
            settings.split_variants = false;
            const poligon::PlantKind whole = poligon::MakePlantKind(*model, settings, vegetation);
            Expect(split.variants.size() == 2 && whole.variants.size() == 1, "a model splits into its separate plants");
            Expect(std::fabs(split.variants[0].height - 1.0f) < 0.001f && std::fabs(whole.variants[0].half_width.x - 5.5f) < 0.001f,
                   "variants know their size");

            // Seen from above under even light, the cube's top shows the texture's green.
            poligon::Camera camera;
            camera.position = {0.0f, 3.0f, 0.0f};
            camera.target = {0.0f, 0.0f, 0.0f};
            camera.up = {0.0f, 0.0f, -1.0f};
            poligon::DirectionalLight light;
            light.color = {0.0f, 0.0f, 0.0f, 1.0f};
            light.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
            poligon::Environment environment;
            environment.shadows = false;
            // First without the material's emission, then with it alone, in darkness.
            poligon::RenderModel unlit_model = model->render;
            for (poligon::Material& material : unlit_model.materials) {
                material.emissive = {0.0f, 0.0f, 0.0f, 1.0f};
            }
            renderer.BeginFrame(camera, light, environment);
            poligon::DrawModel(renderer, unlit_model, poligon::Transform());
            renderer.EndFrame();
            poligon::Image frame;
            renderer.CaptureFrame(frame);
            const std::uint8_t* middle = &frame.pixels[static_cast<size_t>(32 * 64 + 32) * 4];
            if (std::abs(middle[0] - 40) > 8 || std::abs(middle[1] - 200) > 8 || std::abs(middle[2] - 90) > 8) {
                std::printf("  middle pixel %d %d %d, expected about 40 200 90\n", middle[0], middle[1], middle[2]);
            }
            Expect(std::abs(middle[0] - 40) <= 8 && std::abs(middle[1] - 200) <= 8 && std::abs(middle[2] - 90) <= 8,
                   "the cooked, compressed texture draws in its color");

            // In darkness only the emission shows: (0, 0.5, 1) times the same texture.
            light.ambient = {0.0f, 0.0f, 0.0f, 1.0f};
            renderer.BeginFrame(camera, light, environment);
            poligon::DrawModel(renderer, model->render, poligon::Transform());
            renderer.EndFrame();
            renderer.CaptureFrame(frame);
            const std::uint8_t* glow = &frame.pixels[static_cast<size_t>(32 * 64 + 32) * 4];
            const auto to_linear = [](int stored) {
                const float value = static_cast<float>(stored) / 255.0f;
                return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
            };
            const float green = 0.5f * to_linear(200);
            const float blue = to_linear(90);
            const float glow_green = to_linear(glow[1]);
            const float glow_blue = to_linear(glow[2]);
            if (glow[0] > 3 || std::abs(glow_green - green) > 0.03f || std::abs(glow_blue - blue) > 0.03f) {
                std::printf("  glowing middle pixel %d %d %d\n", glow[0], glow[1], glow[2]);
            }
            Expect(glow[0] <= 3 && std::abs(glow_green - green) <= 0.03f && std::abs(glow_blue - blue) <= 0.03f,
                   "the cooked emission glows in the dark through its texture");
        }
    }

    // --- A cooked animated model: its clips come built, and play as the file's own ---
    {
        WriteText(source / "arm.gltf", kArmGltf);
        const std::vector<std::uint8_t> arm_bin = MakeArmBin();
        std::ofstream(source / "arm.bin", std::ios::binary)
            .write(reinterpret_cast<const char*>(arm_bin.data()), static_cast<std::streamsize>(arm_bin.size()));
        const fs::path animated_list_path = root / "animated.txt";
        WriteText(animated_list_path, "poligon-assets 1\n\nmodel animals/arm\nsource arm.gltf\nlod arm.gltf\n");
        poligon::AssetList animated_list;
        const fs::path animated_cooked = root / "cooked_animated";
        Expect(poligon::LoadAssetList(animated_list_path.string().c_str(), animated_list, error), "the second list loads");
        const poligon::CookReport cook = poligon::CookAssets(animated_list, animated_list_path.string().c_str(),
                                                             source.string().c_str(), animated_cooked.string().c_str());
        if (!cook.errors.empty()) {
            std::printf("  %s\n", cook.errors[0].c_str());
        }
        Expect(cook.cooked == 1 && cook.errors.empty(), "the animated model is cooked");

        poligon::Model from_file;
        Expect(poligon::LoadModelFromFile((source / "arm.gltf").string().c_str(), from_file, error) &&
                   from_file.skeleton.joints.size() >= 2 && from_file.animations.size() == 1,
               "the animated test model has its joints and one clip");
        poligon::Renderer renderer(config);
        poligon::AssetLibrary library;
        Expect(library.Open(animated_cooked.string().c_str(), error), "the animated cooked folder opens");
        const poligon::LoadedModel* arm = library.LoadModel(renderer, "animals/arm", error);
        Expect(arm != nullptr && arm->animations != nullptr && arm->model.animations.empty(),
               "a cooked model's clips come built, not as keys");
        if (arm != nullptr && arm->animations != nullptr && !from_file.animations.empty()) {
            const poligon::AnimationSet& cooked_set = *arm->animations;
            Expect(cooked_set.GetClipCount() == 1 && std::string(cooked_set.GetClipName(0)) == from_file.animations[0].name &&
                       std::fabs(cooked_set.GetClipDuration(0) - 1.0f) < 1e-4f,
                   "the cooked clip keeps its name and length");
            const std::unique_ptr<poligon::AnimationSet> built_set = poligon::BuildAnimationSet(from_file.skeleton, from_file.animations);
            const poligon::AnimationSet& file_set = *built_set;
            poligon::AnimationPlayer cooked_player(cooked_set);
            poligon::AnimationPlayer file_player(file_set);
            cooked_player.Play(0);
            file_player.Play(0);
            bool same = true;
            bool moved = false;
            const std::vector<glm::mat4> start = cooked_player.GetJointMatrices();
            for (int tick = 0; tick < 50; ++tick) {
                cooked_player.Update(1.0f / 60.0f);
                file_player.Update(1.0f / 60.0f);
                const std::vector<glm::mat4>& a = cooked_player.GetJointMatrices();
                const std::vector<glm::mat4>& b = file_player.GetJointMatrices();
                same = same && a == b;
                moved = moved || a != start;
            }
            Expect(same && moved, "the cooked clip plays exactly as the one built from the file");
        }
        // Its simpler level (here the same file) is skinned too, so the two levels of its mesh
        // can be drawn as one group with one set of joint matrices.
        if (arm != nullptr && !arm->model.instances.empty()) {
            const poligon::MeshLodGroup group = poligon::MakeMeshLodGroup(*arm, 0, {20.0f, 80.0f});
            Expect(group.levels.size() == 2 && group.levels[1].mesh.id == arm->lod_meshes[0][0].id && group.levels[1].distance == 80.0f,
                   "a cooked skinned model's simpler level makes the second level of its group");
            const std::vector<glm::mat4> rest(arm->model.skeleton.joints.size(), glm::mat4(1.0f));
            poligon::Camera far_camera;
            far_camera.position = {0.0f, 1.0f, 50.0f};
            renderer.BeginFrame(far_camera, poligon::DirectionalLight{});
            const int level = renderer.DrawSkinnedMesh(group, glm::dvec3(0.0), glm::mat4(1.0f), arm->render.materials[0], rest.data(),
                                                       static_cast<int>(rest.size()));
            renderer.EndFrame();
            Expect(level == 1, "50 m away the skinned model is drawn with its simpler level");
        }
    }

    // --- The level file ---
    {
        poligon::Level level;
        const char* text = "poligon-level 1\nterrain.seed 7\nterrain.size 2048\nplayer_start 100 200 45\n"
                           "object rocks/boulder 10 20 30 1.5\nplant pine\n";
        Expect(poligon::ParseLevel(text, level, error, {"plant"}), "a level parses");
        Expect(level.terrain.seed == 7 && level.terrain.size == 2048.0f && !level.start_at_lake &&
                   level.start_position == glm::dvec2(100.0, 200.0) && level.start_turn_degrees == 45.0f,
               "terrain settings and the start are read");
        Expect(level.objects.size() == 1 && level.objects[0].asset == "rocks/boulder" && level.objects[0].scale == 1.5f,
               "objects are read");
        Expect(level.other_lines.size() == 1 && level.other_lines[0].first == 6, "lines for other readers are kept with their number");
        const fs::path saved = root / "saved.level";
        Expect(poligon::SaveLevel(saved.string().c_str(), level), "a level is saved");
        poligon::Level again;
        Expect(poligon::LoadLevel(saved.string().c_str(), again, error, {"plant"}) &&
                   poligon::HashTerrainGeneratorSettings(again.terrain) == poligon::HashTerrainGeneratorSettings(level.terrain) &&
                   again.objects.size() == 1 && again.other_lines.size() == 1,
               "and loads back the same");
        Expect(!poligon::ParseLevel("poligon-level 1\nterrain.sise 10\n", level, error) && error.find("line 2") != std::string::npos,
               "a misspelled setting is an error that names its line");
        Expect(!poligon::ParseLevel("poligon-level 1\nobject a 1 2\n", level, error), "an object without all values is an error");
    }

    std::printf("assets_test finished\n");
    return test::ExitCode();
}
