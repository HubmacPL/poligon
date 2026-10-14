// Showcase: real land (6 x 6 km around Batorz, from poligon-assets/terrain) with a piece of
// forest on a hilltop, put together from the models in the poligon-assets folder, to walk
// through and look at. Without the land's file, hills from noise are used. It exists to judge the renderer on a real scene and to
// measure how long a frame takes.
//
// Usage: forest_scene [X Z TURN TILT [collision]] [standard Poligon options]
//   X Z         where to stand, in meters from the middle of the forest (default 0 0)
//   TURN TILT   where to look, in degrees (default: towards the sun, level)
//   collision   the word itself: start with the collision shapes shown
//   --render-settings FILE   the look to start with, as saved from the settings window
//
// Mouse: hold the left button and move to look around.
// Keys:  W/S walk, A/D step sideways, Left/Right turn, Up/Down look up and down, hold
//        Left Shift to run, Space to jump. F switches between walking and flying (then E/Q
//        rise and sink). T throws a crate, B shows the collision shapes as colored boxes.
//        Tab hides and shows the debug windows.
// Walking is done by the physics world: trunks, rocks and stumps block the way.
// The "Render settings" window changes the look while the program runs; its Save button
// writes forest_render_settings.txt into the poligon-assets folder.
// Example for the agent:
//   forest_scene 10 -5 40 5 --headless --dump-frame forest.png --render-settings look.txt
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "poligon/animation.h"
#include "poligon/command_line.h"
#include "poligon/crash_handler.h"
#include "poligon/debug_ui.h"
#include "poligon/free_camera.h"
#include "poligon/game_loop.h"
#include "poligon/impostor.h"
#include "poligon/input.h"
#include "poligon/model.h"
#include "poligon/physics.h"
#include "poligon/platform.h"
#include "poligon/render_settings.h"
#include "poligon/terrain.h"
#include "poligon/vegetation.h"
#include "poligon/renderer.h"

namespace {

constexpr float kForestHalfSize = 250.0f;  // plants stand within this distance of the start
// Beyond that, big trees alone continue to this distance; far away they are impostors.
constexpr float kFarForestHalfSize = 1200.0f;
// Collisions exist within this distance of the start; fences stand at its edge.
constexpr float kPlayHalfSize = 500.0f;
// Where the player starts on the Batorz map, in meters from its north-west corner: the top
// of the nearest hill west of the village, 70 m above the valley.
constexpr float kStartOnMapX = 2120.0f;
constexpr float kStartOnMapZ = 3120.0f;
constexpr float kEyeHeight = 1.7f;
constexpr float kCrateSize = 0.6f;
// How far each kind of thing is drawn (meters). Trees have simpler meshes for the two
// longer ranges; everything else has one mesh and simply stops being drawn.
// How far each level of detail is used, for kinds that have simpler levels. `impostor`:
// how far a baked impostor (poligon-assets/impostors/NAME.impostor, made by
// impostor_baker) is drawn after the low level; 0 = no impostor. Without the file the low
// level reaches to `low_without_impostor` instead.
struct LevelDistances {
    float full;
    float medium;
    float low;
    float impostor;
    float low_without_impostor;
};
constexpr LevelDistances kTreeLevels{28.0f, 70.0f, 150.0f, 1700.0f, 450.0f};
constexpr LevelDistances kYoungTreeLevels{28.0f, 70.0f, 150.0f, 0.0f, 150.0f};
// Rocks, stumps, fallen trunks and shrubs.
constexpr LevelDistances kThingLevels{15.0f, 45.0f, 150.0f, 0.0f, 150.0f};
constexpr float kSmallPlantDistance = 45.0f;
constexpr float kMediumThingDistance = 150.0f;
constexpr float kShadowDistance = 65.0f;

// Repeatable random numbers: the same forest on every run and on every machine.
class Random {
public:
    explicit Random(std::uint32_t seed) : state_(seed) {}

    // Evenly spread in 0..1.
    float Next() {
        state_ = state_ * 747796405u + 2891336453u;
        std::uint32_t word = ((state_ >> ((state_ >> 28u) + 4u)) ^ state_) * 277803737u;
        word = (word >> 22u) ^ word;
        return static_cast<float>(word >> 8) / 16777216.0f;
    }

