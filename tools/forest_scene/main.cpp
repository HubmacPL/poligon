// Showcase: 6 x 6 km of land made by the engine's terrain generator, with a piece of
// forest above a lake, put together from the models in the poligon-assets folder, to walk
// through and look at. It exists to judge the renderer on a whole scene and to measure how
// long a frame takes. The land is generated on the first run (about two minutes) and then
// loaded from build/terrain_cache.
//
// Usage: forest_scene [X Z TURN TILT [collision]] [standard Poligon options]
//   X Z         where to stand, in meters from the start (default 0 0): a slope above
//               the largest lake, chosen so that the lake is in view
//   TURN TILT   where to look, in degrees (default: towards the lake, slightly down)
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
#include "poligon/graphics_quality.h"
#include "poligon/ground_cover.h"
#include "poligon/impostor.h"
#include "poligon/input.h"
#include "poligon/model.h"
#include "poligon/physics.h"
#include "poligon/platform.h"
#include "poligon/render_settings.h"
#include "poligon/terrain.h"
#include "poligon/vegetation.h"
#include "poligon/renderer.h"
#include "poligon/water.h"

namespace {

constexpr float kForestHalfSize = 250.0f;  // plants stand within this distance of the start
// Beyond that, big trees alone continue to this distance; far away they are impostors.
constexpr float kFarForestHalfSize = 1200.0f;
// Collisions exist within this distance of the start; fences stand at its edge.
constexpr float kPlayHalfSize = 500.0f;
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
// TerrainConfig::detail at quality 1 (high).
constexpr float kTerrainDetail = 2.0f;
// Grass, ferns, nettles, low shrubs and stones exist within this distance of the player.
constexpr float kGroundCoverRadius = 90.0f;
// Trees up to this far away still cast shadows into the shadow range: a 24 m pine with the
// sun 29 degrees up throws a 44 m shadow.
constexpr float kTreeShadowDistance = kShadowDistance + 45.0f;

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
    // The variant's number in the vegetation system, and what was registered there.
    int species = -1;
    poligon::VegetationSpecies definition;
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
    ForestGame(poligon::Renderer& renderer, const std::filesystem::path& assets, const std::filesystem::path& terrain_cache,
               const poligon::Input& input, const ForestActions& actions, const float view[4], bool turn_given,
               const poligon::RenderSettings& settings, const std::string& settings_path)
        : renderer_(renderer), assets_(assets), terrain_cache_(terrain_cache), input_(input), actions_(actions),
          settings_(settings), settings_path_(settings_path) {
        CreateGround();
        camera_.position = {view[0], Ground(view[0], view[1]) + kEyeHeight, view[1]};
        camera_.yaw_degrees = turn_given ? view[2] : start_yaw_;
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

    // Average processor time of EndFrame over the run, for the summary at the end.
    float GetAverageCpuMilliseconds() const {
        return cpu_frames_ > 0 ? static_cast<float>(cpu_milliseconds_sum_ / cpu_frames_) : 0.0f;
    }
    float GetAverageWaitMilliseconds() const {
        return cpu_frames_ > 0 ? static_cast<float>(wait_milliseconds_sum_ / cpu_frames_) : 0.0f;
    }

    void Render(poligon::GameLoop& /*loop*/, const poligon::FrameInfo& /*frame*/) override {
        if (cpu_frames_ >= 0) {
            // The statistics describe the previous frame; the first call has none.
            const poligon::FrameStats stats = renderer_.GetFrameStats();
            if (stats.cpu_milliseconds > 0.0f) {
                cpu_milliseconds_sum_ += stats.cpu_milliseconds;
                wait_milliseconds_sum_ += stats.wait_milliseconds;
                ++cpu_frames_;
            }
        }
        poligon::Camera clip_planes;
        clip_planes.near_plane = 0.1f;
        clip_planes.far_plane = 12000.0f;
        const poligon::Camera camera = camera_.ToCamera(clip_planes);

        ApplyQualityToSystems();
        // The scene's look, limited by the graphics quality.
        poligon::Environment environment = settings_.environment;
        poligon::PostSettings post = settings_.post;
        poligon::ApplyGraphicsQuality(settings_.quality, environment, post);
        renderer_.SetPostSettings(post);
        // The wind runs on game time, so a replay moves the plants exactly as the recording did.
        environment.wind_time = time_seconds_;
        renderer_.BeginFrame(camera, settings_.sun, environment);
        if (show_windows_) {
            poligon::ShowRenderSettingsWindow(renderer_, settings_, settings_path_.c_str());
            poligon::ShowFrameStatsWindow(renderer_);
        }
        terrain_->Draw(camera);
        for (const poligon::WaterSurface& lake : lakes_) {
            renderer_.DrawWater(lake.mesh, lake.transform, water_material_);
        }
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
        ground_cover_->Update(camera.position);
        ground_cover_->Draw(renderer_, camera);
        if (show_windows_ && renderer_.IsDebugUiActive()) {
            const poligon::VegetationStats& stats = vegetation_.GetStats();
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.0f, 140.0f), ImGuiCond_FirstUseEver,
                                    ImVec2(1.0f, 0.0f));
            if (ImGui::Begin("Vegetation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("%d plants and rocks placed", stats.instances_total);
                ImGui::Text("drawn: %d full, %d medium, %d low detail", stats.instances_per_level[0],
                            stats.instances_per_level[1], stats.instances_per_level[2]);
                ImGui::Text("cells: %d of %d skipped whole", stats.cells_skipped, stats.cells_total);
                const poligon::GroundCoverStats& cover = ground_cover_->GetStats();
                ImGui::Text("ground cover: %d cells, %d plants and stones", cover.cells_alive, cover.instances_alive);
                const poligon::FrameStats frame = renderer_.GetFrameStats();
                ImGui::Text("last frame: %d draw calls for %d + %d instances", frame.instance_draw_calls, frame.instances_drawn,
                            frame.shadow_instances);
                ImGui::PushItemWidth(160.0f);
                bool changed = ImGui::SliderFloat("detail distance", &detail_scale_, 0.2f, 4.0f, "x %.2f");
                changed |= ImGui::SliderFloat("shadow distance", &shadow_scale_, 0.0f, 2.0f, "x %.2f");
                ImGui::PopItemWidth();
                if (changed) {
                    applied_quality_valid_ = false;  // ApplyQualityToSystems combines these with the quality
                }
            }
            ImGui::End();
        }
        renderer_.EndFrame();
    }

private:
    // The graphics quality's scales for the scene's own systems; done again whenever they
    // change (the settings window or Load).
    void ApplyQualityToSystems() {
        const poligon::GraphicsQuality& quality = settings_.quality;
        if (applied_quality_valid_ && quality.vegetation_detail == applied_quality_.vegetation_detail &&
            quality.vegetation_shadow == applied_quality_.vegetation_shadow &&
            quality.ground_cover_density == applied_quality_.ground_cover_density &&
            quality.ground_cover_radius == applied_quality_.ground_cover_radius &&
            quality.terrain_detail == applied_quality_.terrain_detail) {
            return;
        }
        vegetation_.SetDistanceScale(detail_scale_ * quality.vegetation_detail, shadow_scale_ * quality.vegetation_shadow);
        // Ground cover: its plants' distances shrink with its radius, so that they still fade
        // out before their cells are dropped.
        ground_cover_->SetRadius(kGroundCoverRadius * quality.ground_cover_radius);
        ground_cover_->GetVegetation().SetDistanceScale(quality.ground_cover_radius, quality.vegetation_shadow);
        if (!applied_quality_valid_ || quality.ground_cover_density != applied_quality_.ground_cover_density) {
            ground_cover_->SetDensityScale(quality.ground_cover_density);
        }
        terrain_->SetDetail(kTerrainDetail * quality.terrain_detail);
        applied_quality_ = quality;
        applied_quality_valid_ = true;
    }

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

