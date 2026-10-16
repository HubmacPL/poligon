// Checks the asset system: block compression (sizes, levels, repeatable bytes, the file
// format), the asset list (and its error messages), cooking only what changed, loading by
// name from the cooked folder and from an archive, drawing a cooked textured model, plant
// variants split from one model file, and the level file.
// Usage: assets_test d3d12|vulkan OUTPUT_DIRECTORY
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "poligon/assets/assets.h"
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
)";

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
                textured = textured || (model->model.materials[m].name == "painted" && model->render.materials[m].texture.id != 0);
            }
            Expect(model != nullptr && model->model.instances.size() == 2 && textured,
                   ("a model with its texture loads from the " + where).c_str());
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
            renderer.BeginFrame(camera, light, environment);
            poligon::DrawModel(renderer, model->render, glm::mat4(1.0f));
            renderer.EndFrame();
            poligon::Image frame;
            renderer.CaptureFrame(frame);
            const std::uint8_t* middle = &frame.pixels[static_cast<size_t>(32 * 64 + 32) * 4];
            if (std::abs(middle[0] - 40) > 8 || std::abs(middle[1] - 200) > 8 || std::abs(middle[2] - 90) > 8) {
                std::printf("  middle pixel %d %d %d, expected about 40 200 90\n", middle[0], middle[1], middle[2]);
            }
            Expect(std::abs(middle[0] - 40) <= 8 && std::abs(middle[1] - 200) <= 8 && std::abs(middle[2] - 90) <= 8,
                   "the cooked, compressed texture draws in its color");
        }
    }

    // --- The level file ---
    {
        poligon::Level level;
        const char* text = "poligon-level 1\nterrain.seed 7\nterrain.size 2048\nplayer_start 100 200 45\n"
                           "object rocks/boulder 10 20 30 1.5\nplant pine\n";
        Expect(poligon::ParseLevel(text, level, error, {"plant"}), "a level parses");
        Expect(level.terrain.seed == 7 && level.terrain.size == 2048.0f && !level.start_at_lake &&
                   level.start_position == glm::vec2(100.0f, 200.0f) && level.start_turn_degrees == 45.0f,
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