    float Between(float low, float high) { return low + (high - low) * Next(); }

private:
    std::uint32_t state_;
};

// One mesh of a plant with its material, placed relative to the plant's foot.
struct Piece {
    poligon::MeshHandle mesh;
    poligon::Material material;
    glm::mat4 local;
};

// One plant (or rock) that can be put into the scene many times.
struct Variant {
    std::vector<Piece> pieces;
    // The variant's number in the vegetation system.
    int species = -1;
    float height = 0.0f;
    // Half of the variant's width along x and z, before scaling.
    glm::vec2 half_width{0.0f};
};

// What stands in the way of the player and of thrown crates.
enum class Collision {
    None,   // plants: walked through
    Trunk,  // trees: an upright cylinder where the trunk is; the crown does not block
    Block,  // rocks, stumps, fallen trunks: a box around the whole thing
};

// The variants of one model file. Several downloaded models hold a row of different
// plants; each of those becomes its own variant.
struct Kind {
    std::vector<Variant> variants;
    bool is_small = false;
    Collision collision = Collision::None;
    // For Collision::Trunk: radius of the trunk before scaling.
    float trunk_radius = 0.3f;
};

struct Placement {
    const Variant* variant;
    glm::mat4 transform;
    glm::vec3 position;
    bool is_small;
};

// An animal that moves around the clearing in a circle, or stands in one place.
struct Animal {
    std::unique_ptr<poligon::AnimationPlayer> player;
    float circle_radius = 0.0f;       // 0 = stands still at `place`
    float meters_per_second = 0.0f;   // negative = the other way around
    float start_angle = 0.0f;
    glm::vec2 place{0.0f, 0.0f};      // x and z; for a standing animal
    float facing_degrees = 0.0f;      // for a standing animal
    glm::mat4 transform{1.0f};
};

struct ForestActions {
    poligon::InputAction walk;
    poligon::InputAction strafe;
    poligon::InputAction turn;
    poligon::InputAction tilt;
    poligon::InputAction run;
    poligon::InputAction rise;
    poligon::InputAction toggle_fly;
    poligon::InputAction toggle_windows;
    poligon::InputAction jump;
    poligon::InputAction throw_crate;
    poligon::InputAction toggle_collision_view;
    poligon::InputAction drag_x;
    poligon::InputAction drag_y;
    poligon::InputAction drag_hold;
};

ForestActions SetUpInput(poligon::Input& input) {
    ForestActions actions;
    actions.walk = input.AddAction("walk");
    actions.strafe = input.AddAction("strafe");
    actions.turn = input.AddAction("turn");
    actions.tilt = input.AddAction("tilt");
    actions.run = input.AddAction("run");
    actions.rise = input.AddAction("rise");
    actions.toggle_fly = input.AddAction("toggle_fly");
    actions.toggle_windows = input.AddAction("toggle_windows");
    actions.jump = input.AddAction("jump");
    actions.throw_crate = input.AddAction("throw_crate");
    actions.toggle_collision_view = input.AddAction("toggle_collision_view");
    actions.drag_x = input.AddAction("drag_x");
    actions.drag_y = input.AddAction("drag_y");
    actions.drag_hold = input.AddAction("drag_hold");

    input.BindKey(actions.walk, poligon::Key::W, 1.0f);
    input.BindKey(actions.walk, poligon::Key::S, -1.0f);
    input.BindKey(actions.strafe, poligon::Key::D, 1.0f);
    input.BindKey(actions.strafe, poligon::Key::A, -1.0f);
    input.BindKey(actions.turn, poligon::Key::Left, 1.0f);
    input.BindKey(actions.turn, poligon::Key::Right, -1.0f);
    input.BindKey(actions.tilt, poligon::Key::Up, 1.0f);
    input.BindKey(actions.tilt, poligon::Key::Down, -1.0f);
    input.BindKey(actions.run, poligon::Key::LeftShift);
    input.BindKey(actions.rise, poligon::Key::E, 1.0f);
    input.BindKey(actions.rise, poligon::Key::Q, -1.0f);
    input.BindKey(actions.toggle_fly, poligon::Key::F);
    input.BindKey(actions.toggle_windows, poligon::Key::Tab);
    input.BindKey(actions.jump, poligon::Key::Space);
    input.BindKey(actions.throw_crate, poligon::Key::T);
    input.BindKey(actions.toggle_collision_view, poligon::Key::B);
    input.BindMouseMotion(actions.drag_x, poligon::MouseAxis::X);
    input.BindMouseMotion(actions.drag_y, poligon::MouseAxis::Y);
    input.BindMouseButton(actions.drag_hold, poligon::MouseButton::Left);
    return actions;
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

// The first .gltf file in a folder, or an empty path.
std::filesystem::path FindGltf(const std::filesystem::path& folder) {
    std::error_code error;
    std::vector<std::filesystem::path> found;
    for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        if (it->path().extension() == ".gltf") {
            found.push_back(it->path());
        }
    }
    std::sort(found.begin(), found.end());
    return found.empty() ? std::filesystem::path() : found.front();
}

struct Box {
    glm::vec3 min{1.0e30f};
    glm::vec3 max{-1.0e30f};
};

class ForestGame : public poligon::Game {
public:
    ForestGame(poligon::Renderer& renderer, const std::filesystem::path& assets, const poligon::Input& input,
               const ForestActions& actions, const float view[4], const poligon::RenderSettings& settings,
               const std::string& settings_path)
        : renderer_(renderer), assets_(assets), input_(input), actions_(actions), settings_(settings),
          settings_path_(settings_path) {
        CreateGround();
        camera_.position = {view[0], Ground(view[0], view[1]) + kEyeHeight, view[1]};
        camera_.yaw_degrees = view[2];
        camera_.pitch_degrees = view[3];
        camera_.vertical_fov_degrees = 65.0f;
        LoadKinds();
        PlantForest();
        AddAnimals();
        AddCrates();

        poligon::CharacterSettings walker;
        walker.position = {view[0], Ground(view[0], view[1]), view[1]};
        walker_ = physics_.AddCharacter(walker);
        std::printf("forest_scene: %d physics bodies\n", physics_.GetBodyCount());
    }

    int GetLoadedKindCount() const { return loaded_kind_count_; }
    // Height of the land at a world position.
    float Ground(float x, float z) const { return terrain_->GetHeight(x, z); }
    void ShowCollision(bool show) { show_collision_ = show; }
    glm::vec3 GetCameraPosition() const { return camera_.position; }

    void BeginFrame(poligon::GameLoop& loop) override {
        if (!poligon::ProcessPlatformEvents()) {
            loop.RequestQuit();
        }
    }

    void FixedUpdate(poligon::GameLoop& /*loop*/, const poligon::TickInfo& tick) override {
        if (input_.WasPressed(actions_.toggle_fly)) {
            flying_ = !flying_;
        }
        if (input_.WasPressed(actions_.toggle_windows)) {
            show_windows_ = !show_windows_;
        }

        const float delta = static_cast<float>(tick.delta_seconds);
        time_seconds_ += delta;
        for (Animal& animal : animals_) {
            animal.player->Update(delta);
            PlaceAnimal(animal);
        }
        float yaw_change = input_.GetValue(actions_.turn) * 90.0f * delta;
        float pitch_change = input_.GetValue(actions_.tilt) * 60.0f * delta;
        if (input_.IsDown(actions_.drag_hold)) {
            yaw_change -= input_.GetValue(actions_.drag_x) * 0.2f;
            pitch_change -= input_.GetValue(actions_.drag_y) * 0.2f;
        }
        camera_.Turn(yaw_change, pitch_change);

        if (input_.WasPressed(actions_.toggle_collision_view)) {
            show_collision_ = !show_collision_;
        }
        if (input_.WasPressed(actions_.throw_crate)) {
            ThrowCrate();
        }

        const float speed = input_.IsDown(actions_.run) ? 10.0f : 2.5f;
        const float forward = input_.GetValue(actions_.walk) * speed;
        const float right = input_.GetValue(actions_.strafe) * speed;
        if (input_.WasPressed(actions_.toggle_fly) && !flying_) {
            // Back to walking: the character continues from below where the camera is.
            physics_.SetCharacterPosition(walker_, camera_.position - glm::vec3(0.0f, kEyeHeight, 0.0f));
        }
        if (flying_) {
            camera_.Move(forward * delta, right * delta, input_.GetValue(actions_.rise) * speed * delta);
            physics_.SetCharacterInput(walker_, glm::vec3(0.0f), false);
        } else {
            // Walking: the wish to move is level whatever the view direction; the physics
            // world keeps the character on the ground and out of trunks and rocks.
            poligon::FreeCamera level = camera_;
            level.pitch_degrees = 0.0f;
            level.position = glm::vec3(0.0f);
            level.Move(forward, right, 0.0f);
            physics_.SetCharacterInput(walker_, level.position, input_.WasPressed(actions_.jump));
        }
        physics_.Step(delta);
        if (!flying_) {
            camera_.position = physics_.GetCharacterPosition(walker_) + glm::vec3(0.0f, kEyeHeight, 0.0f);
        }
    }

