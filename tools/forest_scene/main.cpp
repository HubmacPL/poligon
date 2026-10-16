// Showcase: 6 x 6 km of land made by the engine's terrain generator, with a piece of
// forest above a lake, put together from the models in the poligon-assets folder, to walk
// through and look at. It exists to judge the renderer on a whole scene and to measure how
// long a frame takes. The land is generated on the first run (about three minutes) and then
// loaded from build/terrain_cache. The models and textures are named in assets.txt (next to
// this file); their sources are in poligon-assets/source, and they are cooked into
// poligon-assets/cooked when the program starts, whenever the list or a source changed
// (poligon/assets/assets.h). The land's settings, the player's start, things put by
// hand and the rules that plant the forest, the shore and the ground cover are in
// forest.level (poligon/world/level.h, poligon/world/plant_rules.h). Walking flattens grass into paths
// (poligon/world/trample.h).
//
// Usage: forest_scene [X Z TURN TILT [collision]] [standard Poligon options]
//   X Z         where to stand, in meters from the start (default 0 0): a slope above
//               the largest lake, chosen so that the lake is in view
//   TURN TILT   where to look, in degrees (default: towards the lake, slightly down)
//   collision   the word itself: start with the collision shapes shown
//   --render-settings FILE   the look to start with, as saved from the settings window
//   --record FILE, --replay FILE   write or play back the input (docs/en/input.md)
//
// Mouse: hold the left button and move to look around.
// Keys:  W/S walk, A/D step sideways, Left/Right turn, Up/Down look up and down, hold
//        Left Shift to run, Space to jump. F switches between walking and flying (then E/Q
//        rise and sink). T throws a crate, B shows the collision shapes as colored boxes.
//        Tab hides and shows the debug windows. F11 switches between a 1280 x 720 window
//        and fullscreen at 1920 x 1080 (the display changes its resolution).
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

#include "poligon/animation/animation.h"
#include "poligon/assets/assets.h"
#include "poligon/core/command_line.h"
#include "poligon/core/crash_handler.h"
#include "poligon/debug/debug_ui.h"
#include "poligon/debug/free_camera.h"
#include "poligon/core/game_loop.h"
#include "poligon/render/graphics_quality.h"
#include "poligon/world/ground_cover.h"
#include "poligon/render/impostor.h"
#include "poligon/core/input.h"
#include "poligon/world/level.h"
#include "poligon/assets/model.h"
#include "poligon/physics/physics.h"
#include "poligon/world/plant_rules.h"
#include "poligon/world/plants.h"
#include "poligon/world/procedural_plants.h"
#include "poligon/core/platform.h"
#include "poligon/render/render_settings.h"
#include "poligon/world/sky_occlusion.h"
#include "poligon/world/terrain.h"
#include "poligon/world/trample.h"
#include "poligon/world/vegetation.h"
#include "poligon/render/renderer.h"
#include "poligon/world/water.h"

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
// Downloaded grass clumps: dense cut-out blades, so their full level is kept close.
constexpr LevelDistances kGrassLevels{6.0f, 15.0f, 35.0f, 0.0f, 35.0f};
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

// One kind of plant, rock or dead wood: its variants (poligon/world/plants.h), and whether it is
// small (small things may stand in the view to the lake and need no long distances).
struct Kind {
    poligon::PlantKind plants;
    bool is_small = false;
    std::string asset_name;
};

