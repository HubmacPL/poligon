// Shows 3D models on a checkered floor, with a camera that circles them.
//
// Usage: model_viewer [MODEL_FILE or FOLDER] [TURN TILT DISTANCE] [standard Poligon options]
//   Double click (no arguments): browse the models in the "poligon-assets" folder next to the project.
//   A folder:                    browse every model in it.
//   One picture:                 model_viewer tree.glb --headless --dump-frame tree.png
//
// Mouse: hold the left button and move to turn the camera.
// Keys:  A/D or Left/Right turn, W/S or Up/Down zoom, Q/E raise and lower the view,
//        N / P next and previous model, Escape quits.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "poligon/command_line.h"
#include "poligon/crash_handler.h"
#include "poligon/game_loop.h"
#include "poligon/input.h"
#include "poligon/model.h"
#include "poligon/platform.h"
#include "poligon/renderer.h"

namespace {

struct ViewerActions {
    poligon::InputAction turn;
    poligon::InputAction tilt;
    poligon::InputAction zoom;
    poligon::InputAction drag_x;
    poligon::InputAction drag_y;
    poligon::InputAction drag_hold;
    poligon::InputAction next_model;
    poligon::InputAction previous_model;
};

ViewerActions SetUpInput(poligon::Input& input) {
    ViewerActions actions;
    actions.turn = input.AddAction("turn");
    actions.tilt = input.AddAction("tilt");
    actions.zoom = input.AddAction("zoom");
    actions.drag_x = input.AddAction("drag_x");
    actions.drag_y = input.AddAction("drag_y");
    actions.drag_hold = input.AddAction("drag_hold");
    actions.next_model = input.AddAction("next_model");
    actions.previous_model = input.AddAction("previous_model");

    input.BindKey(actions.turn, poligon::Key::D, 1.0f);
    input.BindKey(actions.turn, poligon::Key::A, -1.0f);
    input.BindKey(actions.turn, poligon::Key::Right, 1.0f);
    input.BindKey(actions.turn, poligon::Key::Left, -1.0f);
    input.BindKey(actions.tilt, poligon::Key::Q, 1.0f);
    input.BindKey(actions.tilt, poligon::Key::E, -1.0f);
    input.BindKey(actions.zoom, poligon::Key::W, 1.0f);
    input.BindKey(actions.zoom, poligon::Key::S, -1.0f);
    input.BindKey(actions.zoom, poligon::Key::Up, 1.0f);
    input.BindKey(actions.zoom, poligon::Key::Down, -1.0f);
    input.BindMouseMotion(actions.drag_x, poligon::MouseAxis::X);
    input.BindMouseMotion(actions.drag_y, poligon::MouseAxis::Y);
    input.BindMouseButton(actions.drag_hold, poligon::MouseButton::Left);
    input.BindKey(actions.next_model, poligon::Key::N);
    input.BindKey(actions.previous_model, poligon::Key::P);
    return actions;
}

bool IsModelFile(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".gltf" || extension == ".glb" || extension == ".fbx" || extension == ".obj" ||
           extension == ".dae";
}

// All model files below a folder, sorted by path.
std::vector<std::string> FindModels(const std::filesystem::path& folder) {
    std::vector<std::string> models;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && IsModelFile(it->path())) {
            models.push_back(it->path().string());
        }
    }
    std::sort(models.begin(), models.end());
    return models;
}

// The assets folder lives next to the project folder. Starting from the program's own
// location, look in every folder above it for one named "poligon-assets".
std::filesystem::path FindAssetsFolder(const char* program_path) {
    std::error_code error;
    std::filesystem::path folder = std::filesystem::absolute(program_path, error).parent_path();
    for (int depth = 0; depth < 8 && !folder.empty(); ++depth) {
        const std::filesystem::path candidate = folder / "poligon-assets";
        if (std::filesystem::is_directory(candidate, error)) {
            return candidate;
        }
        if (folder == folder.parent_path()) {
            break;
        }
        folder = folder.parent_path();
    }
    return {};
}