    void Render(poligon::GameLoop& /*loop*/, const poligon::FrameInfo& /*frame*/) override {
        poligon::Camera clip_planes;
        clip_planes.near_plane = 0.1f;
        clip_planes.far_plane = 12000.0f;
        const poligon::Camera camera = camera_.ToCamera(clip_planes);

        renderer_.SetPostSettings(settings_.post);
        renderer_.BeginFrame(camera, settings_.sun, settings_.environment);
        if (show_windows_) {
            poligon::ShowRenderSettingsWindow(renderer_, settings_, settings_path_.c_str());
            poligon::ShowFrameStatsWindow(renderer_);
        }
        terrain_->Draw(camera);
        for (const poligon::BodyHandle crate : crates_) {
            renderer_.DrawMesh(cube_mesh_, physics_.GetTransform(crate) * glm::scale(glm::mat4(1.0f), glm::vec3(kCrateSize)),
                               crate_material_);
        }
        if (show_collision_) {
            // Every body except the ground and the fences, as the box that encloses it:
            // red = never moves, yellow = moves.
            for (std::uint32_t id = first_shown_body_id_; id <= physics_.GetLastBodyId(); ++id) {
                const poligon::BodyHandle body{id};
                if (!physics_.IsBody(body)) {
                    continue;
                }
                glm::vec3 bounds_min;
                glm::vec3 bounds_max;
                physics_.GetBounds(body, bounds_min, bounds_max);
                if (glm::distance((bounds_min + bounds_max) * 0.5f, camera.position) > 60.0f) {
                    continue;
                }
                const poligon::Color color = physics_.IsDynamic(body) ? poligon::Color{1.0f, 0.9f, 0.1f, 1.0f}
                                                                       : poligon::Color{1.0f, 0.15f, 0.3f, 1.0f};
                renderer_.DrawMesh(cube_mesh_,
                                   glm::translate(glm::mat4(1.0f), (bounds_min + bounds_max) * 0.5f) *
                                       glm::scale(glm::mat4(1.0f), bounds_max - bounds_min),
                                   color);
            }
        }
        for (Animal& animal : animals_) {
            poligon::DrawModel(renderer_, fox_model_, animal.transform, animal.player->GetJointMatrices());
        }
        vegetation_.Draw(renderer_, camera);
        if (show_windows_ && renderer_.IsDebugUiActive()) {
            const poligon::VegetationStats& stats = vegetation_.GetStats();
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.0f, 140.0f), ImGuiCond_FirstUseEver,
                                    ImVec2(1.0f, 0.0f));
            if (ImGui::Begin("Vegetation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("%d plants and rocks placed", stats.instances_total);
                ImGui::Text("drawn: %d full, %d medium, %d low detail", stats.instances_per_level[0],
                            stats.instances_per_level[1], stats.instances_per_level[2]);
                ImGui::Text("cells: %d of %d skipped whole", stats.cells_skipped, stats.cells_total);
                const poligon::FrameStats frame = renderer_.GetFrameStats();
                ImGui::Text("last frame: %d draw calls for %d + %d instances", frame.instance_draw_calls, frame.instances_drawn,
                            frame.shadow_instances);
                ImGui::PushItemWidth(160.0f);
                bool changed = ImGui::SliderFloat("detail distance", &detail_scale_, 0.2f, 4.0f, "x %.2f");
                changed |= ImGui::SliderFloat("shadow distance", &shadow_scale_, 0.0f, 2.0f, "x %.2f");
                ImGui::PopItemWidth();
                if (changed) {
                    vegetation_.SetDistanceScale(detail_scale_, shadow_scale_);
                }
            }
            ImGui::End();
        }
        renderer_.EndFrame();
    }

private:
    poligon::TextureHandle LoadTexture(const std::filesystem::path& path, bool is_color) {
        poligon::Image image;
        std::string error;
        if (!poligon::LoadImageFromFile(path.string().c_str(), image, error)) {
            std::printf("forest_scene: %s\n", error.c_str());
            return {};
        }
        poligon::TextureOptions options;
        options.is_color = is_color;
        return renderer_.CreateTexture(image, options);
    }

    // The land: the Batorz height map if it is there, otherwise hills from noise. The world's
    // origin is put at the player's start on the map, at the land's height there.
    void CreateGround() {
        poligon::HeightMap map;
        std::string error;
        const std::filesystem::path file = assets_ / "terrain" / "batorz" / "batorz_6km_1m.heights";
        glm::vec2 start{kStartOnMapX, kStartOnMapZ};
        if (!poligon::LoadHeightMap(file.string().c_str(), map, error)) {
            std::printf("forest_scene: %s; using hills made from noise\n", error.c_str());
            map = poligon::MakeNoiseHeightMap(2049, 2049, 1.0f, 3, 350.0f, 60.0f);
            start = {1024.0f, 1024.0f};
        }
        poligon::TerrainConfig config;
        config.origin = {-start.x, -map.GetHeight(start.x, start.y), -start.y};
        const std::filesystem::path textures = assets_ / "polyhaven" / "textures";
        // Mossy forest floor and dry pine litter, mixed in patches.
        config.material.layers[0].color = LoadTexture(textures / "forrest_ground_01" / "forrest_ground_01_diff_2k.jpg", true);
        config.material.layers[0].normal = LoadTexture(textures / "forrest_ground_01" / "forrest_ground_01_nor_gl_2k.jpg", false);
        config.material.layers[0].tile_size = 3.0f;
        config.material.layers[1].color = LoadTexture(textures / "forest_leaves_04" / "forest_leaves_04_diff_2k.jpg", true);
        config.material.layers[1].normal = LoadTexture(textures / "forest_leaves_04" / "forest_leaves_04_nor_gl_2k.jpg", false);
        config.material.layers[1].tile_size = 2.5f;
        terrain_ = std::make_unique<poligon::Terrain>(renderer_, map, config);
        std::printf("forest_scene: land %.0f x %.0f m, a point every %.0f m\n", static_cast<double>(map.GetSizeX()),
                    static_cast<double>(map.GetSizeZ()), static_cast<double>(map.spacing));

        // Collisions: the square of land around the start, as large as fits on the map.
        const int half_points = static_cast<int>(kPlayHalfSize / map.spacing);
        const int center_x = static_cast<int>(start.x / map.spacing);
        const int center_z = static_cast<int>(start.y / map.spacing);
        const int size = 2 * half_points + 1;
        const int first_x = std::clamp(center_x - half_points, 0, map.width - size);
        const int first_z = std::clamp(center_z - half_points, 0, map.depth - size);
        const poligon::HeightMap window = poligon::CropHeightMap(map, first_x, first_z, size, size);
        physics_.AddHeightField(window.heights.data(), size, map.spacing,
                                config.origin + glm::vec3(static_cast<float>(first_x) * map.spacing, 0.0f,
                                                          static_cast<float>(first_z) * map.spacing));
        // Four fences keep the player on the part of the land that has collisions.
        const float fence = kPlayHalfSize - 5.0f;
        for (int side = 0; side < 4; ++side) {
            const bool along_x = side < 2;
            const float sign = side % 2 == 0 ? 1.0f : -1.0f;
            poligon::BodySettings settings;
            settings.position = along_x ? glm::vec3(sign * fence, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, sign * fence);
            physics_.AddBox(along_x ? glm::vec3(0.5f, 300.0f, fence) : glm::vec3(fence, 300.0f, 0.5f), settings);
        }
        first_shown_body_id_ = physics_.GetLastBodyId() + 1;
    }

