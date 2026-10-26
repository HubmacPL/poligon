// Shows 3D models on a checkered floor, with a camera that circles them.
//
// Usage: model_viewer [MODEL_FILE or FOLDER] [TURN TILT DISTANCE [CLIP]] [standard Poligon options]
// CLIP is the name of the animation clip to play (default: the model's first one); C
// switches to the next clip. With --headless, --ticks N plays N sixtieths of a second
// before the picture is taken.
//   Double click (no arguments): browse the models in poligon-assets/source next to the project,
//                                or, in a released copy without sources, the models of the
//                                cooked archive poligon-assets/cooked/assets.pak, by the names
//                                in assets.txt next to the program.
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
#include <memory>
#include <string>
#include <vector>

#include "poligon/animation/animation_build.h"
#include "poligon/assets/assets.h"
#include "poligon/assets/cooking.h"
#include "poligon/core/command_line.h"
#include "poligon/core/crash_handler.h"
#include "poligon/core/game_loop.h"
#include "poligon/core/input.h"
#include "poligon/assets/model.h"
#include "poligon/assets/model_import.h"
#include "poligon/core/platform.h"
#include "poligon/render/renderer.h"

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
    poligon::InputAction next_clip;
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
    actions.next_clip = input.AddAction("next_clip");

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
    input.BindKey(actions.next_clip, poligon::Key::C);
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

// The models offered when the viewer is started without arguments, from the source half of
// the assets folder: generated ones, and downloaded ones as their first simpler level when
// they have one (some originals take a minute to load).
std::vector<std::string> FindDefaultModels(const std::filesystem::path& source) {
    std::vector<std::string> models = FindModels(source / "generated");
    std::error_code error;
    for (const std::string& model : FindModels(source / "polyhaven")) {
        const std::string asset_name = std::filesystem::path(model).parent_path().filename().string();
        const std::filesystem::path simpler = source / "lods" / asset_name / "lod1" / (asset_name + ".gltf");
        models.push_back(std::filesystem::is_regular_file(simpler, error) ? simpler.string() : model);
    }
    return models;
}

// The model names of an asset list (released copies: the models of the cooked archive).
std::vector<std::string> FindListedModels(const std::filesystem::path& list_path) {
    poligon::AssetList list;
    std::string error;
    std::vector<std::string> names;
    if (!poligon::LoadAssetList(list_path.string().c_str(), list, error)) {
        std::fprintf(stderr, "model_viewer: %s\n", error.c_str());
        return names;
    }
    for (const poligon::AssetEntry& entry : list.entries) {
        if (entry.kind == poligon::AssetKind::Model) {
            names.push_back(entry.name);
        }
    }
    return names;
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
    if (!model.skeleton.joints.empty()) {
        std::printf("skeleton: %zu joints\n", model.skeleton.joints.size());
    }
    for (const poligon::AnimationClip& clip : model.animations) {
        std::printf("animation \"%s\": %.3f s, %zu channels\n", clip.name.c_str(),
                    static_cast<double>(clip.duration_seconds), clip.channels.size());
    }
    for (const std::string& warning : model.warnings) {
        std::printf("warning: %s\n", warning.c_str());
    }
    std::fflush(stdout);
}

class ViewerGame : public poligon::Game {
public:
    // `library`: when given, model_paths are asset names in it, not files.
    ViewerGame(poligon::Renderer& renderer, const std::vector<std::string>& model_paths, const poligon::Input& input,
               const ViewerActions& actions, const float view[3], const std::string& start_clip,
               poligon::AssetLibrary* library = nullptr)
        : renderer_(renderer), model_paths_(model_paths), input_(input), actions_(actions), start_clip_(start_clip),
          library_(library) {
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
        poligon::Model loaded_from_file;
        const poligon::LoadedModel* cooked = nullptr;
        std::string error;
        if (library_ != nullptr) {
            cooked = library_->LoadModel(renderer_, path, error);
        }
        if (library_ != nullptr ? cooked == nullptr : !poligon::LoadModelFromFile(path.c_str(), loaded_from_file, error)) {
            std::fprintf(stderr, "model_viewer: %s\n", error.c_str());
            return false;
        }
        const poligon::Model& model = cooked != nullptr ? cooked->model : loaded_from_file;
        PrintModelSummary(path.c_str(), model);

        // A cooked model's render model belongs to the library.
        player_.reset();
        if (render_model_owned_) {
            poligon::DestroyRenderModel(renderer_, render_model_);
        }
        render_model_ = cooked != nullptr ? cooked->render : poligon::UploadModel(renderer_, model);
        render_model_owned_ = cooked == nullptr;
        current_index_ = index;

        // A model with animation clips plays one of them (cooked ones come built).
        animations_.reset();
        clip_name_.clear();
        if (cooked != nullptr) {
            animations_ = cooked->animations;
        } else if (!model.animations.empty()) {
            animations_ = poligon::BuildAnimationSet(model.skeleton, model.animations);
        }
        if (animations_ != nullptr) {
            player_ = std::make_unique<poligon::AnimationPlayer>(*animations_);
            const int wanted = animations_->FindClip(start_clip_.c_str());
            if (!start_clip_.empty() && wanted < 0) {
                std::fprintf(stderr, "model_viewer: no clip named \"%s\", playing the first one\n", start_clip_.c_str());
            }
            PlayClip(wanted >= 0 ? wanted : 0, 0.0f);
        }

        // Frame the model: look at its middle from far enough to see all of it.
        center_ = (model.bounds_min + model.bounds_max) * 0.5f;
        radius_ = std::max(glm::length(model.bounds_max - model.bounds_min) * 0.5f, 0.001f);
        floor_height_ = model.bounds_min.y;
        yaw_degrees_ = start_view_[0];
        pitch_degrees_ = start_view_[1];
        distance_ = radius_ * 2.6f * start_view_[2];

        UpdateTitle();
        return true;
    }