// The models offered when the viewer is started without arguments: simplified and generated
// ones, plus downloaded originals that have no simplified version (the originals of
// simplified models can take a minute to load).
std::vector<std::string> FindDefaultModels(const std::filesystem::path& assets) {
    std::vector<std::string> models = FindModels(assets / "simplified");
    for (const std::string& model : FindModels(assets / "generated")) {
        models.push_back(model);
    }
    std::error_code error;
    for (const std::string& model : FindModels(assets / "polyhaven")) {
        const std::filesystem::path asset_name = std::filesystem::path(model).parent_path().filename();
        if (!std::filesystem::is_directory(assets / "simplified" / asset_name, error)) {
            models.push_back(model);
        }
    }
    return models;
}

void PrintModelSummary(const char* path, const poligon::Model& model) {
    const glm::vec3 size = model.bounds_max - model.bounds_min;
    std::printf("model: %s\n", path);
    std::printf("meshes=%zu instances=%zu materials=%zu triangles=%lld vertices=%lld\n", model.meshes.size(),
                model.instances.size(), model.materials.size(), static_cast<long long>(model.GetTriangleCount()),
                static_cast<long long>(model.GetVertexCount()));
    std::printf("size: %.3f x %.3f x %.3f (width x height x depth)\n", size.x, size.y, size.z);
    for (const poligon::ModelMaterial& material : model.materials) {
        if (material.base_color_texture.width > 0) {
            std::printf("material \"%s\": texture %dx%d%s%s%s\n", material.name.c_str(),
                        material.base_color_texture.width, material.base_color_texture.height,
                        material.normal_texture.width > 0 ? ", normal map" : "",
                        material.orm_texture.width > 0 ? ", roughness/metallic map" : "",
                        material.alpha_cutoff > 0.0f ? ", cut-out" : "");
        } else {
            std::printf("material \"%s\": color %.2f %.2f %.2f, no texture\n", material.name.c_str(),
                        material.base_color.r, material.base_color.g, material.base_color.b);
        }
    }
    for (const std::string& warning : model.warnings) {
        std::printf("warning: %s\n", warning.c_str());
    }
    std::fflush(stdout);
}

class ViewerGame : public poligon::Game {
public:
    ViewerGame(poligon::Renderer& renderer, const std::vector<std::string>& model_paths, const poligon::Input& input,
               const ViewerActions& actions, const float view[3])
        : renderer_(renderer), model_paths_(model_paths), input_(input), actions_(actions) {
        start_view_[0] = view[0];
        start_view_[1] = view[1];
        start_view_[2] = view[2];
        floor_mesh_ = renderer.CreatePlaneMesh();
        const std::uint8_t light[4] = {150, 150, 150, 255};
        const std::uint8_t dark[4] = {120, 120, 120, 255};
        floor_texture_ = renderer.CreateTexture(poligon::MakeCheckerImage(128, 128, 64, light, dark));
    }

    // Loads the model at `index` in place of the current one. Returns false (keeping the
    // current model) if the file cannot be loaded.
    bool ShowModel(size_t index) {
        const std::string& path = model_paths_[index];
        poligon::Model model;
        std::string error;
        if (!poligon::LoadModelFromFile(path.c_str(), model, error)) {
            std::fprintf(stderr, "model_viewer: %s\n", error.c_str());
            return false;
        }
        PrintModelSummary(path.c_str(), model);

        poligon::DestroyRenderModel(renderer_, render_model_);
        render_model_ = poligon::UploadModel(renderer_, model);
        current_index_ = index;

        // Frame the model: look at its middle from far enough to see all of it.
        center_ = (model.bounds_min + model.bounds_max) * 0.5f;
        radius_ = std::max(glm::length(model.bounds_max - model.bounds_min) * 0.5f, 0.001f);
        floor_height_ = model.bounds_min.y;
        yaw_degrees_ = start_view_[0];
        pitch_degrees_ = start_view_[1];
        distance_ = radius_ * 2.6f * start_view_[2];

        const std::string title = "Poligon model viewer - " + std::filesystem::path(path).filename().string() + "  (" +
                                  std::to_string(index + 1) + " of " + std::to_string(model_paths_.size()) +
                                  ", N/P: next/previous)";
        renderer_.SetWindowTitle(title.c_str());
        return true;
    }

    void BeginFrame(poligon::GameLoop& loop) override {
        if (!poligon::ProcessPlatformEvents()) {
            loop.RequestQuit();
        }
    }