    // keep_origin: leave the model where its file put it sideways (a tree's trunk stands at
    // the file's origin, while the middle of its crown may be well off to one side).
    // lod_name: the folder below poligon-assets/lods that holds the two simpler levels of
    // detail (made by tools/assets/make_tree.mjs or simplify_model.py); nullptr for kinds
    // without. `levels` says how far each level is used.
    Kind LoadKind(const char* relative_folder, bool split, bool is_small, bool keep_origin = false,
                  const char* lod_name = nullptr, const LevelDistances& levels = kTreeLevels) {
        Kind kind;
        kind.is_small = is_small;
        const std::filesystem::path file = FindGltf(assets_ / relative_folder);
        poligon::Model model;
        std::string error;
        if (file.empty() || !poligon::LoadModelFromFile(file.string().c_str(), model, error)) {
            std::printf("forest_scene: skipping %s: %s\n", relative_folder, file.empty() ? "no .gltf file" : error.c_str());
            return kind;
        }
        render_models_.push_back(poligon::UploadModel(renderer_, model));
        const poligon::RenderModel& render_model = render_models_.back();

        // The box around each instance.
        const size_t count = model.instances.size();
        std::vector<Box> boxes(count);
        for (size_t i = 0; i < count; ++i) {
            const poligon::ModelInstance& instance = model.instances[i];
            for (const poligon::MeshVertex& vertex : model.meshes[static_cast<size_t>(instance.mesh_index)].vertices) {
                const glm::vec3 position = glm::vec3(instance.transform * glm::vec4(vertex.position, 1.0f));
                boxes[i].min = glm::min(boxes[i].min, position);
                boxes[i].max = glm::max(boxes[i].max, position);
            }
        }

        // Group instances whose boxes overlap when seen from above: trunk and leaves of one
        // tree end up together, neighboring plants in a row do not.
        std::vector<size_t> group(count);
        std::iota(group.begin(), group.end(), size_t{0});
        const auto find_group = [&group](size_t i) {
            while (group[i] != i) {
                i = group[i];
            }
            return i;
        };
        for (size_t a = 0; a < count; ++a) {
            for (size_t b = a + 1; b < count; ++b) {
                const bool overlap = boxes[a].min.x < boxes[b].max.x && boxes[b].min.x < boxes[a].max.x &&
                                     boxes[a].min.z < boxes[b].max.z && boxes[b].min.z < boxes[a].max.z;
                if (!split || overlap) {
                    group[find_group(b)] = find_group(a);
                }
            }
        }

        for (size_t root = 0; root < count; ++root) {
            if (find_group(root) != root) {
                continue;
            }
            Box box;
            for (size_t i = 0; i < count; ++i) {
                if (find_group(i) == root) {
                    box.min = glm::min(box.min, boxes[i].min);
                    box.max = glm::max(box.max, boxes[i].max);
                }
            }
            // Move the group so that the middle of its foot is at the origin.
            const glm::mat4 to_foot = glm::translate(
                glm::mat4(1.0f), {keep_origin ? 0.0f : -(box.min.x + box.max.x) * 0.5f, -box.min.y,
                                  keep_origin ? 0.0f : -(box.min.z + box.max.z) * 0.5f});
            Variant variant;
            std::vector<std::string> node_names;
            variant.height = box.max.y - box.min.y;
            variant.half_width = {(box.max.x - box.min.x) * 0.5f, (box.max.z - box.min.z) * 0.5f};
            for (size_t i = 0; i < count; ++i) {
                if (find_group(i) != root) {
                    continue;
                }
                const size_t mesh_index = static_cast<size_t>(model.instances[i].mesh_index);
                Piece piece;
                piece.mesh = render_model.meshes[mesh_index];
                piece.material = render_model.materials[static_cast<size_t>(render_model.mesh_material_index[mesh_index])];
                piece.local = to_foot * model.instances[i].transform;
                variant.pieces.push_back(piece);
                node_names.push_back(model.instances[i].node_name);
            }

            // Register the variant with the vegetation system: its meshes, and from how far each is used.
            poligon::VegetationSpecies species;
            poligon::VegetationLevel full;
            for (const Piece& piece : variant.pieces) {
                full.parts.push_back({piece.mesh, piece.material, piece.local});
            }
            if (lod_name != nullptr) {
                full.max_distance = levels.full;
                species.levels.push_back(full);
                const float distances[2] = {levels.medium, levels.low};
                for (int level = 1; level <= 2; ++level) {
                    poligon::VegetationLevel simpler;
                    simpler.parts = LoadLodParts(lod_name, level, to_foot, model, render_model, node_names);
                    simpler.max_distance = distances[level - 1];
                    if (simpler.parts.empty()) {
                        // No simpler mesh: keep drawing the previous level out to this distance.
                        species.levels.back().max_distance = simpler.max_distance;
                    } else {
                        species.levels.push_back(std::move(simpler));
                    }
                }
                // The farthest level: one card with pictures of the tree.
                poligon::VegetationLevel far;
                far.impostor = LoadImpostor(lod_name, levels);
                if (far.impostor.id != 0) {
                    far.max_distance = levels.impostor;
                    species.levels.push_back(far);
                } else {
                    species.levels.back().max_distance = levels.low_without_impostor;
                }
                species.shadow_distance = is_small ? 20.0f : kShadowDistance;
                species.shadow_level_bias = 1;
            } else {
                full.max_distance = is_small ? kSmallPlantDistance : kMediumThingDistance;
                species.levels.push_back(full);
                // Grass and the like are too small for their shadows to matter beyond a few steps.
                species.shadow_distance = is_small ? 20.0f : kShadowDistance;
            }
            variant.species = vegetation_.AddSpecies(species);
            kind.variants.push_back(std::move(variant));
        }
        std::printf("forest_scene: %s: %zu variant(s), %lld triangles\n", relative_folder, kind.variants.size(),
                    static_cast<long long>(model.GetTriangleCount()));
        std::fflush(stdout);
        ++loaded_kind_count_;
        return kind;
    }