struct Placement {
    const poligon::PlantVariant* variant;
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
    poligon::InputAction toggle_fullscreen;
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
    actions.toggle_fullscreen = input.AddAction("toggle_fullscreen");
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
    input.BindKey(actions.toggle_fullscreen, poligon::Key::F11);
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

class ForestGame : public poligon::Game {
public:
    ForestGame(poligon::Renderer& renderer, const std::filesystem::path& assets, const std::filesystem::path& build_folder,
               const std::filesystem::path& asset_list, const std::filesystem::path& level_path, const poligon::Input& input,
               const ForestActions& actions,
               const float view[4], bool turn_given, const poligon::RenderSettings& settings, const std::string& settings_path)
        : renderer_(renderer), sources_(assets / "source"), terrain_cache_(build_folder / "terrain_cache"), input_(input),
          actions_(actions), settings_(settings), settings_path_(settings_path) {
        OpenAssets(asset_list, assets / "cooked");
        if (settings.environment.clouds) {
            renderer_.PrepareClouds();
        }
        std::string level_error;
        if (!poligon::LoadLevel(level_path.string().c_str(), level_, level_error, poligon::GetPlantRuleWords())) {
            std::printf("forest_scene: %s; using the default land\n", level_error.c_str());
        } else if (!poligon::ParsePlantRules(level_.other_lines, rules_, level_error)) {
            std::printf("forest_scene: %s: %s\n", level_path.string().c_str(), level_error.c_str());
        }
        renderer_.SetTrampleMap(&trample_);
        CreateGround();
        camera_.position = {view[0], Ground(view[0], view[1]) + kEyeHeight, view[1]};
        camera_.yaw_degrees = turn_given ? view[2] : start_yaw_;
        camera_.pitch_degrees = view[3];
        camera_.vertical_fov_degrees = 65.0f;
        LoadKinds();
        PlaceLevelObjects();
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
        // F11 was pressed during the last ticks: the window mode changes between frames.
        if (fullscreen_toggle_pending_) {
            fullscreen_toggle_pending_ = false;
            const bool to_fullscreen = renderer_.GetWindowMode() == poligon::WindowMode::Windowed;
            const bool done = to_fullscreen ? renderer_.SetWindowMode(poligon::WindowMode::Fullscreen, 1920, 1080)
                                            : renderer_.SetWindowMode(poligon::WindowMode::Windowed, 1280, 720);
            if (done) {
                std::printf("forest_scene: %s, %d x %d\n", to_fullscreen ? "fullscreen" : "window", renderer_.GetWidth(),
                            renderer_.GetHeight());
            } else {
                std::printf("forest_scene: the display has no 1920 x 1080 mode; staying in the window\n");
            }
        }
    }

