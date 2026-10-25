// Example: load a 3D model from a file and draw it without a window.
// Usage: example_model OUTPUT_DIRECTORY
// The example first writes a tiny model file itself (a pyramid in OBJ format), so it needs no assets.
#include <cstdio>
#include <string>

#include "poligon/assets/model.h"
#include "poligon/assets/model_import.h"
#include "poligon/render/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_model OUTPUT_DIRECTORY\n");
        return 2;
    }
    const std::string directory = argv[1];
    const std::string model_path = directory + "/example_model_pyramid.obj";
    const std::string frame_path = directory + "/example_model_frame.png";

    std::FILE* file = nullptr;
    if (fopen_s(&file, model_path.c_str(), "wb") != 0 || file == nullptr) {
        std::fprintf(stderr, "cannot write %s\n", model_path.c_str());
        return 1;
    }
    std::fputs("o Pyramid\n"
               "v -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\nv 0 1.5 0\n"
               "f 1 2 3 4\n"   // the base, seen from below
               "f 4 3 5\nf 3 2 5\nf 2 1 5\nf 1 4 5\n",
               file);
    std::fclose(file);

    // Loading needs no graphics card. The same call reads glTF, GLB, FBX, DAE and many other formats.
    poligon::Model model;
    std::string error;
    if (!poligon::LoadModelFromFile(model_path.c_str(), model, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    std::printf("loaded %lld triangles in %zu mesh(es); size %.1f x %.1f x %.1f\n",
                static_cast<long long>(model.GetTriangleCount()), model.meshes.size(),
                model.bounds_max.x - model.bounds_min.x, model.bounds_max.y - model.bounds_min.y,
                model.bounds_max.z - model.bounds_min.z);
    for (const std::string& warning : model.warnings) {
        std::printf("warning: %s\n", warning.c_str());
    }

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.clear_color = {0.1f, 0.1f, 0.15f, 1.0f};
    poligon::Renderer renderer(config);

    // UploadModel copies meshes and textures to the graphics card; DrawModel draws all of them.
    const poligon::RenderModel render_model = poligon::UploadModel(renderer, model);

    poligon::Camera camera;
    camera.position = {3.0f, 2.5f, 4.0f};
    camera.target = {0.0f, 0.6f, 0.0f};
    const poligon::DirectionalLight light;

    renderer.BeginFrame(camera, light);
    poligon::DrawModel(renderer, render_model, poligon::Transform());
    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(frame_path.c_str())) {
        std::fprintf(stderr, "cannot write %s\n", frame_path.c_str());
        return 1;
    }
    std::printf("frame saved to %s\n", frame_path.c_str());
    return 0;
}