    // An image for a terrain layer at 1024 x 1024 (texture arrays need one size; until
    // textures are compressed, 2048 would cost too much memory). A missing file gives a
    // plain stand-in.
    poligon::Image LoadLayerImage(const std::filesystem::path& path, bool is_color) {
        poligon::Image image;
        std::string error;
        if (!poligon::LoadImageFromFile(path.string().c_str(), image, error)) {
            std::printf("forest_scene: %s\n", error.c_str());
            return is_color ? poligon::MakeSolidImage(1024, 1024, 110, 100, 80) : poligon::MakeSolidImage(1024, 1024, 128, 128, 255);
        }
        while (image.width > 1024) {
            image = poligon::MakeHalfSizeImage(image, is_color);
        }
        return image;
    }

    // Where the player starts: on gentle ground 150 to 500 m from the largest lake, as high
    // above it as possible (up to 40 m) while the lake's middle stays in view. Sets
    // start_yaw_ to face the lake. Positions are meters on the map.
    glm::vec2 ChooseStart() {
        const poligon::HeightMap& map = maps_.height;
        glm::vec2 best(map.GetSizeX() * 0.5f, map.GetSizeZ() * 0.5f);
        start_yaw_ = 270.0f;
        const poligon::Lake* lake = nullptr;
        for (const poligon::Lake& candidate : maps_.lakes) {
            if (lake == nullptr || candidate.area > lake->area) {
                lake = &candidate;
            }
        }
        if (lake == nullptr) {
            return best;
        }
        float best_score = -1.0e30f;
        const float margin = 300.0f;
        for (float radius = 150.0f; radius <= 500.0f; radius += 50.0f) {
            for (int step = 0; step < 72; ++step) {
                const float angle = static_cast<float>(step) * 0.0872665f;
                const glm::vec2 place = lake->center + radius * glm::vec2(std::cos(angle), std::sin(angle));
                if (place.x < margin || place.y < margin || place.x > map.GetSizeX() - margin || place.y > map.GetSizeZ() - margin) {
                    continue;
                }
                if (maps_.Sample(maps_.water_distance, place.x, place.y) < 30.0f || map.GetSlopeDegrees(place.x, place.y) > 12.0f) {
                    continue;
                }
                const float above = map.GetHeight(place.x, place.y) - lake->level;
                if (above < 6.0f) {
                    continue;
                }
                // The line from the eye to the lake's middle must stay above the ground.
                const glm::vec3 eye(place.x, map.GetHeight(place.x, place.y) + kEyeHeight, place.y);
                const glm::vec3 target(lake->center.x, lake->level, lake->center.y);
                bool seen = true;
                for (int sample = 1; sample < 64 && seen; ++sample) {
                    const glm::vec3 point = glm::mix(eye, target, static_cast<float>(sample) / 64.0f);
                    seen = map.GetHeight(point.x, point.z) < point.y - 0.3f;
                }
                if (!seen) {
                    continue;
                }
                const float score = std::min(above, 40.0f) - radius * 0.02f;
                if (score > best_score) {
                    best_score = score;
                    best = place;
                }
            }
        }
        const glm::vec2 to_lake = glm::normalize(lake->center - best);
        start_yaw_ = glm::degrees(std::atan2(-to_lake.x, -to_lake.y));
        start_lake_level_ = lake->level;
        lake_direction_ = to_lake;
        lake_distance_ = glm::length(lake->center - best);
        return best;
    }