    void FixedUpdate(poligon::GameLoop& /*loop*/, const poligon::TickInfo& tick) override {
        const size_t count = model_paths_.size();
        if (count > 1 && input_.WasPressed(actions_.next_model)) {
            ShowModel((current_index_ + 1) % count);
        } else if (count > 1 && input_.WasPressed(actions_.previous_model)) {
            ShowModel((current_index_ + count - 1) % count);
        }

        const float delta = static_cast<float>(tick.delta_seconds);
        yaw_degrees_ += input_.GetValue(actions_.turn) * 90.0f * delta;
        pitch_degrees_ += input_.GetValue(actions_.tilt) * 60.0f * delta;
        distance_ *= 1.0f - input_.GetValue(actions_.zoom) * 1.2f * delta;
        if (input_.IsDown(actions_.drag_hold)) {
            yaw_degrees_ -= input_.GetValue(actions_.drag_x) * 0.3f;
            pitch_degrees_ += input_.GetValue(actions_.drag_y) * 0.3f;
        }
        pitch_degrees_ = std::clamp(pitch_degrees_, -5.0f, 85.0f);
        distance_ = std::clamp(distance_, radius_ * 0.2f, radius_ * 20.0f);
    }

    void Render(poligon::GameLoop& /*loop*/, const poligon::FrameInfo& /*frame*/) override {
        const glm::mat4 identity(1.0f);
        const glm::mat4 orbit = glm::rotate(identity, glm::radians(yaw_degrees_), {0.0f, 1.0f, 0.0f}) *
                                glm::rotate(identity, glm::radians(-pitch_degrees_), {1.0f, 0.0f, 0.0f});

        poligon::Camera camera;
        camera.target = center_;
        camera.position = center_ + glm::vec3(orbit * glm::vec4(0.0f, 0.0f, distance_, 0.0f));
        // Clip planes follow the model's size, so both a pebble and a mountain are drawn fully.
        camera.near_plane = radius_ * 0.01f;
        camera.far_plane = radius_ * 100.0f;

        // A sunny day: warm sun from the upper left, blue sky lighting the shadows.
        poligon::DirectionalLight sun;
        sun.direction = {-0.5f, -0.75f, -0.4f};
        sun.color = {2.6f, 2.45f, 2.2f, 1.0f};
        poligon::Environment environment;
        environment.draw_sky = true;
        environment.sky_light = true;
        environment.sky_ground = {0.42f, 0.44f, 0.46f, 1.0f};  // haze below the horizon, past the floor's edge
        environment.shadow_distance = std::max(distance_ + radius_ * 3.0f, 5.0f);
        environment.ambient_occlusion = true;
        // The reach follows the model's size: a 30 cm plant must not darken half a meter of floor.
        environment.ambient_occlusion_radius = std::clamp(radius_ * 0.15f, 0.03f, 0.6f);
        environment.volumetric_fog = true;
        environment.fog_density = 0.0015f;
        environment.fog_height_falloff = 0.03f;

        renderer_.BeginFrame(camera, sun, environment);

        poligon::Material floor_material;
        floor_material.texture = floor_texture_;
        const float floor_size = radius_ * 8.0f;
        floor_material.uv_scale = {8.0f, 8.0f};
        renderer_.DrawMesh(floor_mesh_,
                           glm::translate(identity, {center_.x, floor_height_, center_.z}) *
                               glm::scale(identity, {floor_size, 1.0f, floor_size}),
                           floor_material);

        poligon::DrawModel(renderer_, render_model_, identity);
        renderer_.EndFrame();
    }

private:
    poligon::Renderer& renderer_;
    const std::vector<std::string>& model_paths_;
    const poligon::Input& input_;
    ViewerActions actions_;
    poligon::RenderModel render_model_;
    poligon::MeshHandle floor_mesh_;
    poligon::TextureHandle floor_texture_;
    size_t current_index_ = 0;
    float start_view_[3] = {35.0f, 20.0f, 1.0f};
    glm::vec3 center_;
    float radius_ = 1.0f;
    float floor_height_ = 0.0f;
    float yaw_degrees_ = 35.0f;
    float pitch_degrees_ = 20.0f;
    float distance_ = 3.0f;
};

} // namespace

