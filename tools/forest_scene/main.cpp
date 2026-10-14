// Showcase: a piece of forest put together from the models in the poligon-assets folder,
// to walk through and look at. It exists to judge the renderer on a real scene and to
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
#include "poligon/input.h"
#include "poligon/model.h"
#include "poligon/physics.h"
#include "poligon/platform.h"
#include "poligon/render_settings.h"
#include "poligon/vegetation.h"
#include "poligon/renderer.h"

namespace {

constexpr float kForestHalfSize = 70.0f;   // trees stand within this distance of the middle
constexpr float kGroundHalfSize = 150.0f;  // the ground reaches further, to the fog
constexpr float kEyeHeight = 1.7f;
constexpr float kCrateSize = 0.6f;
// How far each kind of thing is drawn (meters). Trees have simpler meshes for the two
// longer ranges; everything else has one mesh and simply stops being drawn.
constexpr float kTreeFullDetail = 28.0f;
constexpr float kTreeMediumDetail = 70.0f;
constexpr float kTreeLowDetail = 450.0f;
constexpr float kSmallPlantDistance = 45.0f;
constexpr float kMediumThingDistance = 150.0f;
constexpr float kShadowDistance = 90.0f;

// Height of the ground at a point: a few gentle waves.
float GroundHeight(float x, float z) {
    return 1.2f * std::sin(x * 0.045f + 1.3f) * std::cos(z * 0.038f) + 0.5f * std::sin(x * 0.11f + z * 0.09f) +
           0.15f * std::sin(x * 0.31f) * std::sin(z * 0.27f + 2.0f);
}

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
        camera_.position = {view[0], GroundHeight(view[0], view[1]) + kEyeHeight, view[1]};
        camera_.yaw_degrees = view[2];
        camera_.pitch_degrees = view[3];
        camera_.vertical_fov_degrees = 65.0f;
        CreateGround();
        LoadKinds();
        PlantForest();
        AddAnimals();
        AddCrates();