    // Whether there is lake water (or its very edge) at a world position.
    bool IsWater(float x, float z) const {
        const float map_x = x + map_offset_.x;
        const float map_z = z + map_offset_.y;
        return maps_.GetWaterDepth(map_x, map_z) > 0.0f || maps_.Sample(maps_.water_distance, map_x, map_z) < 1.5f;
    }

    // The land: generated by the engine (poligon/terrain_generator.h) on the first run, then
    // loaded from the cache. Its ground textures follow the generator's maps. The world's
    // origin is put at the player's start, at the land's height there.
    void CreateGround() {
        const poligon::TerrainGeneratorSettings generator;  // the defaults: 6 x 6 km, seed 1
        std::string error;
        bool generated = false;
        const auto started = std::chrono::steady_clock::now();
        if (!poligon::LoadOrGenerateTerrain(generator, terrain_cache_.string().c_str(), maps_, generated, error)) {
            std::printf("forest_scene: %s (the land is used without being kept)\n", error.c_str());
        }
        std::printf("forest_scene: land %s in %.1f s\n", generated ? "generated" : "loaded from the cache",
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
        const poligon::HeightMap& map = maps_.height;
        const glm::vec2 start = ChooseStart();
        map_offset_ = start;
        poligon::TerrainConfig config;
        config.detail = kTerrainDetail;
        config.origin = {-start.x, -map.GetHeight(start.x, start.y), -start.y};
        world_height_offset_ = config.origin.y;

        // Ground layers and where each covers the land, from the generator's maps.
        const std::filesystem::path textures = assets_ / "polyhaven" / "textures";
        struct LayerSource {
            const char* name;
            float tile_size;
        };
        const LayerSource sources[] = {{"forrest_ground_01", 3.0f}, {"forest_leaves_04", 2.5f}, {"grass_ground", 3.0f},
                                       {"brown_mud_02", 3.0f},      {"gravelly_sand", 2.5f},    {"river_small_rocks", 2.0f},
                                       {"aerial_rocks_02", 6.0f}};
        constexpr int kLayers = 7;
        std::vector<poligon::Image> colors;
        std::vector<poligon::Image> normals;
        for (int i = 0; i < kLayers; ++i) {
            const std::string name = sources[i].name;
            colors.push_back(LoadLayerImage(textures / name / (name + "_diff_2k.jpg"), true));
            normals.push_back(LoadLayerImage(textures / name / (name + "_nor_gl_2k.jpg"), false));
            config.material.tile_sizes[i] = sources[i].tile_size;
        }
        poligon::TextureOptions data;
        data.is_color = false;
        config.material.colors = renderer_.CreateTextureArray(colors.data(), kLayers);
        config.material.normals = renderer_.CreateTextureArray(normals.data(), kLayers, data);
        config.material.layer_count = kLayers;
        config.material.steep_start_degrees = 90.0f;  // the rock layer covers steep ground

        poligon::TerrainLayerRule rules[kLayers];
        // 0: mossy forest floor, wherever nothing else fits.
        rules[0].strength = 0.35f;
        // 1: dry litter in patches on drier ground.
        rules[1].moisture = {0.0f, 0.45f, 0.15f};
        rules[1].patch_size = 45.0f;
        rules[1].patch_cover = 0.45f;
        // 2: grass in clearings on gentle, moderately moist ground.
        rules[2].slope_degrees = {0.0f, 18.0f, 6.0f};
        rules[2].moisture = {0.3f, 0.85f, 0.15f};
        rules[2].patch_size = 120.0f;
        rules[2].patch_cover = 0.4f;
        // 3: mud where water gathers: wet ground, the bottoms of drainage lines, a band along
        // the shore.
        rules[3].moisture = {0.82f, 1.0f, 0.1f};
        rules[3].water_depth = {0.0f, 0.05f, 0.05f};
        rules[3].strength = 1.2f;
        // 4: sand and gravel on the shore line and in shallow water.
        rules[4].water_distance = {0.0f, 2.0f, 2.0f};
        rules[4].water_depth = {0.0f, 0.5f, 0.4f};
        rules[4].strength = 1.6f;
        // 5: small stones on the lake bed and in stream beds.
        rules[5].water_depth = {0.6f, 1000.0f, 0.4f};
        rules[5].strength = 2.0f;
        // 6: rock on steep ground and where the generator found bare rock.
        rules[6].slope_degrees = {32.0f, 90.0f, 8.0f};
        rules[6].strength = 2.0f;
        poligon::Image weights[2];
        const auto weights_started = std::chrono::steady_clock::now();
        poligon::MakeTerrainLayerWeights(maps_, rules, kLayers, 1u, weights);
        config.material.weights[0] = renderer_.CreateTexture(weights[0], [] {
            poligon::TextureOptions options;
            options.is_color = false;
            options.generate_mipmaps = false;
            return options;
        }());
        config.material.weights[1] = renderer_.CreateTexture(weights[1], [] {
            poligon::TextureOptions options;
            options.is_color = false;
            options.generate_mipmaps = false;
            return options;
        }());
        std::printf("forest_scene: ground layer weights in %.1f s; %d lakes; start at %.0f, %.0f on the map, %.0f m above "
                    "the largest lake, %.0f m from its middle, which lies at turn %.0f\n",
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - weights_started).count(),
                    static_cast<int>(maps_.lakes.size()), static_cast<double>(start.x), static_cast<double>(start.y),
                    static_cast<double>(map.GetHeight(start.x, start.y) - start_lake_level_), static_cast<double>(lake_distance_),
                    static_cast<double>(start_yaw_));
        terrain_ = std::make_unique<poligon::Terrain>(renderer_, map, config);
        lakes_ = poligon::CreateLakeSurfaces(renderer_, maps_, config.origin);
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
    // wind: Material::wind_bend and wind_flutter for the kind; flutter goes to the cut-out
    // (leaf) materials only.
    Kind LoadKind(const char* relative_folder, bool split, bool is_small, bool keep_origin = false,
                  const char* lod_name = nullptr, const LevelDistances& levels = kTreeLevels,
                  glm::vec2 wind = {0.0f, 0.0f}) {
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
        poligon::RenderModel& render_model = render_models_.back();
        for (poligon::Material& material : render_model.materials) {
            material.wind_bend = wind.x;
            material.wind_flutter = material.alpha_cutoff > 0.0f ? wind.y : 0.0f;
        }

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
                species.shadow_distance = is_small ? 20.0f : kTreeShadowDistance;
                species.shadow_level_bias = 1;
            } else {
                full.max_distance = is_small ? kSmallPlantDistance : kMediumThingDistance;
                species.levels.push_back(full);
                // Grass and the like are too small for their shadows to matter beyond a few steps.
                species.shadow_distance = is_small ? 20.0f : kShadowDistance + 10.0f;
            }
            variant.definition = species;
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
        // The last argument is the wind response: bend and flutter (see Material).
        pine_ = LoadKind("generated/pine", false, false, true, "pine", kTreeLevels, {1.0f, 1.0f});
        spruce_ = LoadKind("generated/spruce", false, false, true, "spruce", kTreeLevels, {1.0f, 1.0f});
        birch_ = LoadKind("generated/birch", false, false, true, "birch", kTreeLevels, {1.3f, 1.3f});
        oak_ = LoadKind("generated/oak", false, false, true, "oak", kTreeLevels, {0.8f, 1.0f});
        young_spruce_ = LoadKind("generated/spruce_young", false, false, true, "spruce_young", kYoungTreeLevels, {1.5f, 1.0f});
        young_pine_ = LoadKind("generated/pine_young", false, false, true, "pine_young", kYoungTreeLevels, {1.5f, 1.0f});
        // Simpler levels of downloaded models: tools/assets/simplify_model.py.
        shrub_ = LoadKind("polyhaven/shrub_02", true, false, false, "shrub_02", kThingLevels, {2.5f, 1.0f});
        low_shrub_ = LoadKind("polyhaven/shrub_03", true, true, false, nullptr, kTreeLevels, {4.0f, 1.2f});
        fern_ = LoadKind("polyhaven/fern_02", true, true, false, nullptr, kTreeLevels, {5.0f, 1.2f});
        nettle_ = LoadKind("polyhaven/nettle_plant", true, true, false, nullptr, kTreeLevels, {4.0f, 1.0f});
        grass_ = LoadKind("polyhaven/grass_bermuda_01", true, true, false, nullptr, kTreeLevels, {6.0f, 1.5f});
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
        // Nothing grows or lies on slopes this steep (the walls of gullies), or in a lake.
        if (terrain_->GetSlopeDegrees(x, z) > 33.0f || IsWater(x, z)) {
            return;
        }
        // A clearing opens from the start towards the lake, so that the lake is in view:
        // no trees or large things within 22 degrees of that direction.
        if (!kind.is_small && lake_distance_ > 0.0f) {
            const glm::vec2 offset(x, z);
            const float along = glm::dot(offset, lake_direction_);
            const float across = std::fabs(lake_direction_.x * offset.y - lake_direction_.y * offset.x);
            if (along > -8.0f && along < lake_distance_ && across < 8.0f + along * 0.4f) {
                return;
            }
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
            }
        }