    void PlayClip(int clip, float fade_seconds) {
        player_->Play(clip, fade_seconds);
        clip_name_ = animations_->GetClipName(clip);
        std::printf("playing clip \"%s\"\n", clip_name_.c_str());
        UpdateTitle();
    }

    void UpdateTitle() {
        std::string title = "Poligon model viewer - " +
                            std::filesystem::path(model_paths_[current_index_]).filename().string() + "  (" +
                            std::to_string(current_index_ + 1) + " of " + std::to_string(model_paths_.size()) +
                            ", N/P: next/previous)";
        if (!clip_name_.empty()) {
            title += "  clip: " + clip_name_ + " (C: next clip)";
        }
        renderer_.SetWindowTitle(title.c_str());
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
        if (player_) {
            if (input_.WasPressed(actions_.next_clip)) {
                PlayClip((player_->GetClip() + 1) % animations_->GetClipCount(), 0.3f);
            }
            player_->Update(delta);
        }
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
                           glm::dvec3(0.0), glm::translate(identity, {center_.x, floor_height_, center_.z}) *
                               glm::scale(identity, {floor_size, 1.0f, floor_size}),
                           floor_material);

        if (player_) {
            poligon::DrawModel(renderer_, render_model_, poligon::Transform(), player_->GetJointMatrices());
        } else {
            poligon::DrawModel(renderer_, render_model_, poligon::Transform());
        }
        renderer_.EndFrame();
    }

private:
    poligon::Renderer& renderer_;
    const std::vector<std::string>& model_paths_;
    const poligon::Input& input_;
    ViewerActions actions_;
    poligon::RenderModel render_model_;
    bool render_model_owned_ = true;
    std::string start_clip_;
    poligon::AssetLibrary* library_ = nullptr;
    std::string clip_name_;
    // The player refers to the set, so it is declared after it and destroyed before it.
    std::shared_ptr<const poligon::AnimationSet> animations_;
    std::unique_ptr<poligon::AnimationPlayer> player_;
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
        if (options.positional.size() > 5) {
            error = "expected at most a model file or folder followed by TURN TILT DISTANCE CLIP";
        }
        for (size_t i = 1; i < options.positional.size() && i < 4 && error.empty(); ++i) {
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
                     "usage: model_viewer [MODEL_FILE or FOLDER] [TURN TILT DISTANCE [CLIP]] [--headless --ticks N]\n"
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
    poligon::AssetLibrary library;
    bool use_library = false;
    if (options.positional.empty()) {
        const std::filesystem::path assets = FindAssetsFolder(argv[0]);
        const std::filesystem::path archive = assets / "cooked" / "assets.pak";
        const std::filesystem::path list = std::filesystem::absolute(argv[0], file_error).parent_path() / "assets.txt";
        if (!assets.empty() && std::filesystem::is_directory(assets / "source", file_error)) {
            model_paths = FindDefaultModels(assets / "source");
        } else if (!assets.empty() && std::filesystem::is_regular_file(archive, file_error) &&
                   std::filesystem::is_regular_file(list, file_error)) {
            if (library.Open(archive.string().c_str(), error)) {
                model_paths = FindListedModels(list);
                use_library = true;
            }
            error.clear();
        }
        if (model_paths.empty()) {
            error = "No models found. Expected a folder named poligon-assets next to the project folder (with source inside) "
                    "or next to the program (with cooked/assets.pak inside, and assets.txt next to the program), "
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

    const std::string start_clip = options.positional.size() > 4 ? options.positional[4] : std::string();
    ViewerGame game(renderer, model_paths, input, actions, view, start_clip, use_library ? &library : nullptr);
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