        poligon::CharacterSettings walker;
        walker.position = {view[0], GroundHeight(view[0], view[1]), view[1]};
        walker_ = physics_.AddCharacter(walker);
        std::printf("forest_scene: %d physics bodies\n", physics_.GetBodyCount());
    }

    int GetLoadedKindCount() const { return loaded_kind_count_; }
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
        clip_planes.far_plane = 600.0f;
        const poligon::Camera camera = camera_.ToCamera(clip_planes);

        renderer_.SetPostSettings(settings_.post);
        renderer_.BeginFrame(camera, settings_.sun, settings_.environment);
        if (show_windows_) {
            poligon::ShowRenderSettingsWindow(renderer_, settings_, settings_path_.c_str());
            poligon::ShowFrameStatsWindow(renderer_);
        }
        renderer_.DrawMesh(ground_mesh_, glm::mat4(1.0f), ground_material_);
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

    // The ground: a grid of 1 m squares following GroundHeight, with one texture repeated every 3 m.
    void CreateGround() {
        const int cells = static_cast<int>(kGroundHalfSize) * 2;
        const int row = cells + 1;
        std::vector<poligon::MeshVertex> vertices(static_cast<size_t>(row) * row);
        for (int j = 0; j < row; ++j) {
            for (int i = 0; i < row; ++i) {
                const float x = static_cast<float>(i) - kGroundHalfSize;
                const float z = static_cast<float>(j) - kGroundHalfSize;
                poligon::MeshVertex& vertex = vertices[static_cast<size_t>(j) * row + i];
                vertex.position = {x, GroundHeight(x, z), z};
                const float slope_x = GroundHeight(x + 0.5f, z) - GroundHeight(x - 0.5f, z);
                const float slope_z = GroundHeight(x, z + 0.5f) - GroundHeight(x, z - 0.5f);
                vertex.normal = glm::normalize(glm::vec3(-slope_x, 1.0f, -slope_z));
                vertex.uv = {x / 3.0f, z / 3.0f};
            }
        }
        std::vector<std::uint32_t> indices;
        indices.reserve(static_cast<size_t>(cells) * cells * 6);
        for (int j = 0; j < cells; ++j) {
            for (int i = 0; i < cells; ++i) {
                const std::uint32_t corner = static_cast<std::uint32_t>(j * row + i);
                const std::uint32_t next_row = corner + static_cast<std::uint32_t>(row);
                indices.insert(indices.end(), {corner, next_row, corner + 1, corner + 1, next_row, next_row + 1});
            }
        }
        ground_mesh_ = renderer_.CreateMesh(vertices.data(), static_cast<int>(vertices.size()), indices.data(),
                                            static_cast<int>(indices.size()));

        // The same triangles are the ground of the physics world.
        std::vector<glm::vec3> positions;
        positions.reserve(vertices.size());
        for (const poligon::MeshVertex& vertex : vertices) {
            positions.push_back(vertex.position);
        }
        physics_.AddMesh(positions.data(), static_cast<int>(positions.size()), indices.data(),
                         static_cast<int>(indices.size()), poligon::BodySettings{});
        // Four fences keep everything inside the part of the ground that has forest around it.
        const float fence = kGroundHalfSize - 20.0f;
        for (int side = 0; side < 4; ++side) {
            const bool along_x = side < 2;
            const float sign = side % 2 == 0 ? 1.0f : -1.0f;
            poligon::BodySettings settings;
            settings.position = along_x ? glm::vec3(sign * fence, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, sign * fence);
            physics_.AddBox(along_x ? glm::vec3(0.5f, 50.0f, fence) : glm::vec3(fence, 50.0f, 0.5f), settings);
        }
        first_shown_body_id_ = physics_.GetLastBodyId() + 1;

        const std::filesystem::path folder = assets_ / "polyhaven" / "textures" / "forrest_ground_01";
        ground_material_.texture = LoadTexture(folder / "forrest_ground_01_diff_2k.jpg", true);
        ground_material_.normal_texture = LoadTexture(folder / "forrest_ground_01_nor_gl_2k.jpg", false);
        ground_material_.orm_texture = LoadTexture(folder / "forrest_ground_01_arm_2k.jpg", false);
        ground_material_.occlusion_in_orm = ground_material_.orm_texture.id != 0;
        ground_material_.roughness = 1.0f;
        if (ground_material_.texture.id == 0) {
            ground_material_.color = {0.25f, 0.22f, 0.14f, 1.0f};
        }
    }

    // Loads one model file and cuts it into variants. With `split`, instances that stand
    // apart from each other become separate variants; otherwise the whole file is one.
    // keep_origin: leave the model where its file put it sideways (a tree's trunk stands at
    // the file's origin, while the middle of its crown may be well off to one side).
    // lod_name: for trees, the folder below poligon-assets/lods that holds the two simpler
    // levels of detail (made by tools/assets/simplify_model.py); nullptr for kinds without.
    Kind LoadKind(const char* relative_folder, bool split, bool is_small, bool keep_origin = false,
                  const char* lod_name = nullptr) {
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
            }

            // Register the variant with the vegetation system: its meshes, and from how far each is used.
            poligon::VegetationSpecies species;
            poligon::VegetationLevel full;
            for (const Piece& piece : variant.pieces) {
                full.parts.push_back({piece.mesh, piece.material, piece.local});
            }
            if (lod_name != nullptr) {
                full.max_distance = kTreeFullDetail;
                species.levels.push_back(full);
                const float distances[2] = {kTreeMediumDetail, kTreeLowDetail};
                for (int level = 1; level <= 2; ++level) {
                    poligon::VegetationLevel simpler;
                    simpler.parts = LoadLodParts(lod_name, level, to_foot, model, render_model);
                    simpler.max_distance = distances[level - 1];
                    if (simpler.parts.empty()) {
                        // No simpler mesh: keep drawing the previous level out to this distance.
                        species.levels.back().max_distance = simpler.max_distance;
                    } else {
                        species.levels.push_back(std::move(simpler));
                    }
                }
                species.shadow_distance = kShadowDistance;
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

    // The meshes of one simpler level of a tree. They use the materials of the full model,
    // matched by name, so no texture is loaded twice.
    std::vector<poligon::VegetationPart> LoadLodParts(const char* lod_name, int level, const glm::mat4& to_foot,
                                                      const poligon::Model& full_model,
                                                      const poligon::RenderModel& full_render_model) {
        std::vector<poligon::VegetationPart> parts;
        const std::filesystem::path file = FindGltf(assets_ / "lods" / lod_name / ("lod" + std::to_string(level)));
        poligon::Model model;
        std::string error;
        if (file.empty() || !poligon::LoadModelFromFile(file.string().c_str(), model, error, false)) {
            std::printf("forest_scene: %s has no level %d (%s)\n", lod_name, level, file.empty() ? "no file" : error.c_str());
            return parts;
        }
        for (const poligon::ModelInstance& instance : model.instances) {
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
        tall_pine_ = LoadKind("generated/scots_pine", false, false, true, "scots_pine");
        pine_ = LoadKind("generated/pine_stock", false, false, true, "pine_stock");
        broadleaf_ = LoadKind("simplified/jacaranda_tree", false, false, true, "jacaranda_tree");
        small_tree_ = LoadKind("simplified/island_tree_02", false, false, true, "island_tree_02");
        fir_sapling_ = LoadKind("simplified/fir_sapling", true, false);
        pine_sapling_ = LoadKind("simplified/pine_sapling_small", true, false);
        shrub_ = LoadKind("polyhaven/shrub_02", true, false);
        low_shrub_ = LoadKind("polyhaven/shrub_03", true, true);
        fern_ = LoadKind("polyhaven/fern_02", true, true);
        nettle_ = LoadKind("polyhaven/nettle_plant", true, true);
        grass_ = LoadKind("polyhaven/grass_bermuda_01", true, true);
        boulder_ = LoadKind("polyhaven/boulder_01", false, false);
        rocks_ = LoadKind("polyhaven/rock_moss_set_01", true, false);
        stump_ = LoadKind("polyhaven/tree_stump_01", false, false);
        fallen_trunk_ = LoadKind("polyhaven/dead_tree_trunk_02", false, false);

        tall_pine_.collision = Collision::Trunk;
        tall_pine_.trunk_radius = 0.32f;
        pine_.collision = Collision::Trunk;
        pine_.trunk_radius = 0.28f;
        broadleaf_.collision = Collision::Trunk;
        broadleaf_.trunk_radius = 0.6f;
        small_tree_.collision = Collision::Trunk;
        small_tree_.trunk_radius = 0.12f;
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
        const float ground = GroundHeight(place.x, place.y);
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
        animal.transform = glm::translate(glm::mat4(1.0f), {place.x, GroundHeight(place.x, place.y), place.y}) *
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
        Placement placement;
        placement.variant = &variant;
        placement.position = {x, GroundHeight(x, z) - variant.height * scale * sink, z};
        const float turn = random.Between(0.0f, 6.2831853f);
        placement.transform = glm::translate(glm::mat4(1.0f), placement.position) *
                              glm::rotate(glm::mat4(1.0f), turn, {0.0f, 1.0f, 0.0f}) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(scale));

        // What blocks the way gets a body in the physics world.
        poligon::BodySettings body;
        body.rotation = glm::angleAxis(turn, glm::vec3(0.0f, 1.0f, 0.0f));
        if (kind.collision == Collision::Trunk) {
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

        // Big trees: one per 9 m square, shifted at random within it, with gaps, and a
        // small clearing in the middle where the walk starts.
        const float spacing = 9.0f;
        for (float cell_z = -kForestHalfSize; cell_z < kForestHalfSize; cell_z += spacing) {
            for (float cell_x = -kForestHalfSize; cell_x < kForestHalfSize; cell_x += spacing) {
                const float x = cell_x + random.Between(1.0f, spacing - 1.0f);
                const float z = cell_z + random.Between(1.0f, spacing - 1.0f);
                const float choice = random.Next();
                const float scale = random.Between(0.8f, 1.2f);
                if (x * x + z * z < 6.0f * 6.0f || choice < 0.22f) {
                    continue;
                }
                if (choice < 0.50f) {
                    Place(tall_pine_, random, x, z, scale);
                } else if (choice < 0.96f) {
                    Place(pine_, random, x, z, scale);
                } else {
                    Place(broadleaf_, random, x, z, scale * 0.6f);
                }
                // Something at the foot of some trees.
                if (random.Next() < 0.35f) {
                    Place(fern_, random, x + random.Between(-1.2f, 1.2f), z + random.Between(-1.2f, 1.2f),
                          random.Between(1.0f, 1.6f));
                }
            }
        }

        Scatter(small_tree_, random, 6, kForestHalfSize, 1.2f, 1.8f);
        Scatter(fir_sapling_, random, 30, kForestHalfSize, 1.0f, 2.2f);
        Scatter(pine_sapling_, random, 30, kForestHalfSize, 1.0f, 2.2f);
        Scatter(shrub_, random, 45, kForestHalfSize, 0.7f, 1.2f);
        Scatter(low_shrub_, random, 150, 50.0f, 1.2f, 2.0f);
        Scatter(fern_, random, 350, 55.0f, 0.9f, 1.7f);
        Scatter(nettle_, random, 160, 45.0f, 2.5f, 4.0f);
        Scatter(grass_, random, 1800, 35.0f, 2.0f, 3.5f);
        Scatter(boulder_, random, 12, kForestHalfSize, 0.6f, 2.2f, 0.25f);
        Scatter(rocks_, random, 20, kForestHalfSize, 0.6f, 1.4f, 0.15f);
        Scatter(stump_, random, 10, kForestHalfSize, 0.8f, 1.2f, 0.05f);
        Scatter(fallen_trunk_, random, 7, kForestHalfSize, 0.9f, 1.4f, 0.1f);
        std::printf("forest_scene: %d things placed\n", placed_count_);
    }

    poligon::Renderer& renderer_;
    std::filesystem::path assets_;
    const poligon::Input& input_;
    ForestActions actions_;

    poligon::MeshHandle ground_mesh_;
    poligon::Material ground_material_;
    std::vector<poligon::RenderModel> render_models_;
    Kind tall_pine_, pine_, broadleaf_, small_tree_, fir_sapling_, pine_sapling_, shrub_, low_shrub_, fern_, nettle_,
        grass_, boulder_, rocks_, stump_, fallen_trunk_;
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
    environment.shadow_distance = 90.0f;
    environment.ambient_occlusion = true;
    environment.ambient_occlusion_radius = 0.5f;
    environment.volumetric_fog = true;
    environment.fog_density = 0.012f;
    environment.fog_color = {0.80f, 0.78f, 0.70f, 1.0f};
    environment.fog_height_falloff = 0.05f;
    environment.fog_base_height = 2.0f;
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
    // Default: in the clearing, looking towards the sun.
    float view[4] = {0.0f, 0.0f, 225.0f, 4.0f};
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