        // The forest now covers 500 x 500 m, twelve times the area it had on the flat test
        // ground. Undergrowth is dense near the start and thinner over the whole area.
        Scatter(young_spruce_, random, 350, kForestHalfSize, 0.5f, 1.3f);
        Scatter(young_pine_, random, 350, kForestHalfSize, 0.5f, 1.3f);
        Scatter(shrub_, random, 500, kForestHalfSize, 0.7f, 1.2f);
        Scatter(boulder_, random, 120, kForestHalfSize, 0.6f, 2.2f, 0.25f);
        Scatter(stump_, random, 110, kForestHalfSize, 0.8f, 1.2f, 0.05f);
        Scatter(fallen_trunk_, random, 80, kForestHalfSize, 0.9f, 1.4f, 0.1f);
        std::printf("forest_scene: %d things placed\n", placed_count_);
        GrowGroundCover();
    }

    // Grass, ferns, nettles, low shrubs and stones are not planted: they grow on the fly
    // around the player (poligon/ground_cover.h) from rules and the land, over the whole map.
    void GrowGroundCover() {
        ground_cover_ = std::make_unique<poligon::GroundCover>(*terrain_, kGroundCoverRadius);
        // One layer per variant of a kind, sharing the kind's density between them. A
        // kind's levels are cut at the radius, so that plants fade out before their cell goes.
        // every_nth: use only every n-th variant of the kind (each variant is a draw call per
        // pass). shadow_limit: shadows of the layer stop here at the latest; grass shadows
        // matter only at the player's feet.
        const auto add = [this](const Kind& kind, float density, float scale_low, float scale_high, float patch_threshold,
                                float max_slope, bool align, float sink, int seed, size_t every_nth, float shadow_limit) {
            for (size_t i = 0; i < kind.variants.size(); i += every_nth) {
                poligon::GroundCoverLayer layer;
                layer.species = kind.variants[i].definition;
                layer.species.shadow_distance = std::min(layer.species.shadow_distance, shadow_limit);
                std::vector<poligon::VegetationLevel>& levels = layer.species.levels;
                const float limit = kGroundCoverRadius * 0.9f;
                for (size_t level = 0; level < levels.size(); ++level) {
                    if (levels[level].max_distance >= limit || level + 1 == levels.size()) {
                        levels[level].max_distance = std::min(levels[level].max_distance, limit);
                        levels.resize(level + 1);
                        break;
                    }
                }
                layer.density = density / static_cast<float>((kind.variants.size() + every_nth - 1) / every_nth);
                layer.scale_min = scale_low;
                layer.scale_max = scale_high;
                layer.patch_threshold = patch_threshold;
                layer.patch_softness = 0.25f;
                layer.max_slope_degrees = max_slope;
                layer.align_to_ground = align;
                layer.sink_meters = sink;
                // Nothing grows under the start lake's surface. (Rules that read the
                // generator's maps replace this in the planting step.)
                layer.min_height = start_lake_level_ + world_height_offset_ + 0.1f;
                layer.seed = seed * 100 + static_cast<int>(i);
                ground_cover_->AddLayer(layer);
            }
        };
        add(grass_, 3.0f, 2.5f, 4.0f, 0.2f, 40.0f, true, 0.0f, 1, 3, 8.0f);
        add(fern_, 0.06f, 0.9f, 1.7f, 0.5f, 33.0f, false, 0.0f, 2, 1, 20.0f);
        add(nettle_, 0.03f, 2.5f, 4.0f, 0.55f, 33.0f, false, 0.0f, 3, 1, 20.0f);
        add(low_shrub_, 0.02f, 1.2f, 2.0f, 0.4f, 33.0f, false, 0.0f, 4, 1, 20.0f);
        add(rocks_, 0.002f, 0.35f, 0.8f, 0.0f, 50.0f, true, 0.08f, 5, 1, 20.0f);
    }

    poligon::Renderer& renderer_;
    std::filesystem::path assets_;
    const poligon::Input& input_;
    ForestActions actions_;

    std::filesystem::path terrain_cache_;
    // The generated land and its maps; map position = world position + map_offset_.
    poligon::TerrainMaps maps_;
    glm::vec2 map_offset_{0.0f};
    // World height = map height + world_height_offset_.
    float world_height_offset_ = 0.0f;
    float start_yaw_ = 270.0f;
    float start_lake_level_ = -1.0e30f;
    // Direction (map x, z) and distance from the start to the largest lake's middle.
    glm::vec2 lake_direction_{0.0f};
    float lake_distance_ = 0.0f;
    std::unique_ptr<poligon::Terrain> terrain_;
    // The lakes' water surfaces and how they look (the defaults: clear lake water).
    std::vector<poligon::WaterSurface> lakes_;
    poligon::WaterMaterial water_material_;
    std::vector<poligon::RenderModel> render_models_;
    Kind pine_, spruce_, birch_, oak_, young_spruce_, young_pine_, shrub_, low_shrub_, fern_, nettle_, grass_, boulder_,
        rocks_, stump_, fallen_trunk_;
    int loaded_kind_count_ = 0;
    // All plants, rocks and stumps: placed once, drawn by distance.
    poligon::Vegetation vegetation_;
    std::unique_ptr<poligon::GroundCover> ground_cover_;
    int placed_count_ = 0;
    float detail_scale_ = 1.0f;
    float shadow_scale_ = 1.0f;
    poligon::GraphicsQuality applied_quality_;
    bool applied_quality_valid_ = false;

    poligon::RenderModel fox_model_;
    float fox_scale_ = 1.0f;
    // Players refer to the set, so it is declared before them and destroyed after them.
    std::unique_ptr<poligon::AnimationSet> fox_animations_;
    std::vector<Animal> animals_;
    float time_seconds_ = 0.0f;
    double cpu_milliseconds_sum_ = 0.0;
    double wait_milliseconds_sum_ = 0.0;
    int cpu_frames_ = 0;

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
    environment.wind_direction = {0.6f, 0.8f};
    environment.wind_strength = 1.0f;

    settings.post.bloom = true;
    settings.post.auto_exposure = true;
    settings.post.auto_exposure_target = 0.22f;
    // Temporal anti-aliasing instead of multisampling (see main): smooths leaves and dithered
    // fades, which multisampling cannot; a little sharpening gives back its softness.
    settings.post.temporal_aa = true;
    settings.post.sharpen = 0.4f;
    // A slightly warm, a little more saturated and contrasty picture with a soft vignette:
    // a late-summer afternoon rather than a technical render.
    settings.post.temperature = 0.15f;
    settings.post.saturation = 1.08f;
    settings.post.contrast = 1.06f;
    settings.post.vignette = 0.18f;
    return settings;
}

} // namespace