    void FixedUpdate(poligon::GameLoop& /*loop*/, const poligon::TickInfo& tick) override {
        if (input_.WasPressed(actions_.toggle_fly)) {
            flying_ = !flying_;
        }
        if (input_.WasPressed(actions_.toggle_windows)) {
            show_windows_ = !show_windows_;
        }
        if (input_.WasPressed(actions_.toggle_fullscreen)) {
            fullscreen_toggle_pending_ = true;
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
        // Grass under walking feet goes flat in about half a second and stands up again
        // over about a minute and a half; walking the same way again makes a path.
        const glm::vec2 feet(camera_.position.x, camera_.position.z);
        trample_.Recenter(feet);
        if (!flying_ && (forward != 0.0f || right != 0.0f)) {
            trample_.Trample(feet, 0.5f, 2.0f * delta);
        }
        for (const Animal& animal : animals_) {
            if (animal.circle_radius > 0.0f) {
                trample_.Trample({animal.transform[3].x, animal.transform[3].z}, 0.3f, 1.5f * delta);
            }
        }
        trample_.Recover(0.011f * delta);
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

    // Cooks what changed since the last run (sources in poligon-assets/source, cooked files
    // in poligon-assets/cooked) and opens the cooked folder.
    void OpenAssets(const std::filesystem::path& list_path, const std::filesystem::path& cooked) {
        poligon::AssetList list;
        std::string error;
        if (!poligon::LoadAssetList(list_path.string().c_str(), list, error)) {
            std::printf("forest_scene: %s\n", error.c_str());
        } else {
            const auto started = std::chrono::steady_clock::now();
            const poligon::CookReport report =
                poligon::CookAssets(list, list_path.string().c_str(), sources_.string().c_str(), cooked.string().c_str());
            std::printf("forest_scene: assets: %d cooked, %d up to date, in %.1f s\n", report.cooked, report.up_to_date,
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
            for (const std::string& failure : report.errors) {
                std::printf("forest_scene: %s\n", failure.c_str());
            }
        }
        if (!library_.Open(cooked.string().c_str(), error)) {
            std::printf("forest_scene: %s\n", error.c_str());
        }
    }

    // Where the player starts: as the level says, above the largest lake or at a given
    // place. Sets start_yaw_ and what is known of the lake. Positions are meters on the map.
    glm::vec2 ChooseStart() {
        if (!level_.start_at_lake) {
            start_yaw_ = level_.start_turn_degrees;
            return level_.start_position;
        }
        poligon::LakeViewpoint viewpoint;
        if (!poligon::FindLakeViewpoint(maps_, kEyeHeight, viewpoint)) {
            std::printf("forest_scene: no place above a lake; starting in the middle of the land\n");
            return viewpoint.position;
        }
        start_yaw_ = viewpoint.yaw_degrees;
        start_lake_level_ = maps_.lakes[static_cast<size_t>(viewpoint.lake)].level;
        lake_direction_ = viewpoint.direction;
        lake_distance_ = viewpoint.distance;
        return viewpoint.position;
    }

    // Whether there is lake water (or its very edge) at a world position.
    bool IsWater(float x, float z) const {
        const float map_x = x + map_offset_.x;
        const float map_z = z + map_offset_.y;
        return maps_.GetWaterDepth(map_x, map_z) > 0.0f || maps_.Sample(maps_.water_distance, map_x, map_z) < 1.5f;
    }

    // The land: generated by the engine (poligon/world/terrain_generator.h) on the first run, then
    // loaded from the cache. Its ground textures follow the generator's maps. The world's
    // origin is put at the player's start, at the land's height there.
    void CreateGround() {
        const poligon::TerrainGeneratorSettings& generator = level_.terrain;
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

        // Ground layers (cooked, block-compressed) and where each covers the land, from the
        // generator's maps.
        struct LayerSource {
            const char* name;
            float tile_size;
        };
        const LayerSource sources[] = {{"forest_floor", 3.0f}, {"litter", 2.5f}, {"grass", 3.0f}, {"mud", 3.0f},
                                       {"sand", 2.5f},         {"stones", 2.0f}, {"rock", 6.0f}};
        constexpr int kLayers = 7;
        std::vector<poligon::CompressedImage> colors;
        std::vector<poligon::CompressedImage> normals;
        for (int i = 0; i < kLayers; ++i) {
            const std::string name = std::string("ground/") + sources[i].name;
            const poligon::CompressedImage* color = library_.LoadImage(name + "_color", error);
            const poligon::CompressedImage* normal = color != nullptr ? library_.LoadImage(name + "_normal", error) : nullptr;
            if (color == nullptr || normal == nullptr) {
                std::printf("forest_scene: %s; the ground is drawn without textures\n", error.c_str());
                colors.clear();
                break;
            }
            colors.push_back(*color);
            normals.push_back(*normal);
            config.material.tile_sizes[i] = sources[i].tile_size;
        }
        if (!colors.empty()) {
            config.material.colors = renderer_.CreateTextureArray(colors.data(), kLayers);
            config.material.normals = renderer_.CreateTextureArray(normals.data(), kLayers);
            config.material.layer_count = kLayers;
        }
        config.material.steep_start_degrees = 90.0f;  // the rock layer covers steep ground

        poligon::TerrainLayerRule rules[kLayers];
        // 0: mossy forest floor, wherever nothing else fits.
        rules[0].strength = 0.35f;
        // 1: dry litter in patches on drier ground.
        rules[1].conditions.moisture = {0.0f, 0.45f, 0.15f};
        rules[1].patch_size = 45.0f;
        rules[1].patch_cover = 0.45f;
        // 2: grass in clearings on gentle, moderately moist ground.
        rules[2].conditions.slope_degrees = {0.0f, 18.0f, 6.0f};
        rules[2].conditions.moisture = {0.3f, 0.85f, 0.15f};
        rules[2].patch_size = 120.0f;
        rules[2].patch_cover = 0.4f;
        // 3: mud where water gathers: wet ground, the bottoms of drainage lines, a band along
        // the shore.
        rules[3].conditions.moisture = {0.82f, 1.0f, 0.1f};
        rules[3].conditions.water_depth = {0.0f, 0.05f, 0.05f};
        rules[3].strength = 1.2f;
        // 4: sand and gravel on the shore line and in shallow water.
        rules[4].conditions.water_distance = {0.0f, 2.0f, 2.0f};
        rules[4].conditions.water_depth = {0.0f, 0.5f, 0.4f};
        rules[4].strength = 1.6f;
        // 5: small stones on the lake bed and in stream beds.
        rules[5].conditions.water_depth = {0.6f, 1000.0f, 0.4f};
        rules[5].strength = 2.0f;
        // 6: rock on steep ground and where the generator found bare rock.
        rules[6].conditions.slope_degrees = {32.0f, 90.0f, 8.0f};
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

    // One kind from its cooked model and its settings as a plant (poligon/world/plants.h).
    Kind LoadKind(const char* asset_name, bool is_small, const poligon::PlantSettings& settings) {
        Kind kind;
        kind.is_small = is_small;
        std::string error;
        const poligon::LoadedModel* model = library_.LoadModel(renderer_, asset_name, error);
        if (model == nullptr) {
            std::printf("forest_scene: skipping %s: %s\n", asset_name, error.c_str());
            return kind;
        }
        kind.plants = poligon::MakePlantKind(*model, settings, vegetation_);
        std::printf("forest_scene: %s: %zu variant(s), %lld triangles\n", asset_name, kind.plants.variants.size(),
                    static_cast<long long>(model->model.GetTriangleCount()));
        std::fflush(stdout);
        ++loaded_kind_count_;
        kind.asset_name = asset_name;
        return kind;
    }

    // The things the level puts by hand, by asset name.
    void PlaceLevelObjects() {
        for (const poligon::LevelObject& object : level_.objects) {
            const Kind* kind = FindKind(object.asset);
            if (kind == nullptr) {
                std::printf("forest_scene: the level places \"%s\", which is not loaded\n", object.asset.c_str());
                continue;
            }
            const poligon::PlantVariant& variant = kind->plants.variants.front();
            const float x = object.position.x - map_offset_.x;
            const float z = object.position.y - map_offset_.y;
            const float turn = glm::radians(object.turn_degrees);
            const glm::vec3 foot(x, Ground(x, z) - variant.height * object.scale * 0.15f, z);
            vegetation_.AddInstance(variant.species_id, glm::translate(glm::mat4(1.0f), foot) *
                                                            glm::rotate(glm::mat4(1.0f), turn, {0.0f, 1.0f, 0.0f}) *
                                                            glm::scale(glm::mat4(1.0f), glm::vec3(object.scale)));
            poligon::AddPlantCollider(physics_, kind->plants, variant, foot, turn, object.scale);
        }
    }

    // Settings of a kind as a plant: distances of its levels (with simpler levels and an
    // impostor, or one mesh), shadows, wind response, and what blocks the player.
    static poligon::PlantSettings MakeSettings(bool with_levels, const LevelDistances& levels, bool is_small, glm::vec2 wind,
                                               bool split, bool keep_origin, poligon::PlantCollider collider = poligon::PlantCollider::None,
                                               float trunk_radius = 0.3f) {
        poligon::PlantSettings settings;
        settings.split_variants = split;
        settings.keep_origin = keep_origin;
        if (with_levels) {
            settings.level_distances = {levels.full, levels.medium, levels.low};
            if (levels.impostor > 0.0f) {
                settings.level_distances.push_back(levels.impostor);
            }
            settings.distance_without_impostor = levels.low_without_impostor;
            settings.shadow_distance = is_small ? 20.0f : kTreeShadowDistance;
            settings.shadow_level_bias = 1;
        } else {
            settings.level_distances = {is_small ? kSmallPlantDistance : kMediumThingDistance};
            // Grass and the like are too small for their shadows to matter beyond a few steps.
            settings.shadow_distance = is_small ? 20.0f : kShadowDistance + 10.0f;
        }
        settings.wind_bend = wind.x;
        settings.wind_flutter = wind.y;
        settings.collider = collider;
        settings.trunk_radius = trunk_radius;
        return settings;
    }

    void LoadKinds() {
        using poligon::PlantCollider;
        // Trees made with tools/assets/make_tree.mjs from the recipes in tools/assets/trees.
        // Wind response: bend and flutter (see Material). Trunk radii measured at the foot.
        pine_ = LoadKind("trees/pine", false, MakeSettings(true, kTreeLevels, false, {1.0f, 1.0f}, false, true, PlantCollider::Trunk, 0.4f));
        spruce_ = LoadKind("trees/spruce", false, MakeSettings(true, kTreeLevels, false, {1.0f, 1.0f}, false, true, PlantCollider::Trunk, 0.25f));
        birch_ = LoadKind("trees/birch", false, MakeSettings(true, kTreeLevels, false, {1.3f, 1.3f}, false, true, PlantCollider::Trunk, 0.14f));
        oak_ = LoadKind("trees/oak", false, MakeSettings(true, kTreeLevels, false, {0.8f, 1.0f}, false, true, PlantCollider::Trunk, 0.45f));
        // Second trees of each kind; trunk radii scaled from the first by height.
        pine_b_ = LoadKind("trees/pine_b", false, MakeSettings(true, kTreeLevels, false, {1.0f, 1.0f}, false, true, PlantCollider::Trunk, 0.35f));
        spruce_b_ = LoadKind("trees/spruce_b", false, MakeSettings(true, kTreeLevels, false, {1.0f, 1.0f}, false, true, PlantCollider::Trunk, 0.29f));
        birch_b_ = LoadKind("trees/birch_b", false, MakeSettings(true, kTreeLevels, false, {1.3f, 1.3f}, false, true, PlantCollider::Trunk, 0.12f));
        oak_b_ = LoadKind("trees/oak_b", false, MakeSettings(true, kTreeLevels, false, {0.8f, 1.0f}, false, true, PlantCollider::Trunk, 0.38f));
        young_spruce_ = LoadKind("trees/spruce_young", false, MakeSettings(true, kYoungTreeLevels, false, {1.5f, 1.0f}, false, true));
        young_pine_ = LoadKind("trees/pine_young", false, MakeSettings(true, kYoungTreeLevels, false, {1.5f, 1.0f}, false, true));
        // Downloaded models; simpler levels from tools/assets/simplify_model.py.
        shrub_ = LoadKind("plants/shrub", false, MakeSettings(true, kThingLevels, false, {2.5f, 1.0f}, true, false));
        // Low plants are lit partly, and grass fully, like the ground under them (normal_up),
        // and left out of ambient occlusion, which makes thin leaves far too dark.
        const auto low_plant = [](poligon::PlantSettings settings, float normal_up) {
            settings.normal_up = normal_up;
            settings.casts_ambient_occlusion = false;
            return settings;
        };
        low_shrub_ = LoadKind("plants/low_shrub", true, low_plant(MakeSettings(false, kTreeLevels, true, {4.0f, 1.2f}, true, false), 0.5f));
        fern_ = LoadKind("plants/fern", true, low_plant(MakeSettings(false, kTreeLevels, true, {5.0f, 1.2f}, true, false), 0.5f));
        nettle_ = LoadKind("plants/nettle", true, low_plant(MakeSettings(false, kTreeLevels, true, {4.0f, 1.0f}, true, false), 0.5f));
        grass_ = LoadKind("plants/grass", true, low_plant(MakeSettings(false, kTreeLevels, true, {6.0f, 1.5f}, true, false), 1.0f));
        grass_medium_ = LoadKind("plants/grass_medium", true, low_plant(MakeSettings(true, kGrassLevels, true, {6.0f, 1.5f}, true, false), 1.0f));
        grass_tall_ = LoadKind("plants/grass_tall", true, low_plant(MakeSettings(true, kGrassLevels, true, {5.0f, 1.5f}, true, false), 1.0f));
        boulder_ = LoadKind("rocks/boulder", false, MakeSettings(true, kThingLevels, false, {0.0f, 0.0f}, false, false, PlantCollider::Block));
        rocks_ = LoadKind("rocks/mossy_rocks", false, MakeSettings(true, kThingLevels, false, {0.0f, 0.0f}, true, false, PlantCollider::Block));
        stump_ = LoadKind("wood/stump", false, MakeSettings(true, kThingLevels, false, {0.0f, 0.0f}, false, false, PlantCollider::Block));
        fallen_trunk_ = LoadKind("wood/fallen_trunk", false, MakeSettings(true, kThingLevels, false, {0.0f, 0.0f}, false, false, PlantCollider::Block));
        // Water and shore plants made by the engine: no free models were found.
        reed_ = MakeProceduralKind("procedural/reed", poligon::MakeReedModel(11u), {3.0f, 1.2f});
        cattail_ = MakeProceduralKind("procedural/cattail", poligon::MakeCattailModel(12u), {2.0f, 0.8f});
        water_lily_ = MakeProceduralKind("procedural/water_lily", poligon::MakeWaterLilyModel(13u), {0.0f, 0.0f});
        // Grass and flowers made by the engine: real blades, seen right from every side.
        short_grass_ = MakeProceduralKind("procedural/short_grass", poligon::MakeGrassModel(21u, 6, 0.12f, 0.3f, 10, 16), {7.0f, 1.5f}, 30.0f);
        meadow_grass_ = MakeProceduralKind("procedural/meadow_grass", poligon::MakeGrassModel(22u, 6, 0.4f, 0.8f, 8, 14), {5.0f, 1.5f});
        flowers_ = MakeProceduralKind("procedural/flowers", poligon::MakeFlowerModel(23u), {5.0f, 1.0f});
    }

    // A kind from a model made in memory; small, one level, short shadows.
    Kind MakeProceduralKind(const char* name, const poligon::Model& model, glm::vec2 wind, float distance = kSmallPlantDistance) {
        procedural_models_.push_back(std::make_unique<poligon::LoadedModel>(poligon::UploadLoadedModel(renderer_, model)));
        Kind kind;
        kind.is_small = true;
        kind.asset_name = name;
        poligon::PlantSettings settings = MakeSettings(false, kTreeLevels, true, wind, true, false);
        settings.level_distances = {distance};
        kind.plants = poligon::MakePlantKind(*procedural_models_.back(), settings, vegetation_);
        std::printf("forest_scene: %s: %zu variant(s), %lld triangles\n", name, kind.plants.variants.size(),
                    static_cast<long long>(model.GetTriangleCount()));
        return kind;
    }

    // The kind with this asset name, or nullptr.
    const Kind* FindKind(const std::string& name) const {
        for (const Kind* kind : {&pine_, &spruce_, &birch_, &oak_, &young_spruce_, &young_pine_, &shrub_, &low_shrub_, &fern_,
                                 &nettle_, &grass_, &boulder_, &rocks_, &stump_, &fallen_trunk_, &reed_, &cattail_, &water_lily_,
                                 &short_grass_, &meadow_grass_, &flowers_, &grass_medium_, &grass_tall_, &pine_b_, &spruce_b_,
                                 &birch_b_, &oak_b_}) {
            if (kind->asset_name == name && !kind->plants.variants.empty()) {
                return kind;
            }
        }
        return nullptr;
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
        const poligon::LoadedModel* loaded = nullptr;
        std::string error;
        for (const char* name : {"animals/fox", "animals/fox_sample"}) {
            loaded = library_.LoadModel(renderer_, name, error);
            if (loaded != nullptr && loaded->animations != nullptr) {
                std::printf("forest_scene: fox model %s\n", name);
                break;
            }
            loaded = nullptr;
        }
        if (loaded == nullptr) {
            std::printf("forest_scene: no animals: %s\n", error.c_str());
            return;
        }
        const poligon::Model& model = loaded->model;
        fox_model_ = loaded->render;
        // Cooked clips come built (an ozz-animation archive): nothing to prepare here.
        fox_animations_ = loaded->animations;
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

    // Puts one plant where a rule placed it (map coordinates; its foot at `placement.height`
    // on the map, pushed into the ground by `sink` of its height).
    void PlaceAt(const Kind& kind, const poligon::PlantPlacement& placement, float sink) {
        const poligon::PlantVariant& variant = kind.plants.variants[static_cast<size_t>(placement.variant)];
        const float x = placement.position.x - map_offset_.x;
        const float z = placement.position.y - map_offset_.y;
        const glm::vec3 foot(x, placement.height + world_height_offset_ - variant.height * placement.scale * sink, z);
        vegetation_.AddInstance(variant.species_id, glm::translate(glm::mat4(1.0f), foot) *
                                                        glm::rotate(glm::mat4(1.0f), placement.turn, {0.0f, 1.0f, 0.0f}) *
                                                        glm::scale(glm::mat4(1.0f), glm::vec3(placement.scale)));
        // What blocks the way gets a body in the physics world, where the player can walk.
        if (std::fabs(x) < kPlayHalfSize && std::fabs(z) < kPlayHalfSize) {
            poligon::AddPlantCollider(physics_, kind.plants, variant, foot, placement.turn, placement.scale);
        }
        // Tall plants' crowns hide part of the sky from the ground below them.
        if (variant.height * placement.scale > 6.0f) {
            crowns_.push_back({x, z, (variant.half_width.x + variant.half_width.y) * 0.5f * placement.scale * 0.9f});
        }
        ++placed_count_;
    }

    // Whether a large thing may stand at a world position: not in the small clearing at the
    // start, and not in the clearing that opens from the start towards the lake.
    bool KeepsTheViewOpen(float x, float z) const {
        if (x * x + z * z < 14.0f * 14.0f) {
            return false;
        }
        if (lake_distance_ > 0.0f) {
            const glm::vec2 offset(x, z);
            const float along = glm::dot(offset, lake_direction_);
            const float across = std::fabs(lake_direction_.x * offset.y - lake_direction_.y * offset.x);
            if (along > -8.0f && along < lake_distance_ && across < 8.0f + along * 0.4f) {
                return false;
            }
        }
        return true;
    }

    // Plants everything the level's "plant" rules describe, group by group, around the start.
    void PlantForest() {
        std::vector<std::string> group_names;
        std::vector<std::vector<const poligon::PlantRule*>> groups;
        for (size_t i = 0; i < rules_.size(); ++i) {
            const poligon::PlantRule& rule = rules_[i];
            if (rule.is_cover) {
                continue;
            }
            if (FindKind(rule.kind) == nullptr) {
                std::printf("forest_scene: a rule plants \"%s\", which is not loaded\n", rule.kind.c_str());
                continue;
            }
            const std::string name = rule.group.empty() ? "#" + std::to_string(i) : rule.group;
            const auto found = std::find(group_names.begin(), group_names.end(), name);
            if (found == group_names.end()) {
                group_names.push_back(name);
                groups.push_back({&rule});
            } else {
                groups[static_cast<size_t>(found - group_names.begin())].push_back(&rule);
            }
        }
        for (const std::vector<const poligon::PlantRule*>& group : groups) {
            std::vector<const Kind*> kinds;
            std::vector<int> variant_counts;
            for (const poligon::PlantRule* rule : group) {
                kinds.push_back(FindKind(rule->kind));
                variant_counts.push_back(static_cast<int>(kinds.back()->plants.variants.size()));
            }
            const auto keep = [&](int rule, glm::vec2 place) {
                return kinds[static_cast<size_t>(rule)]->is_small ||
                       KeepsTheViewOpen(place.x - map_offset_.x, place.y - map_offset_.y);
            };
            const auto placements = poligon::PlacePlantGroup(maps_, group, map_offset_, 1500.0f, variant_counts, keep);
            for (const auto& [rule, placement] : placements) {
                PlaceAt(*kinds[static_cast<size_t>(rule)], placement, group[static_cast<size_t>(rule)]->sink);
            }
        }
        std::printf("forest_scene: %d things placed by %zu rules\n", placed_count_, rules_.size());
        BuildSkyOcclusion();
        GrowGroundCover();
    }

    // How much of the sky each place sees: the land around it (out to 160 m) and the crowns
    // of the trees above it, over 2.4 km around the start.
    void BuildSkyOcclusion() {
        const auto started = std::chrono::steady_clock::now();
        sky_occlusion_ = poligon::MakeTerrainSkyOcclusion(*terrain_, {0.0f, 0.0f}, 2400.0f, 4.0f);
        for (const glm::vec3& crown : crowns_) {
            poligon::AddCanopy(sky_occlusion_, {crown.x, crown.y}, crown.z, 0.55f);
        }
        renderer_.SetSkyOcclusion(&sky_occlusion_);
        std::printf("forest_scene: sky occlusion of the land and %zu crowns in %.1f s\n", crowns_.size(),
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
    }

    // Grass, ferns, nettles, low shrubs and stones are not planted: they grow on the fly
    // around the player (poligon/world/ground_cover.h) from rules and the land, over the whole map.
    void GrowGroundCover() {
        ground_cover_ = std::make_unique<poligon::GroundCover>(*terrain_, kGroundCoverRadius);
        // One layer per variant of a kind, sharing the kind's density between them. A
        // kind's levels are cut at the radius, so that plants fade out before their cell goes.
        // every_nth: use only every n-th variant of the kind (each variant is a draw call per
        // pass). shadow_limit: shadows of the layer stop here at the latest; grass shadows
        // matter only at the player's feet.
        ground_cover_->SetMaps(&maps_, map_offset_);
        // One layer per variant of a rule's kind, sharing the rule's density between them. A
        // kind's levels are cut at the radius, so that plants fade out before their cell goes.
        // Dense cover (more than a plant per square meter, grass) casts shadows only at the
        // player's feet.
        for (const poligon::PlantRule& rule : rules_) {
            if (!rule.is_cover) {
                continue;
            }
            const Kind* kind = FindKind(rule.kind);
            if (kind == nullptr) {
                std::printf("forest_scene: a cover rule grows \"%s\", which is not loaded\n", rule.kind.c_str());
                continue;
            }
            const std::vector<poligon::PlantVariant>& variants = kind->plants.variants;
            const size_t every_nth = static_cast<size_t>(std::max(rule.every_nth, 1));
            const float shadow_limit = rule.density > 1.0f ? 8.0f : 20.0f;
            for (size_t i = 0; i < variants.size(); i += every_nth) {
                poligon::GroundCoverLayer layer;
                layer.species = variants[i].species;
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
                layer.density = rule.density / static_cast<float>((variants.size() + every_nth - 1) / every_nth);
                layer.scale_min = rule.scale_min;
                layer.scale_max = rule.scale_max;
                if (rule.patch_size > 0.0f) {
                    layer.patch_size = rule.patch_size;
                    layer.patch_threshold = 1.0f - rule.patch_cover;
                    layer.patch_softness = 0.25f;
                }
                layer.max_slope_degrees = 90.0f;  // the rule's conditions decide
                layer.align_to_ground = rule.align_to_ground;
                layer.sink_meters = rule.sink * variants[i].height * (rule.scale_min + rule.scale_max) * 0.5f;
                layer.seed = rule.seed * 100 + static_cast<int>(i);
                layer.conditions = rule.conditions;
                ground_cover_->AddLayer(layer);
            }
        }
    }

    poligon::Renderer& renderer_;
    std::filesystem::path sources_;
    const poligon::Input& input_;
    ForestActions actions_;

    std::filesystem::path terrain_cache_;
    // Cooked models and textures, loaded by name, and the level.
    poligon::AssetLibrary library_;
    poligon::Level level_;
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
        rocks_, stump_, fallen_trunk_, reed_, cattail_, water_lily_, short_grass_, meadow_grass_, flowers_, grass_medium_, grass_tall_, pine_b_,
        spruce_b_, birch_b_, oak_b_;
    // Models made in memory (water plants), kept for their meshes and textures.
    std::vector<std::unique_ptr<poligon::LoadedModel>> procedural_models_;
    // The level's plant rules, and where plants have been walked flat.
    std::vector<poligon::PlantRule> rules_;
    poligon::TrampleMap trample_{128.0f, 256};
    // Crowns of tall plants (x, z, radius) and the sky occlusion made from them and the land.
    std::vector<glm::vec3> crowns_;
    poligon::SkyOcclusionMap sky_occlusion_;
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
    std::shared_ptr<const poligon::AnimationSet> fox_animations_;
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
    bool fullscreen_toggle_pending_ = false;
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
    // Fair-weather clouds, their shadows drifting over the land, and the haze of distance
    // that turns far hills blue.
    environment.clouds = true;
    environment.cloud_coverage = 0.32f;
    environment.cloud_bottom = 1400.0f;
    environment.cloud_top = 3200.0f;
    environment.cloud_density = 0.025f;
    environment.cloud_speed = 6.0f;
    environment.aerial_density = 0.00012f;
    environment.foliage_translucency = 0.5f;

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
    if (options.headless && options.ticks == 0 && options.replay_path.empty()) {
        options.ticks = 1;
    }

    const std::filesystem::path assets = FindAssetsFolder(argv[0]);
    std::error_code assets_error;
    if (assets.empty() || !std::filesystem::is_directory(assets / "source", assets_error)) {
        const char* message = "No folder named poligon-assets, with the asset sources in its folder source, found next to the project folder.";
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

    // Input: a recording if one is given (--replay), otherwise the keyboard and mouse when
    // there is a window; --record writes every tick's actions to a file.
    constexpr int kTicksPerSecond = 60;
    poligon::Input input;
    const ForestActions actions = SetUpInput(input);
    if (!options.replay_path.empty()) {
        if (!input.StartReplay(options.replay_path.c_str(), kTicksPerSecond, error)) {
            std::fprintf(stderr, "forest_scene: %s\n", error.c_str());
            return 2;
        }
        if (options.ticks == 0) {
            options.ticks = input.GetReplayTickCount();
        }
    } else if (!options.headless) {
        input.UseLiveDevices();
    }
    if (!options.record_path.empty() && !input.StartRecording(options.record_path.c_str(), kTicksPerSecond, error)) {
        std::fprintf(stderr, "forest_scene: %s\n", error.c_str());
        return 2;
    }

    const auto load_start = std::chrono::steady_clock::now();
    // Generated land is kept in the build folder (the program runs from build/PRESET/bin);
    // the asset list is in the project's tools/forest_scene.
    std::error_code path_error;
    const std::filesystem::path build_folder =
        std::filesystem::absolute(argv[0], path_error).parent_path().parent_path().parent_path();
    const std::filesystem::path scene_folder = build_folder.parent_path() / "tools" / "forest_scene";
    ForestGame game(renderer, assets, build_folder, scene_folder / "assets.txt", scene_folder / "forest.level", input, actions, view,
                    options.positional.size() >= 3, settings, settings_path);
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
    renderer.SetWindowTitle("Poligon forest scene  (W/S/A/D walk, Shift run, F fly, Tab windows, F11 full screen, drag the mouse to look)");
#else
    renderer.SetWindowTitle("Poligon forest scene  [DEBUG BUILD, slow: use build/release/bin]");
#endif

    poligon::GameLoopConfig loop_config;
    loop_config.headless = options.headless;
    loop_config.ticks_per_second = kTicksPerSecond;
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