    // The impostor baked for a kind, if `levels` asks for one and its file exists.
    poligon::ImpostorHandle LoadImpostor(const char* lod_name, const LevelDistances& levels) {
        if (levels.impostor <= 0.0f) {
            return {};
        }
        const std::filesystem::path file = assets_ / "impostors" / (std::string(lod_name) + ".impostor");
        poligon::Impostor impostor;
        std::string error;
        if (!std::filesystem::exists(file) || !poligon::LoadImpostor(file.string().c_str(), impostor, error)) {
            std::printf("forest_scene: %s has no impostor (%s)\n", lod_name, error.empty() ? "no file" : error.c_str());
            return {};
        }
        return renderer_.CreateImpostor(impostor);
    }

    // The meshes of one simpler level of a variant: those of the simpler file's nodes named
    // like the variant's nodes in the full file (a file can hold several variants). They use
    // the materials of the full model, matched by name, so no texture is loaded twice.
    std::vector<poligon::VegetationPart> LoadLodParts(const char* lod_name, int level, const glm::mat4& to_foot,
                                                      const poligon::Model& full_model,
                                                      const poligon::RenderModel& full_render_model,
                                                      const std::vector<std::string>& node_names) {
        std::vector<poligon::VegetationPart> parts;
        const std::filesystem::path file = FindGltf(assets_ / "lods" / lod_name / ("lod" + std::to_string(level)));
        poligon::Model model;
        std::string error;
        if (file.empty() || !poligon::LoadModelFromFile(file.string().c_str(), model, error, false)) {
            std::printf("forest_scene: %s has no level %d (%s)\n", lod_name, level, file.empty() ? "no file" : error.c_str());
            return parts;
        }
        for (const poligon::ModelInstance& instance : model.instances) {
            if (std::find(node_names.begin(), node_names.end(), instance.node_name) == node_names.end()) {
                continue;
            }
            const poligon::ModelMesh& mesh = model.meshes[static_cast<size_t>(instance.mesh_index)];
            const std::string& material_name = model.materials[static_cast<size_t>(mesh.material_index)].name;
            size_t material_index = 0;
            for (size_t i = 0; i < full_model.materials.size(); ++i) {
                if (full_model.materials[i].name == material_name) {
                    material_index = i;
                    break;
                }
            }
            poligon::VegetationPart part;
            part.mesh = renderer_.CreateMesh(mesh.vertices.data(), static_cast<int>(mesh.vertices.size()), mesh.indices.data(),
                                             static_cast<int>(mesh.indices.size()));
            part.material = full_render_model.materials[material_index];
            part.local = to_foot * instance.transform;
            parts.push_back(part);
        }
        return parts;
    }

    void LoadKinds() {
        render_models_.reserve(32);
        // Trees made with tools/assets/make_tree.mjs from the recipes in tools/assets/trees.
        pine_ = LoadKind("generated/pine", false, false, true, "pine");
        spruce_ = LoadKind("generated/spruce", false, false, true, "spruce");
        birch_ = LoadKind("generated/birch", false, false, true, "birch");
        oak_ = LoadKind("generated/oak", false, false, true, "oak");
        young_spruce_ = LoadKind("generated/spruce_young", false, false, true, "spruce_young", kYoungTreeLevels);
        young_pine_ = LoadKind("generated/pine_young", false, false, true, "pine_young", kYoungTreeLevels);
        // Simpler levels of downloaded models: tools/assets/simplify_model.py.
        shrub_ = LoadKind("polyhaven/shrub_02", true, false, false, "shrub_02", kThingLevels);
        low_shrub_ = LoadKind("polyhaven/shrub_03", true, true);
        fern_ = LoadKind("polyhaven/fern_02", true, true);
        nettle_ = LoadKind("polyhaven/nettle_plant", true, true);
        grass_ = LoadKind("polyhaven/grass_bermuda_01", true, true);
        boulder_ = LoadKind("polyhaven/boulder_01", false, false, false, "boulder_01", kThingLevels);
        rocks_ = LoadKind("polyhaven/rock_moss_set_01", true, false, false, "rock_moss_set_01", kThingLevels);
        stump_ = LoadKind("polyhaven/tree_stump_01", false, false, false, "tree_stump_01", kThingLevels);
        fallen_trunk_ = LoadKind("polyhaven/dead_tree_trunk_02", false, false, false, "dead_tree_trunk_02", kThingLevels);

        // Trunk radii measured at the foot of the generated models.
        pine_.collision = Collision::Trunk;
        pine_.trunk_radius = 0.4f;
        spruce_.collision = Collision::Trunk;
        spruce_.trunk_radius = 0.25f;
        birch_.collision = Collision::Trunk;
        birch_.trunk_radius = 0.14f;
        oak_.collision = Collision::Trunk;
        oak_.trunk_radius = 0.45f;
        boulder_.collision = Collision::Block;
        rocks_.collision = Collision::Block;
        stump_.collision = Collision::Block;
        fallen_trunk_.collision = Collision::Block;
    }

    // A small pile of crates in the clearing, to knock over.
    void AddCrates() {
        cube_mesh_ = renderer_.CreateCubeMesh();
        crate_material_.color = {0.55f, 0.38f, 0.20f, 1.0f};
        crate_material_.roughness = 0.9f;
        const glm::vec2 place{-4.0f, 4.0f};
        const float ground = Ground(place.x, place.y);
        for (int layer = 0; layer < 3; ++layer) {
            for (int i = 0; i < 3 - layer; ++i) {
                poligon::BodySettings settings;
                settings.dynamic = true;
                settings.mass = 8.0f;
                settings.friction = 0.7f;
                settings.position = {place.x + (static_cast<float>(i) + static_cast<float>(layer) * 0.5f) * (kCrateSize + 0.02f),
                                     ground + 0.1f + kCrateSize * (static_cast<float>(layer) + 0.5f), place.y};
                crates_.push_back(physics_.AddBox(glm::vec3(kCrateSize * 0.5f), settings));
            }
        }
    }

    // Throws a crate from where the player looks. Old thrown crates are removed, so their number stays bounded.
    void ThrowCrate() {
        const size_t max_crates = 40;
        if (crates_.size() >= max_crates) {
            physics_.RemoveBody(crates_[6]);  // the oldest thrown one; the first six are the pile
            crates_.erase(crates_.begin() + 6);
        }
        const glm::vec3 forward = camera_.GetForward();
        poligon::BodySettings settings;
        settings.dynamic = true;
        settings.mass = 8.0f;
        settings.friction = 0.7f;
        settings.bounciness = 0.2f;
        settings.position = camera_.position + forward * 0.9f;
        settings.velocity = forward * 12.0f + glm::vec3(0.0f, 2.0f, 0.0f);
        crates_.push_back(physics_.AddBox(glm::vec3(kCrateSize * 0.5f), settings));
    }