int main(int argc, char** argv) {
    poligon::InstallCrashHandler();
    // Started with a double click: there is no console to read errors from, so they are
    // shown in a message box. Any argument means a console or a script is in charge.
    const bool started_by_hand = argc == 1;

    poligon::CommandLineOptions options;
    std::string error;
    // Optional starting view: turn and tilt in degrees, and distance as a multiple of the default.
    float view[3] = {35.0f, 20.0f, 1.0f};
    if (poligon::ParseCommandLine(argc, argv, options, error)) {
        if (options.positional.size() > 4) {
            error = "expected at most a model file or folder followed by TURN TILT DISTANCE";
        }
        for (size_t i = 1; i < options.positional.size() && error.empty(); ++i) {
            char* end = nullptr;
            view[i - 1] = std::strtof(options.positional[i].c_str(), &end);
            if (end == options.positional[i].c_str() || *end != '\0') {
                error = "\"" + options.positional[i] + "\" is not a number";
            }
        }
    }
    if (!error.empty()) {
        std::fprintf(stderr,
                     "model_viewer: %s\n"
                     "usage: model_viewer [MODEL_FILE or FOLDER] [TURN TILT DISTANCE] [--headless --ticks N]\n"
                     "                    [--backend d3d12|vulkan] [--gpu-validation] [--dump-frame PATH.png]\n"
                     "Without a file or folder, the models in the poligon-assets folder are shown.\n"
                     "TURN and TILT are in degrees; DISTANCE 0.5 starts twice as close as the default.\n",
                     error.c_str());
        return 2;
    }
    if (options.headless && options.ticks == 0) {
        options.ticks = 1;
    }

    // What to show: one file, every model in a folder, or the default set from poligon-assets.
    std::vector<std::string> model_paths;
    std::error_code file_error;
    if (options.positional.empty()) {
        const std::filesystem::path assets = FindAssetsFolder(argv[0]);
        if (!assets.empty()) {
            model_paths = FindDefaultModels(assets);
        }
        if (model_paths.empty()) {
            error = "No models found. Expected a folder named poligon-assets next to the project folder, "
                    "or start the viewer with a model file: drag a .gltf or .glb file onto model_viewer.exe.";
        }
    } else if (std::filesystem::is_directory(options.positional[0], file_error)) {
        model_paths = FindModels(options.positional[0]);
        if (model_paths.empty()) {
            error = "no model files in " + options.positional[0];
        }
    } else {
        model_paths.push_back(options.positional[0]);
    }
    if (!error.empty()) {
        std::fprintf(stderr, "model_viewer: %s\n", error.c_str());
        if (started_by_hand) {
            poligon::ShowMessageBox("Poligon model viewer", error.c_str());
        }
        return 1;
    }

    poligon::RendererConfig renderer_config;
    renderer_config.headless = options.headless;
    renderer_config.backend = options.backend;
    renderer_config.enable_validation = options.gpu_validation;
    renderer_config.window_title = "Poligon model viewer";
    renderer_config.clear_color = {0.53f, 0.73f, 0.90f, 1.0f};
    renderer_config.post.bloom = true;
    renderer_config.post.auto_exposure = true;
    poligon::Renderer renderer(renderer_config);

    poligon::Input input;
    const ViewerActions actions = SetUpInput(input);
    if (!options.headless) {
        input.UseLiveDevices();
    }

    poligon::GameLoopConfig loop_config;
    loop_config.headless = options.headless;
    loop_config.max_ticks = options.ticks;
    loop_config.render_every_iteration = !options.headless;
    loop_config.input = &input;

    ViewerGame game(renderer, model_paths, input, actions, view);
    // Show the first model that loads.
    size_t first = 0;
    while (first < model_paths.size() && !game.ShowModel(first)) {
        ++first;
    }
    if (first == model_paths.size()) {
        if (started_by_hand) {
            poligon::ShowMessageBox("Poligon model viewer", "None of the models could be loaded.");
        }
        return 1;
    }

    poligon::GameLoop loop(loop_config);
    const poligon::GameLoopResult result = loop.Run(game);

    if (!options.dump_frame_path.empty()) {
        if (result.frame_count == 0 || !renderer.SaveFrameToPng(options.dump_frame_path.c_str())) {
            std::fprintf(stderr, "model_viewer: cannot write %s\n", options.dump_frame_path.c_str());
            return 1;
        }
        std::printf("frame saved to %s\n", options.dump_frame_path.c_str());
    }
    return 0;
}
