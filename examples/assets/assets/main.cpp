// Example: the asset system. A game names its textures and models in an asset list, cooks
// them once into fast-loading files, and loads them by name. Here the example writes its
// own tiny sources first (a checkered picture and a box), since no asset files live in
// the repository; a real game keeps its sources in the assets folder next to the project.
// Usage: example_assets OUTPUT_FOLDER   (writes OUTPUT_FOLDER/example_assets.png)
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "poligon/assets/assets.h"
#include "poligon/render/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_assets OUTPUT_FOLDER\n");
        return 2;
    }
    namespace fs = std::filesystem;
    const fs::path folder = fs::path(argv[1]);
    const fs::path source = folder / "source";
    const fs::path cooked = folder / "cooked";
    std::error_code ignored;
    fs::create_directories(source, ignored);

    // 1. Sources: a picture and a model that uses it.
    const std::uint8_t light[4] = {220, 200, 150, 255};
    const std::uint8_t dark[4] = {120, 70, 40, 255};
    poligon::SaveImageToPng((source / "checker.png").string().c_str(), poligon::MakeCheckerImage(64, 64, 8, light, dark));
    std::ofstream(source / "box.mtl") << "newmtl wood\nKd 1 1 1\nmap_Kd checker.png\n";
    std::ofstream(source / "box.obj") << "mtllib box.mtl\nv -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\nv -1 1 -1\nv 1 1 -1\nv 1 1 1\nv -1 1 1\n"
                                         "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nusemtl wood\n"
                                         "f 5/1 8/4 7/3 6/2\nf 1/1 2/2 3/3 4/4\nf 1/1 5/4 6/3 2/2\nf 3/1 7/4 8/3 4/2\n"
                                         "f 2/1 6/4 7/3 3/2\nf 4/1 8/4 5/3 1/2\n";

    // 2. The asset list: every asset's name, source and settings.
    const fs::path list_path = folder / "assets.txt";
    std::ofstream(list_path) << "poligon-assets 1\n\nmodel props/box\nsource box.obj\n";
    poligon::AssetList list;
    std::string error;
    if (!poligon::LoadAssetList(list_path.string().c_str(), list, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    // 3. Cooking: only what changed since the last time (here: everything, the first time).
    const poligon::CookReport report =
        poligon::CookAssets(list, list_path.string().c_str(), source.string().c_str(), cooked.string().c_str());
    std::printf("cooked %d, up to date %d, failed %d\n", report.cooked, report.up_to_date, static_cast<int>(report.errors.size()));

    // 4. Loading by name, through one library, and drawing.
    poligon::RendererConfig config;
    config.width = 480;
    config.height = 270;
    config.headless = true;
    config.post.exposure = 0.6f;
    poligon::Renderer renderer(config);
    poligon::AssetLibrary library;
    if (!library.Open(cooked.string().c_str(), error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const poligon::LoadedModel* box = library.LoadModel(renderer, "props/box", error);
    if (box == nullptr) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    poligon::Camera camera;
    camera.position = {2.5f, 2.2f, 3.5f};
    camera.target = {0.0f, 0.5f, 0.0f};
    poligon::DirectionalLight sun;
    sun.direction = {-0.5f, -0.8f, -0.3f};
    sun.color = {2.6f, 2.45f, 2.2f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    renderer.BeginFrame(camera, sun, environment);
    poligon::DrawModel(renderer, box->render, glm::mat4(1.0f));
    renderer.EndFrame();
    const std::string output = (folder / "example_assets.png").string();
    if (!renderer.SaveFrameToPng(output.c_str())) {
        std::fprintf(stderr, "cannot write %s\n", output.c_str());
        return 1;
    }
    std::printf("saved %s\n", output.c_str());
    return 0;
}