    // The first clip whose name contains one of the given words, tried in order; clip 0 if none does.
    int FindClipLike(std::initializer_list<const char*> words) const {
        for (const char* word : words) {
            for (int clip = 0; clip < fox_animations_->GetClipCount(); ++clip) {
                if (std::string(fox_animations_->GetClipName(clip)).find(word) != std::string::npos) {
                    return clip;
                }
            }
        }
        return 0;
    }

    // Foxes in the clearing: some walk or run around it, some stand. They follow the ground
    // but know nothing of each other. The realistic fox is used when it is there, otherwise
    // the simple sample one.
    void AddAnimals() {
        const std::filesystem::path candidates[] = {
            assets_ / "sketchfab" / "fox_realistic_demo" / "source" / "unpacked" / "source" / "FOX_DEMO.fbx",
            assets_ / "khronos" / "fox" / "Fox.glb",
        };
        poligon::Model model;
        std::string error = "no fox model in poligon-assets";
        std::error_code file_error;
        for (const std::filesystem::path& file : candidates) {
            if (std::filesystem::exists(file, file_error) &&
                poligon::LoadModelFromFile(file.string().c_str(), model, error) && !model.animations.empty()) {
                std::printf("forest_scene: fox model %s\n", file.filename().string().c_str());
                break;
            }
            model = poligon::Model{};
        }
        if (model.animations.empty()) {
            std::printf("forest_scene: no animals: %s\n", error.c_str());
            return;
        }
        fox_model_ = poligon::UploadModel(renderer_, model);
        fox_animations_ = std::make_unique<poligon::AnimationSet>(model.skeleton, model.animations);
        // The model is in centimeters and rather large for a fox.
        fox_scale_ = 0.45f / (model.bounds_max.y - model.bounds_min.y);

        const auto add = [this](int clip, float radius, float speed, float start_angle) -> Animal& {
            Animal animal;
            animal.player = std::make_unique<poligon::AnimationPlayer>(*fox_animations_);
            animal.player->Play(clip, 0.0f);
            animal.circle_radius = radius;
            animal.meters_per_second = speed;
            animal.start_angle = start_angle;
            animals_.push_back(std::move(animal));
            return animals_.back();
        };
        // Models name their clips differently; pick by what the name contains.
        const int walk = FindClipLike({"Walk"});
        const int run = FindClipLike({"Run", "Walk"});
        const int stand = FindClipLike({"Stand", "Survey", "Idle"});
        const int howl = FindClipLike({"Howl", "Stand", "Survey"});
        add(walk, 3.5f, run == walk ? 1.1f : 0.55f, 0.6f);
        add(run, 5.2f, run == walk ? -1.1f : -3.2f, 2.5f);
        Animal& watcher = add(stand, 0.0f, 0.0f, 0.0f);
        watcher.place = {-1.5f, -2.5f};
        watcher.facing_degrees = 140.0f;
        Animal& howler = add(howl, 0.0f, 0.0f, 0.0f);
        howler.place = {1.8f, 1.2f};
        howler.facing_degrees = 250.0f;
        for (Animal& animal : animals_) {
            PlaceAnimal(animal);
        }
        std::printf("forest_scene: %zu foxes\n", animals_.size());
    }

    // Where an animal is at the current time. It depends on the time alone, so a run is
    // the same every time.
    void PlaceAnimal(Animal& animal) const {
        glm::vec2 place = animal.place;
        float facing = glm::radians(animal.facing_degrees);
        if (animal.circle_radius > 0.0f) {
            const float angle = animal.start_angle + time_seconds_ * animal.meters_per_second / animal.circle_radius;
            place = {std::cos(angle) * animal.circle_radius, std::sin(angle) * animal.circle_radius};
            // Along the circle, in the direction of travel. The fox model looks along +Z.
            const float direction = animal.meters_per_second >= 0.0f ? 1.0f : -1.0f;
            facing = std::atan2(-std::sin(angle) * direction, std::cos(angle) * direction);
        }
        animal.transform = glm::translate(glm::mat4(1.0f), {place.x, Ground(place.x, place.y), place.y}) *
                           glm::rotate(glm::mat4(1.0f), facing, {0.0f, 1.0f, 0.0f}) *
                           glm::scale(glm::mat4(1.0f), glm::vec3(fox_scale_));
    }

    // Puts one random variant of `kind` at (x, z), standing on the ground, turned at random.
    // `sink` pushes it into the ground by that part of its height (rocks, stumps).
    void Place(const Kind& kind, Random& random, float x, float z, float scale, float sink = 0.0f) {
        if (kind.variants.empty()) {
            return;
        }
        const size_t index = std::min(static_cast<size_t>(random.Next() * static_cast<float>(kind.variants.size())),
                                      kind.variants.size() - 1);
        const Variant& variant = kind.variants[index];
        // Nothing grows or lies on slopes this steep; in Batorz those are the walls of gullies.
        if (terrain_->GetSlopeDegrees(x, z) > 33.0f) {
            return;
        }
        Placement placement;
        placement.variant = &variant;
        placement.position = {x, Ground(x, z) - variant.height * scale * sink, z};
        const float turn = random.Between(0.0f, 6.2831853f);
        placement.transform = glm::translate(glm::mat4(1.0f), placement.position) *
                              glm::rotate(glm::mat4(1.0f), turn, {0.0f, 1.0f, 0.0f}) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(scale));

