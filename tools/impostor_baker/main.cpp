// Bakes an impostor of a model: pictures from many directions over the half sphere above
// it, for drawing it far away as one card (see Impostor in poligon/renderer.h).
//
// Usage: impostor_baker MODEL OUT.impostor [--frames N] [--size PIXELS] [--keep-origin]
//
// MODEL          any model file the engine loads (poligon/model.h)
// OUT.impostor   written with OUT_color.png and OUT_normal.png next to it (poligon/impostor.h)
// --frames N     pictures per side of the grid, N x N in all (default 8)
// --size PIXELS  width and height of each picture (default 128)
// --keep-origin  keep the model where its file puts it sideways; otherwise the middle of
//                its foot is moved to the origin. Use the same choice as when the model is
//                placed in the world: a generated tree keeps its trunk at the origin.
// In both cases the model's lowest point is moved to height 0, as forest_scene does.
//
// Exit codes: 0 = done, 1 = failed, 2 = bad arguments.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "poligon/impostor.h"
#include "poligon/model.h"
#include "poligon/renderer.h"

int main(int argc, char** argv) {
    const char* model_path = nullptr;
    const char* output_path = nullptr;
    int frames_per_side = 8;
    int frame_size = 128;
    bool keep_origin = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames_per_side = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            frame_size = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--keep-origin") == 0) {
            keep_origin = true;
        } else if (model_path == nullptr) {
            model_path = argv[i];
        } else if (output_path == nullptr) {
            output_path = argv[i];
        } else {
            model_path = nullptr;
            break;
        }
    }
    if (model_path == nullptr || output_path == nullptr || frames_per_side < 2 || frames_per_side > 32 || frame_size < 8 ||
        frames_per_side * frame_size > 8192) {
        std::fprintf(stderr, "usage: impostor_baker MODEL OUT.impostor [--frames N] [--size PIXELS] [--keep-origin]\n");
        return 2;
    }

    poligon::Model model;
    std::string error;
    if (!poligon::LoadModelFromFile(model_path, model, error)) {
        std::fprintf(stderr, "impostor_baker: %s\n", error.c_str());
        return 1;
    }

    // The foot of the model at the origin, as forest_scene places it.
    glm::vec3 low(1.0e30f);
    glm::vec3 high(-1.0e30f);
    for (const poligon::ModelInstance& instance : model.instances) {
        for (const poligon::MeshVertex& vertex : model.meshes[static_cast<size_t>(instance.mesh_index)].vertices) {
            const glm::vec3 position = glm::vec3(instance.transform * glm::vec4(vertex.position, 1.0f));
            low = glm::min(low, position);
            high = glm::max(high, position);
        }
    }
    const glm::mat4 to_foot = glm::translate(
        glm::mat4(1.0f), {keep_origin ? 0.0f : -(low.x + high.x) * 0.5f, -low.y, keep_origin ? 0.0f : -(low.z + high.z) * 0.5f});

    poligon::RendererConfig config;
    config.headless = true;
    config.width = 64;
    config.height = 64;
    config.msaa_samples = 1;
    poligon::Renderer renderer(config);
    const poligon::RenderModel render_model = poligon::UploadModel(renderer, model);

    std::vector<poligon::ImpostorPart> parts;
    for (const poligon::ModelInstance& instance : model.instances) {
        const size_t mesh_index = static_cast<size_t>(instance.mesh_index);
        if (render_model.mesh_is_skinned[mesh_index]) {
            continue;  // impostors are for things that stand still
        }
        poligon::ImpostorPart part;
        part.mesh = render_model.meshes[mesh_index];
        part.material = render_model.materials[static_cast<size_t>(render_model.mesh_material_index[mesh_index])];
        part.transform = to_foot * instance.transform;
        parts.push_back(part);
    }
    if (parts.empty()) {
        std::fprintf(stderr, "impostor_baker: %s has no meshes that stand still\n", model_path);
        return 1;
    }

    poligon::Impostor impostor;
    renderer.BakeImpostor(parts.data(), static_cast<int>(parts.size()), frames_per_side, frame_size, impostor);
    if (!poligon::SaveImpostor(output_path, impostor)) {
        std::fprintf(stderr, "impostor_baker: cannot write %s\n", output_path);
        return 1;
    }
    size_t covered = 0;
    for (size_t i = 3; i < impostor.color.pixels.size(); i += 4) {
        covered += impostor.color.pixels[i] > 127 ? 1 : 0;
    }
    std::printf("impostor_baker: %s: %d x %d frames of %d px, center %.2f %.2f %.2f, radius %.2f, %.1f%% covered\n",
                output_path, frames_per_side, frames_per_side, frame_size, static_cast<double>(impostor.center.x),
                static_cast<double>(impostor.center.y), static_cast<double>(impostor.center.z),
                static_cast<double>(impostor.radius),
                100.0 * static_cast<double>(covered) / static_cast<double>(impostor.color.pixels.size() / 4));
    return 0;
}