int main(int argc, char** argv) {
    poligon::InstallCrashHandler();
    const bool started_by_hand = argc == 1;

    poligon::CommandLineOptions options;
    std::string error;
    // Default: at the start, looking towards the lake, slightly down.
    float view[4] = {0.0f, 0.0f, 0.0f, -6.0f};
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
                     "                    [--gpu-validation] [--dump-frame PATH.png] [--render-settings FILE]\n"
                     "                    [--quality low|medium|high|ultra] [--resolution WxH]\n",
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
    if (!options.quality.empty()) {
        poligon::GraphicsPreset preset = poligon::GraphicsPreset::High;
        poligon::ParseGraphicsPreset(options.quality.c_str(), preset);
        settings.quality = poligon::GetGraphicsPreset(preset);
    }
    if (options.width > 0) {
        renderer_config.width = options.width;
        renderer_config.height = options.height;
    }
    renderer_config.post = settings.post;
    renderer_config.debug_ui = true;
    // Shadow image size and multisampling come from the graphics quality.
    poligon::ApplyGraphicsQuality(settings.quality, renderer_config);
    poligon::Renderer renderer(renderer_config);

    poligon::Input input;
    const ForestActions actions = SetUpInput(input);
    if (!options.headless) {
        input.UseLiveDevices();
    }

    const auto load_start = std::chrono::steady_clock::now();
    // Generated land is kept in build/terrain_cache (the program runs from build/PRESET/bin).
    std::error_code path_error;
    const std::filesystem::path terrain_cache =
        std::filesystem::absolute(argv[0], path_error).parent_path().parent_path().parent_path() / "terrain_cache";
    ForestGame game(renderer, assets, terrain_cache, input, actions, view, options.positional.size() >= 3, settings, settings_path);
    game.ShowCollision(options.positional.size() == 5);
    const double load_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - load_start).count();
    std::printf("forest_scene: loaded in %.1f s; quality %s, %d x %d\n", load_seconds,
                poligon::GetGraphicsPresetName(settings.quality.preset), renderer.GetWidth(), renderer.GetHeight());
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
    std::printf("last frame: %d meshes submitted, %d drawn, %d shadow draws; processor per frame on average: %.2f ms "
                "preparing, %.2f ms waiting for the card\n",
                stats.meshes_submitted, stats.meshes_drawn, stats.shadow_draws,
                static_cast<double>(game.GetAverageCpuMilliseconds()), static_cast<double>(game.GetAverageWaitMilliseconds()));
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