        // What blocks the way gets a body in the physics world, where the player can walk.
        const bool in_play_area = std::fabs(x) < kPlayHalfSize && std::fabs(z) < kPlayHalfSize;
        poligon::BodySettings body;
        body.rotation = glm::angleAxis(turn, glm::vec3(0.0f, 1.0f, 0.0f));
        if (!in_play_area) {
            // nothing to collide with out there
        } else if (kind.collision == Collision::Trunk) {
            const float half_height = std::min(variant.height * scale * 0.5f, 4.0f);
            body.position = placement.position + glm::vec3(0.0f, half_height - 0.5f, 0.0f);
            physics_.AddCylinder(half_height, kind.trunk_radius * scale, body);
        } else if (kind.collision == Collision::Block) {
            // Somewhat smaller than the box around the model, whose corners are mostly air.
            const glm::vec3 half_size{variant.half_width.x * scale * 0.8f, variant.height * scale * 0.45f,
                                      variant.half_width.y * scale * 0.8f};
            body.position = placement.position + glm::vec3(0.0f, variant.height * scale * 0.5f, 0.0f);
            physics_.AddBox(half_size, body);
        }
        placement.is_small = kind.is_small;
        vegetation_.AddInstance(variant.species, placement.transform);
        ++placed_count_;
    }

    // Scatters `count` plants evenly at random over a square around the middle.
    void Scatter(const Kind& kind, Random& random, int count, float half_size, float scale_low, float scale_high,
                 float sink = 0.0f) {
        for (int i = 0; i < count; ++i) {
            const float x = random.Between(-half_size, half_size);
            const float z = random.Between(-half_size, half_size);
            Place(kind, random, x, z, random.Between(scale_low, scale_high), sink);
        }
    }

    void PlantForest() {
        Random random(20261014u);

        // Big trees: one per 9 m square, shifted at random within it, with gaps. Two places
        // stay open: a clearing on the hilltop where the walk starts, and a wide wedge to
        // the east of it, so that the valley can be seen from the top.
        // Beyond kForestHalfSize the big trees go on alone, out to kFarForestHalfSize; the
        // wedge to the east stays open for 400 m, so the far forest shows in the panorama.
        const float spacing = 9.0f;
        for (float cell_z = -kFarForestHalfSize; cell_z < kFarForestHalfSize; cell_z += spacing) {
            for (float cell_x = -kFarForestHalfSize; cell_x < kFarForestHalfSize; cell_x += spacing) {
                const float x = cell_x + random.Between(1.0f, spacing - 1.0f);
                const float z = cell_z + random.Between(1.0f, spacing - 1.0f);
                const float choice = random.Next();
                const float scale = random.Between(0.8f, 1.2f);
                const bool in_clearing = x * x + z * z < 14.0f * 14.0f;
                const bool in_view_wedge = x > 5.0f && x < 400.0f && std::fabs(z) < x * 0.75f;
                const bool near_start = std::fabs(x) < kForestHalfSize && std::fabs(z) < kForestHalfSize;
                if (in_clearing || in_view_wedge || choice < 0.22f) {
                    continue;
                }
                // Of the trees: about half pines, the rest spruces, birches and oaks.
                if (choice < 0.60f) {
                    Place(pine_, random, x, z, scale);
                } else if (choice < 0.72f) {
                    Place(spruce_, random, x, z, scale);
                } else if (choice < 0.86f) {
                    Place(birch_, random, x, z, scale);
                } else {
                    Place(oak_, random, x, z, scale);
                }
                // Something at the foot of some trees, near the start.
                if (random.Next() < 0.35f && near_start) {
                    Place(fern_, random, x + random.Between(-1.2f, 1.2f), z + random.Between(-1.2f, 1.2f),
                          random.Between(1.0f, 1.6f));
                }
            }
        }

        // The forest now covers 500 x 500 m, twelve times the area it had on the flat test
        // ground. Undergrowth is dense near the start and thinner over the whole area.
        Scatter(young_spruce_, random, 350, kForestHalfSize, 0.5f, 1.3f);
        Scatter(young_pine_, random, 350, kForestHalfSize, 0.5f, 1.3f);
        Scatter(shrub_, random, 500, kForestHalfSize, 0.7f, 1.2f);
        Scatter(low_shrub_, random, 150, 50.0f, 1.2f, 2.0f);
        Scatter(low_shrub_, random, 1200, kForestHalfSize, 1.2f, 2.0f);
        Scatter(fern_, random, 350, 55.0f, 0.9f, 1.7f);
        Scatter(fern_, random, 3500, kForestHalfSize, 0.9f, 1.7f);
        Scatter(nettle_, random, 160, 45.0f, 2.5f, 4.0f);
        Scatter(nettle_, random, 1200, kForestHalfSize, 2.5f, 4.0f);
        Scatter(grass_, random, 1800, 35.0f, 2.0f, 3.5f);
        Scatter(grass_, random, 9000, kForestHalfSize, 2.0f, 3.5f);
        Scatter(boulder_, random, 120, kForestHalfSize, 0.6f, 2.2f, 0.25f);
        Scatter(rocks_, random, 200, kForestHalfSize, 0.6f, 1.4f, 0.15f);
        Scatter(stump_, random, 110, kForestHalfSize, 0.8f, 1.2f, 0.05f);
        Scatter(fallen_trunk_, random, 80, kForestHalfSize, 0.9f, 1.4f, 0.1f);
        std::printf("forest_scene: %d things placed\n", placed_count_);
    }

    poligon::Renderer& renderer_;
    std::filesystem::path assets_;
    const poligon::Input& input_;
    ForestActions actions_;

    std::unique_ptr<poligon::Terrain> terrain_;
    std::vector<poligon::RenderModel> render_models_;
    Kind pine_, spruce_, birch_, oak_, young_spruce_, young_pine_, shrub_, low_shrub_, fern_, nettle_, grass_, boulder_,
        rocks_, stump_, fallen_trunk_;
    int loaded_kind_count_ = 0;
    // All plants, rocks and stumps: placed once, drawn by distance.
    poligon::Vegetation vegetation_;
    int placed_count_ = 0;
    float detail_scale_ = 1.0f;
    float shadow_scale_ = 1.0f;

    poligon::RenderModel fox_model_;
    float fox_scale_ = 1.0f;
    // Players refer to the set, so it is declared before them and destroyed after them.
    std::unique_ptr<poligon::AnimationSet> fox_animations_;
    std::vector<Animal> animals_;
    float time_seconds_ = 0.0f;

    // Ground, trunks, rocks, crates and the walking player.
    poligon::PhysicsWorld physics_;
    poligon::CharacterHandle walker_;
    std::vector<poligon::BodyHandle> crates_;
    poligon::MeshHandle cube_mesh_;
    poligon::Material crate_material_;
    // Bodies below this id are the ground and the fences, which the collision view leaves out.
    std::uint32_t first_shown_body_id_ = 1;
    bool show_collision_ = false;

    poligon::RenderSettings settings_;
    std::string settings_path_;
    poligon::FreeCamera camera_;
    bool flying_ = false;
    bool show_windows_ = true;
};

// The look the forest starts with: late afternoon, a warm and fairly low sun, so that
// trunks throw long shadows through the haze.
poligon::RenderSettings MakeForestSettings() {
    poligon::RenderSettings settings;
    settings.sun.direction = {-0.62f, -0.48f, -0.62f};
    settings.sun.color = {3.4f, 2.9f, 2.2f, 1.0f};

    poligon::Environment& environment = settings.environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    environment.sky_zenith = {0.18f, 0.34f, 0.72f, 1.0f};
    environment.sky_horizon = {0.72f, 0.76f, 0.80f, 1.0f};
    environment.sky_ground = {0.20f, 0.19f, 0.16f, 1.0f};
    environment.shadow_distance = kShadowDistance;
    environment.ambient_occlusion = true;
    environment.ambient_occlusion_radius = 0.5f;
    environment.volumetric_fog = true;
    // Mist lies in the valley, 70 m below the start, and thins out towards the hilltops,
    // so the view from the top stays clear.
    environment.fog_density = 0.0011f;
    environment.fog_color = {0.78f, 0.80f, 0.80f, 1.0f};
    environment.fog_height_falloff = 0.03f;
    environment.fog_base_height = -60.0f;
    environment.fog_shadow_brightness = 0.22f;
    environment.fog_sun_glow = 1.2f;

    settings.post.bloom = true;
    settings.post.auto_exposure = true;
    settings.post.auto_exposure_target = 0.22f;
    return settings;
}

} // namespace

int main(int argc, char** argv) {
    poligon::InstallCrashHandler();
    const bool started_by_hand = argc == 1;

    poligon::CommandLineOptions options;
    std::string error;
    // Default: on the hilltop, looking east over the valley.
    float view[4] = {0.0f, 0.0f, 270.0f, -6.0f};
    if (poligon::ParseCommandLine(argc, argv, options, error)) {
        if (options.positional.size() > 5 || (options.positional.size() == 5 && options.positional[4] != "collision")) {
            error = "expected at most X Z TURN TILT and the word collision";
        }
        for (size_t i = 0; i < options.positional.size() && i < 4 && error.empty(); ++i) {
            char* end = nullptr;
            view[i] = std::strtof(options.positional[i].c_str(), &end);
            if (end == options.positional[i].c_str() || *end != '\0') {
                error = "\"" + options.positional[i] + "\" is not a number";
            }
        }
    }
    if (!error.empty()) {
        std::fprintf(stderr,
                     "forest_scene: %s\n"
                     "usage: forest_scene [X Z TURN TILT [collision]] [--headless --ticks N] [--backend d3d12|vulkan]\n"
                     "                    [--gpu-validation] [--dump-frame PATH.png]\n",
                     error.c_str());
        return 2;
    }
    if (options.headless && options.ticks == 0) {
        options.ticks = 1;
    }

    const std::filesystem::path assets = FindAssetsFolder(argv[0]);
    if (assets.empty()) {
        const char* message = "No folder named poligon-assets found next to the project folder.";
        std::fprintf(stderr, "forest_scene: %s\n", message);
        if (started_by_hand) {
            poligon::ShowMessageBox("Poligon forest scene", message);
        }
        return 1;
    }

    poligon::RendererConfig renderer_config;
    renderer_config.headless = options.headless;
    renderer_config.backend = options.backend;
    renderer_config.enable_validation = options.gpu_validation;
    renderer_config.window_title = "Poligon forest scene - loading...";
    poligon::RenderSettings settings = MakeForestSettings();
    // Where the window's Save button writes; a file given on the command line is both read and written.
    std::string settings_path = (assets / "forest_render_settings.txt").string();
    if (!options.render_settings_path.empty()) {
        settings_path = options.render_settings_path;
        if (!poligon::LoadRenderSettings(settings_path.c_str(), settings, error)) {
            std::fprintf(stderr, "forest_scene: %s\n", error.c_str());
            return 2;
        }
    }
    renderer_config.post = settings.post;
    renderer_config.debug_ui = true;
    // Shadows are the costliest part of this scene: every leaf is drawn into four shadow
    // images. At 1024 instead of the default 2048 pixels they cost less than half, which
    // brings the frame under 17 ms on a GTX 980; the price is softer shadow edges.
    renderer_config.shadow_map_size = 1024;
    poligon::Renderer renderer(renderer_config);

    poligon::Input input;
    const ForestActions actions = SetUpInput(input);
    if (!options.headless) {
        input.UseLiveDevices();
    }

    const auto load_start = std::chrono::steady_clock::now();
    ForestGame game(renderer, assets, input, actions, view, settings, settings_path);
    game.ShowCollision(options.positional.size() == 5);
    const double load_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - load_start).count();
    std::printf("forest_scene: loaded in %.1f s\n", load_seconds);
    if (game.GetLoadedKindCount() == 0) {
        const char* message = "None of the forest models could be loaded from poligon-assets.";
        std::fprintf(stderr, "forest_scene: %s\n", message);
        if (started_by_hand) {
            poligon::ShowMessageBox("Poligon forest scene", message);
        }
        return 1;
    }
#ifdef NDEBUG
    renderer.SetWindowTitle("Poligon forest scene  (W/S/A/D walk, Shift run, F fly, Tab windows, drag the mouse to look)");
#else
    renderer.SetWindowTitle("Poligon forest scene  [DEBUG BUILD, slow: use build/release/bin]");
#endif

    poligon::GameLoopConfig loop_config;
    loop_config.headless = options.headless;
    loop_config.max_ticks = options.ticks;
    loop_config.render_every_iteration = !options.headless;
    loop_config.input = &input;

    poligon::GameLoop loop(loop_config);
    const auto run_start = std::chrono::steady_clock::now();
    const poligon::GameLoopResult result = loop.Run(game);

    const glm::vec3 standing = game.GetCameraPosition();
    std::printf("camera at %.3f %.3f %.3f\n", static_cast<double>(standing.x), static_cast<double>(standing.y),
                static_cast<double>(standing.z));
    const poligon::FrameStats stats = renderer.GetFrameStats();
    std::printf("last frame: %d meshes submitted, %d drawn, %d shadow draws\n", stats.meshes_submitted,
                stats.meshes_drawn, stats.shadow_draws);
    std::printf("instances: %d submitted, %d in view, %d in shadow cascades, %d draw calls\n", stats.instances_submitted,
                stats.instances_drawn, stats.shadow_instances, stats.instance_draw_calls);
    if (!options.dump_frame_path.empty()) {
        if (result.frame_count == 0 || !renderer.SaveFrameToPng(options.dump_frame_path.c_str())) {
            std::fprintf(stderr, "forest_scene: cannot write %s\n", options.dump_frame_path.c_str());
            return 1;
        }
        std::printf("frame saved to %s\n", options.dump_frame_path.c_str());
    }
    // Reading the last frame back waits until the graphics card has really finished all
    // frames; without it the time below would only be that of handing the work over.
    if (result.frame_count > 0 && options.headless) {
        poligon::Image discarded;
        renderer.CaptureFrame(discarded);
    }
    const double run_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - run_start).count();
    if (result.frame_count > 0) {
        std::printf("%lld frames in %.2f s: %.1f ms per frame\n", static_cast<long long>(result.frame_count), run_seconds,
                    run_seconds * 1000.0 / static_cast<double>(result.frame_count));
    }
    return 0;
}
