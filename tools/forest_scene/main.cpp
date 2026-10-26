// Showcase: 6 x 6 km of land made by the engine's terrain generator, with a piece of
// forest above a lake, put together from the models in the poligon-assets folder, to walk
// through and look at. It exists to judge the renderer on a whole scene and to measure how
// long a frame takes. The land is generated on the first run (about three minutes) and then
// loaded from build/terrain_cache. The models and textures are named in assets.txt (next to
// this file); their sources are in poligon-assets/source, and they are cooked into
// poligon-assets/cooked when the program starts, whenever the list or a source changed
// (poligon/assets/assets.h). A released copy has no sources: then poligon-assets/cooked holds
// one archive, assets.pak (made by tools/package_assets.ps1), with the land already cooked,
// and assets.txt and forest.level lie next to the program. The land's settings, its ground
// layers, the player's start, things put by hand and the rules that plant the forest, the
// shore and the ground cover are in forest.level (poligon/world/level.h,
// poligon/world/plant_rules.h). The land is streamed (poligon/world/streaming.h): cooked into
// 512 m patches with the other assets, and loaded in a window around the camera. Walking
// flattens grass into paths (poligon/world/trample.h).
//
// Usage: forest_scene [X Z TURN TILT [collision] [lights]] [standard Poligon options]
//   X Z         where to stand, in meters from the start (default 0 0): a slope above
//               the largest lake, chosen so that the lake is in view
//   TURN TILT   where to look, in degrees (default: towards the lake, slightly down)
//   collision   the word itself: start with the collision shapes shown
//   lights      the word itself: 16 point lights around the camera (to measure what they cost)
//   noocclusion the word itself: draw what hills hide too (to measure what leaving it out saves)
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
#include "poligon/assets/cooking.h"
#include "poligon/core/command_line.h"
#include "poligon/core/crash_handler.h"
#include "poligon/core/fatal_error.h"
#include "poligon/debug/debug_ui.h"
#include "poligon/debug/free_camera.h"
#include "poligon/core/game_loop.h"
#include "poligon/render/graphics_quality.h"
#include "poligon/world/footprints.h"
#include "poligon/world/ground_cover.h"
#include "poligon/render/decal.h"
#include "poligon/render/impostor.h"
#include "poligon/core/input.h"
#include "poligon/world/land.h"
#include "poligon/world/level.h"
#include "poligon/world/particles.h"
#include "poligon/assets/model.h"
#include "poligon/physics/physics.h"
#include "poligon/world/plant_rules.h"
#include "poligon/world/plants.h"
#include "poligon/world/procedural_plants.h"
#include "poligon/core/platform.h"
#include "poligon/render/render_settings.h"
#include "poligon/world/sky_occlusion.h"
#include "poligon/world/streaming.h"
#include "poligon/world/terrain.h"
#include "poligon/world/trample.h"
#include "poligon/world/vegetation.h"
#include "poligon/render/renderer.h"
#include "poligon/world/water.h"

namespace {

constexpr float kForestHalfSize = 250.0f;  // plants stand within this distance of the start
// Beyond that, big trees alone continue to this distance; far away they are impostors.
constexpr float kFarForestHalfSize = 1200.0f;
// Windows of the streamed land, in 512 m patches around the camera (radius 4 = 9 x 9) or,
// for collisions, around the player and the crates. The terrain's pages hold its window and
// the patches kept half a patch beyond it (see poligon/world/streaming.h).
constexpr int kTerrainWindow = 4;
constexpr int kTerrainPages = (2 * kTerrainWindow + 2) * (2 * kTerrainWindow + 2);
constexpr int kPlantWindow = 3;
constexpr int kSkyWindow = 2;
constexpr int kCollisionWindow = 1;
constexpr float kEyeHeight = 1.7f;
constexpr float kCrateSize = 0.6f;
// Object ids for the renderer's motion vectors: crates by physics body, foxes by number.
constexpr std::uint64_t kCrateObjectIds = 0x100000000ull;
constexpr std::uint64_t kAnimalObjectIds = 0x200000000ull;
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
// Pictures for the decals (defined below).
poligon::Image MakeBootPrintImage();
poligon::Image MakeBloodImage();

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
    poligon::Transform transform;
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
               const float view[4], bool turn_given, const poligon::RenderSettings& settings, const std::string& settings_path,
               bool synchronous_streaming)
        : renderer_(renderer), sources_(assets / "source"), cooked_(assets / "cooked"), terrain_cache_(build_folder / "terrain_cache"),
          input_(input), actions_(actions), settings_(settings), settings_path_(settings_path) {
        OpenAssets(asset_list, cooked_);
        if (settings.environment.clouds) {
            renderer_.PrepareClouds();
        }
        std::string level_error;
        std::vector<std::string> level_words = poligon::GetPlantRuleWords();
        for (const std::string& word : poligon::GetGroundLayerWords()) {
            level_words.push_back(word);
        }
        if (!poligon::LoadLevel(level_path.string().c_str(), level_, level_error, level_words)) {
            std::printf("forest_scene: %s; using the default land\n", level_error.c_str());
        } else if (!poligon::ParsePlantRules(level_.other_lines, rules_, level_error) ||
                   !poligon::ParseGroundLayers(level_.other_lines, ground_layers_, level_error)) {
            std::printf("forest_scene: %s: %s\n", level_path.string().c_str(), level_error.c_str());
        }
        if (level_.world.empty()) {
            level_.world = "worlds/forest";
        }
        renderer_.SetTrampleMap(&trample_);
        trample_.SetRememberOutside(true);  // paths stay where the player left them
        CreateGround();
        camera_.position = {view[0], Ground(view[0], view[1]) + kEyeHeight, view[1]};
        camera_.yaw_degrees = turn_given ? view[2] : start_yaw_;
        camera_.pitch_degrees = view[3];
        camera_.vertical_fov_degrees = 65.0f;
        LoadKinds();
        PlaceLevelObjects();
        PlantForest();
        GrowGroundCover();
        // The land around the start, before anything stands on it: collisions now, and the
        // drawn data too (at once in headless runs, so that pictures do not depend on timing).
        streamer_->SetSynchronous(synchronous_streaming);
        const glm::dvec3 start(0.0, Ground(0.0, 0.0), 0.0);
        const glm::dvec3 standing(view[0], Ground(view[0], view[1]), view[1]);
        streamer_->RequirePatches({start, standing});
        streamer_->Update(camera_.position);
        streamer_->FinishLoading();
        std::printf("forest_scene: %d land patches, %d plants and rocks, %d lake surfaces held around the start\n",
                    terrain_->GetPatchCount(), vegetation_.GetInstanceCount(), water_->GetSurfaceCount());
        AddAnimals();
        AddCrates();

        poligon::CharacterSettings walker;
        walker.position = {view[0], Ground(view[0], view[1]), view[1]};
        walker_ = physics_.AddCharacter(walker);
        std::printf("forest_scene: %d physics bodies\n", physics_.GetBodyCount());
        SetUpTracesAndWeather();
    }

    int GetLoadedKindCount() const { return loaded_kind_count_; }
    // Height of the land at a world position, from the land's patches themselves (read if
    // not held), so that what stands on the land never depends on what has been loaded.
    double Ground(double x, double z) const {
        const glm::vec2 map_place = ToMap(x, z);
        return land_->GetInfo().world_origin.y + static_cast<double>(LandAt(x, z)->maps.height.GetHeight(map_place.x, map_place.y));
    }
    void ShowCollision(bool show) { show_collision_ = show; }
    void ShowTestLights(bool show) { test_lights_ = show; }
    // What hills hid in the last frame (TerrainOcclusion).
    void PrintOcclusion() const {
        std::printf("hidden by hills: %d of %d plant cells, %d land squares\n", vegetation_.GetStats().cells_hidden_by_terrain,
                    vegetation_.GetStats().cells_total, terrain_->GetStats().patches_hidden);
    }
    void UseTerrainOcclusion(bool use) {
        use_occlusion_ = use;
        const poligon::TerrainOcclusion* occlusion = use ? &occlusion_ : nullptr;
        if (terrain_) {
            terrain_->SetOcclusion(occlusion);
        }
        vegetation_.SetOcclusion(occlusion);
        if (ground_cover_) {
            ground_cover_->GetVegetation().SetOcclusion(occlusion);
        }
    }
    // How fast the world's own time runs (wind, clouds, water, animals): 1 = real time,
    // 0 = stopped. The player and physics always run in real time. Also in the Simulation window.
    void SetTimeScale(float scale) { time_scale_ = glm::clamp(scale, 0.0f, 20.0f); }
    glm::dvec3 GetCameraPosition() const { return camera_.position; }

    // Where the crates are, exactly (the first six are the pile, the rest thrown).
    void PrintCrates() const {
        for (const poligon::BodyHandle crate : crates_) {
            const glm::dvec3 position = physics_.GetPosition(crate);
            std::printf("crate %u at %.17g %.17g %.17g\n", crate.id, position.x, position.y, position.z);
        }
    }

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
        // The world's own time (wind, clouds, water, animals) may run faster or slower.
        const float world_delta = delta * time_scale_;
        time_seconds_ += world_delta;
        for (Animal& animal : animals_) {
            animal.player->Update(world_delta);
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
            physics_.SetCharacterPosition(walker_, camera_.position - glm::dvec3(0.0, kEyeHeight, 0.0));
        }
        if (flying_) {
            camera_.Move(forward * delta, right * delta, input_.GetValue(actions_.rise) * speed * delta);
            physics_.SetCharacterInput(walker_, glm::vec3(0.0f), false);
        } else {
            // Walking: the wish to move is level whatever the view direction; the physics
            // world keeps the character on the ground and out of trunks and rocks.
            poligon::FreeCamera level = camera_;
            level.pitch_degrees = 0.0f;
            level.position = glm::dvec3(0.0);
            level.Move(forward, right, 0.0f);
            physics_.SetCharacterInput(walker_, glm::vec3(level.position), input_.WasPressed(actions_.jump));
        }
        // Collisions around everything that moves, loaded now if missing: by the game state
        // alone, never by how far the drawn land has loaded.
        std::vector<glm::dvec3> moving = {physics_.GetCharacterPosition(walker_)};
        for (const poligon::BodyHandle crate : crates_) {
            moving.push_back(physics_.GetPosition(crate));
        }
        streamer_->RequirePatches(moving);
        const auto physics_start = std::chrono::steady_clock::now();  // statistics only
        physics_.Step(delta);
        physics_milliseconds_sum_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - physics_start).count();
        ++physics_steps_;
        if (!flying_) {
            camera_.position = physics_.GetCharacterPosition(walker_) + glm::dvec3(0.0, kEyeHeight, 0.0);
        }
        // Grass under walking feet goes flat in about half a second and stands up again
        // over about a minute and a half; walking the same way again makes a path.
        const glm::dvec2 feet(camera_.position.x, camera_.position.z);
        trample_.Recenter(feet);
        if (!flying_ && (forward != 0.0f || right != 0.0f)) {
            trample_.Trample(feet, 0.5f, 2.0f * delta);
        }
        for (const Animal& animal : animals_) {
            if (animal.circle_radius > 0.0f) {
                trample_.Trample({animal.transform.position.x, animal.transform.position.z}, 0.3f, 1.5f * delta);
            }
        }
        trample_.Recover(0.011f * delta);
        if (!flying_) {
            footprints_.Update(physics_.GetCharacterPosition(walker_), [this](double x, double z) { return TakesPrints(x, z); });
        }
        UpdateWeather(delta);
    }

    // Average processor time of EndFrame over the run, for the summary at the end.
    float GetAverageCpuMilliseconds() const {
        return cpu_frames_ > 0 ? static_cast<float>(cpu_milliseconds_sum_ / cpu_frames_) : 0.0f;
    }
    float GetAveragePhysicsMilliseconds() const {
        return physics_steps_ > 0 ? static_cast<float>(physics_milliseconds_sum_ / physics_steps_) : 0.0f;
    }
    float GetAverageWaitMilliseconds() const {
        return cpu_frames_ > 0 ? static_cast<float>(wait_milliseconds_sum_ / cpu_frames_) : 0.0f;
    }

    void Render(poligon::GameLoop& /*loop*/, const poligon::FrameInfo& frame_info) override {
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
        // The drawn land follows the camera; what has finished loading is handed over here.
        streamer_->Update(camera.position);

        ApplyQualityToSystems();
        // The scene's look, limited by the graphics quality.
        poligon::Environment environment = settings_.environment;
        poligon::PostSettings post = settings_.post;
        poligon::ApplyGraphicsQuality(settings_.quality, environment, post);
        renderer_.SetPostSettings(post);
        // The wind runs on game time, so a replay moves the plants exactly as the recording did.
        environment.wind_time = time_seconds_;
        environment.frame_seconds = static_cast<float>(frame_info.delta_seconds);
        renderer_.BeginFrame(camera, settings_.sun, environment);
        if (show_windows_) {
            poligon::ShowRenderSettingsWindow(renderer_, settings_, settings_path_.c_str());
            poligon::ShowFrameStatsWindow(renderer_);
        }
        occlusion_.Update(camera.position);
        terrain_->Draw(camera);
        water_->Draw(renderer_, water_material_);
        if (test_lights_) {
            // Sixteen warm lamps in two rings around the camera, 6 and 14 m out, 1.5 m below it.
            for (int i = 0; i < 16; ++i) {
                const double angle = 6.2831853 * (static_cast<double>(i % 8) + (i < 8 ? 0.0 : 0.5)) / 8.0;
                const double distance = i < 8 ? 6.0 : 14.0;
                poligon::PointLight lamp;
                lamp.position = camera.position + glm::dvec3(std::cos(angle) * distance, -1.5, std::sin(angle) * distance);
                lamp.color = {1.0f, 0.7f, 0.4f, 1.0f};
                lamp.intensity = 8.0f;
                lamp.range = 12.0f;
                renderer_.DrawLight(lamp);
            }
        }
        // Object ids (crates by body, foxes by number) let temporal anti-aliasing follow them.
        for (const poligon::BodyHandle crate : crates_) {
            poligon::Transform placed = physics_.GetTransform(crate);
            placed.scale = glm::vec3(kCrateSize);
            renderer_.DrawMesh(cube_mesh_, placed, crate_material_, kCrateObjectIds + crate.id);
        }
        if (show_collision_) {
            // Every body except the land's height fields, as the box that encloses it:
            // red = never moves, yellow = moves.
            for (std::uint32_t id = 1; id <= physics_.GetLastBodyId(); ++id) {
                const poligon::BodyHandle body{id};
                if (!physics_.IsBody(body)) {
                    continue;
                }
                glm::dvec3 bounds_min;
                glm::dvec3 bounds_max;
                physics_.GetBounds(body, bounds_min, bounds_max);
                if (glm::distance((bounds_min + bounds_max) * 0.5, camera.position) > 60.0 || bounds_max.x - bounds_min.x > 100.0) {
                    continue;
                }
                const poligon::Color color = physics_.IsDynamic(body) ? poligon::Color{1.0f, 0.9f, 0.1f, 1.0f}
                                                                       : poligon::Color{1.0f, 0.15f, 0.3f, 1.0f};
                renderer_.DrawMesh(cube_mesh_,
                                   poligon::Transform((bounds_min + bounds_max) * 0.5, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                                      glm::vec3(bounds_max - bounds_min)),
                                   color);
            }
        }
        for (size_t i = 0; i < animals_.size(); ++i) {
            poligon::DrawModel(renderer_, fox_model_, animals_[i].transform, animals_[i].player->GetJointMatrices(),
                               kAnimalObjectIds + i);
        }
        vegetation_.Draw(renderer_, camera);
        ground_cover_->Update(camera.position);
        ground_cover_->Draw(renderer_, camera);
        footprints_.Draw(renderer_, print_material_);
        renderer_.DrawDecal(blood_transform_, blood_material_);
        DrawWeather();
        DrawHud(camera);
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
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.0f, 400.0f), ImGuiCond_FirstUseEver,
                                    ImVec2(1.0f, 0.0f));
            if (ImGui::Begin("Simulation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("world time %.0f s (wind, clouds, water, animals)", time_seconds_);
                ImGui::PushItemWidth(160.0f);
                ImGui::SliderFloat("speed", &time_scale_, 0.0f, 20.0f, "x %.2f");
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("x 1")) {
                    time_scale_ = 1.0f;
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
    // in poligon-assets/cooked) and opens the cooked folder. Without sources it opens the
    // archive cooked/assets.pak instead.
    void OpenAssets(const std::filesystem::path& list_path, const std::filesystem::path& cooked) {
        poligon::AssetList list;
        std::string error;
        std::error_code exists_error;
        cooked_only_ = !std::filesystem::is_directory(sources_, exists_error);
        if (cooked_only_) {
            const std::filesystem::path archive = cooked / "assets.pak";
            if (!library_.Open(archive.string().c_str(), error)) {
                std::printf("forest_scene: %s\n", error.c_str());
            }
            return;
        }
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

    // Where the player starts: as the level says, above the largest lake (found when the
    // land was cooked) or at a given place. Sets start_yaw_ and what is known of the lake.
    // Positions are meters on the map.
    glm::vec2 ChooseStart(const poligon::LandInfo& info) {
        if (!level_.start_at_lake) {
            start_yaw_ = level_.start_turn_degrees;
            return glm::vec2(level_.start_position);
        }
        if (!info.has_lake_viewpoint) {
            std::printf("forest_scene: no place above a lake; starting in the middle of the land\n");
            return {info.size_x * 0.5f, info.size_z * 0.5f};
        }
        const poligon::LakeViewpoint& viewpoint = info.lake_viewpoint;
        start_yaw_ = viewpoint.yaw_degrees;
        start_lake_level_ = info.lakes[static_cast<size_t>(viewpoint.lake)].level;
        lake_direction_ = viewpoint.direction;
        lake_distance_ = viewpoint.distance;
        return viewpoint.position;
    }

    // World x, z to map coordinates, and the land patch holding a world place (read if not held).
    glm::vec2 ToMap(double x, double z) const {
        const glm::dvec3& origin = land_->GetInfo().world_origin;
        return glm::vec2(static_cast<float>(x - origin.x), static_cast<float>(z - origin.z));
    }
    std::shared_ptr<const poligon::LandPatch> LandAt(double x, double z) const {
        const poligon::LandInfo& info = land_->GetInfo();
        const glm::vec2 map_place = ToMap(x, z);
        const poligon::PatchCoord patch{
            std::clamp(static_cast<int>(std::floor(map_place.x / info.grid.patch_size)), 0, info.grid.patch_count_x - 1),
            std::clamp(static_cast<int>(std::floor(map_place.y / info.grid.patch_size)), 0, info.grid.patch_count_z - 1)};
        return land_->GetPatch(patch);
    }

    // The land: generated by the engine (poligon/world/terrain_generator.h) and cooked into
    // patches on the first run (poligon/world/land.h), then streamed from the cooked files.
    // Its ground textures follow the level's ground layers. The world's origin is put at the
    // player's start, at the land's height there.
    void CreateGround() {
        std::string error;
        bool cooked = false;
        const auto started = std::chrono::steady_clock::now();
        // A released copy has the land in its archive already.
        if (!cooked_only_ &&
            !poligon::CookLevelWorld(level_, ground_layers_, cooked_.string().c_str(), terrain_cache_.string().c_str(), cooked, error)) {
            poligon::FatalError(("forest_scene: " + error).c_str());
        }
        std::printf("forest_scene: land %s in %.1f s\n", cooked ? "generated and cooked into patches" : "cooked already",
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
        // The land's description first, to find the start and its height.
        poligon::LandFromFiles probe(library_, level_.world, glm::dvec3(0.0));
        if (!probe.IsOpen()) {
            poligon::FatalError(("forest_scene: " + probe.GetError()).c_str());
        }
        const glm::vec2 start = ChooseStart(probe.GetInfo());
        const double patch_size = probe.GetInfo().grid.patch_size;
        const poligon::PatchCoord start_patch{static_cast<int>(start.x / patch_size), static_cast<int>(start.y / patch_size)};
        const double start_height = probe.GetPatch(start_patch)->maps.height.GetHeight(start.x, start.y);
        map_offset_ = start;
        const glm::dvec3 origin(-start.x, -start_height, -start.y);
        world_height_offset_ = origin.y;
        land_ = std::make_unique<poligon::LandFromFiles>(library_, level_.world, origin);
        const poligon::LandInfo& info = land_->GetInfo();
        vegetation_.SetCellOrigin({origin.x, origin.z});  // cells must fit the patches

        // Ground layers (cooked, block-compressed), as the level names them.
        poligon::TerrainConfig config;
        config.detail = kTerrainDetail;
        std::vector<poligon::CompressedImage> colors;
        std::vector<poligon::CompressedImage> normals;
        for (size_t i = 0; i < ground_layers_.size(); ++i) {
            const poligon::CompressedImage* color = library_.LoadImage(ground_layers_[i].texture + "_color", error);
            const poligon::CompressedImage* normal = color != nullptr ? library_.LoadImage(ground_layers_[i].texture + "_normal", error) : nullptr;
            if (color == nullptr || normal == nullptr) {
                std::printf("forest_scene: %s; the ground is drawn without textures\n", error.c_str());
                colors.clear();
                break;
            }
            colors.push_back(*color);
            normals.push_back(*normal);
            config.material.tile_sizes[i] = ground_layers_[i].tile_size;
        }
        if (!colors.empty()) {
            config.material.colors = renderer_.CreateTextureArray(colors.data(), static_cast<int>(colors.size()));
            config.material.normals = renderer_.CreateTextureArray(normals.data(), static_cast<int>(normals.size()));
            config.material.layer_count = static_cast<int>(colors.size());
        }
        config.material.steep_start_degrees = 90.0f;  // a rock layer covers steep ground
        terrain_ = std::make_unique<poligon::Terrain>(renderer_, info, config, kTerrainPages);
        // Plant cells and land squares a hill hides from the camera only cast shadows.
        occlusion_.SetFloor(&info.occlusion_floor, info.world_origin);
        if (use_occlusion_) {
            terrain_->SetOcclusion(&occlusion_);
            vegetation_.SetOcclusion(&occlusion_);
        }

        // What is streamed, each with its own window: the land drawn and its water around the
        // camera, the land's collisions around the player and the crates.
        poligon::StreamingGridSettings grid = info.grid;
        grid.origin = {origin.x, origin.z};
        grid_ = std::make_unique<poligon::StreamingGrid>(grid);
        terrain_source_ = std::make_unique<poligon::TerrainPatchSource>(*terrain_, *land_);
        terrain_collisions_ = std::make_unique<poligon::TerrainPhysicsSource>(physics_, *land_);
        water_ = std::make_unique<poligon::WaterPatchSource>(renderer_, *land_);
        streamer_ = std::make_unique<poligon::WorldStreamer>(*grid_);
        streamer_->AddSource(*terrain_source_, kTerrainWindow, poligon::StreamingMode::Background);
        streamer_->AddSource(*water_, kTerrainWindow, poligon::StreamingMode::Background);
        streamer_->AddSource(*terrain_collisions_, kCollisionWindow, poligon::StreamingMode::Immediate);
        std::printf("forest_scene: land %.0f x %.0f m in %d x %d patches of %.0f m, %zu lakes; start at %.0f, %.0f on the map, "
                    "%.0f m above the largest lake, %.0f m from its middle, which lies at turn %.0f\n",
                    static_cast<double>(info.size_x), static_cast<double>(info.size_z), info.grid.patch_count_x,
                    info.grid.patch_count_z, info.grid.patch_size, info.lakes.size(), static_cast<double>(start.x),
                    static_cast<double>(start.y), start_height - static_cast<double>(start_lake_level_),
                    static_cast<double>(lake_distance_), static_cast<double>(start_yaw_));
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
            const double x = object.position.x - map_offset_.x;
            const double z = object.position.y - map_offset_.y;
            const float turn = glm::radians(object.turn_degrees);
            const glm::dvec3 foot(x, Ground(x, z) - static_cast<double>(variant.height * object.scale * 0.15f), z);
            vegetation_.AddInstance(variant.species_id,
                                    poligon::Transform(foot, glm::angleAxis(turn, glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(object.scale)));
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
        const double ground = Ground(place.x, place.y);
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
        settings.position = camera_.position + glm::dvec3(forward * 0.9f);
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
        animal.transform = poligon::Transform(glm::dvec3(place.x, Ground(place.x, place.y), place.y),
                                              glm::angleAxis(facing, glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(fox_scale_));
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

    // Plants everything the level's "plant" rules describe, group by group, around the start:
    // a planter that the streamed sources call for each patch (drawn plants, their
    // collisions, and the crowns that hide the sky).
    void PlantForest() {
        std::vector<std::string> group_names;
        for (size_t i = 0; i < rules_.size(); ++i) {
            const poligon::PlantRule& rule = rules_[i];
            if (rule.is_cover) {
                continue;
            }
            const Kind* kind = FindKind(rule.kind);
            if (kind == nullptr) {
                std::printf("forest_scene: a rule plants \"%s\", which is not loaded\n", rule.kind.c_str());
                continue;
            }
            const std::string name = rule.group.empty() ? "#" + std::to_string(i) : rule.group;
            const auto found = std::find(group_names.begin(), group_names.end(), name);
            if (found == group_names.end()) {
                group_names.push_back(name);
                plant_groups_.emplace_back();
            }
            PlantGroup& group = plant_groups_[static_cast<size_t>(std::find(group_names.begin(), group_names.end(), name) - group_names.begin())];
            group.rules.push_back(&rule);
            group.kinds.push_back(kind);
            group.variant_counts.push_back(static_cast<int>(kind->plants.variants.size()));
        }
        // Called on the loading thread too: it only reads (the groups, the kinds, the start).
        planter_ = [this](const poligon::LandPatch& land, glm::vec2 low, glm::vec2 high, std::vector<poligon::PlacedPlant>& plants) {
            for (const PlantGroup& group : plant_groups_) {
                const auto keep = [&](int rule, glm::vec2 place) {
                    return group.kinds[static_cast<size_t>(rule)]->is_small ||
                           KeepsTheViewOpen(place.x - map_offset_.x, place.y - map_offset_.y);
                };
                for (const auto& [rule, placement] :
                     poligon::PlacePlantGroupInRectangle(land.maps, group.rules, map_offset_, 1500.0f, low, high, group.variant_counts, keep)) {
                    const Kind& kind = *group.kinds[static_cast<size_t>(rule)];
                    const poligon::PlantVariant& variant = kind.plants.variants[static_cast<size_t>(placement.variant)];
                    // The foot at the ground (or water) there, pushed into it by `sink` of the plant's height.
                    const float sink = group.rules[static_cast<size_t>(rule)]->sink;
                    poligon::PlacedPlant plant;
                    plant.kind = &kind.plants;
                    plant.variant = placement.variant;
                    plant.foot = glm::dvec3(static_cast<double>(placement.position.x) - map_offset_.x,
                                            static_cast<double>(placement.height - variant.height * placement.scale * sink) + world_height_offset_,
                                            static_cast<double>(placement.position.y) - map_offset_.y);
                    plant.turn = placement.turn;
                    plant.scale = placement.scale;
                    plants.push_back(plant);
                }
            }
        };
        plants_ = std::make_unique<poligon::PlantPatchSource>(vegetation_, *land_, planter_);
        plant_colliders_ = std::make_unique<poligon::PlantColliderSource>(physics_, *land_, planter_);
        // How much of the sky each place sees: the land around it (out to 160 m) and the
        // crowns of the trees above it.
        sky_ = std::make_unique<poligon::SkyOcclusionPatchSource>(*land_, kSkyWindow);
        sky_->SetCanopy(planter_);
        renderer_.SetSkyOcclusion(sky_.get());
        streamer_->AddSource(*plants_, kPlantWindow, poligon::StreamingMode::Background);
        streamer_->AddSource(*sky_, kSkyWindow, poligon::StreamingMode::Background);
        streamer_->AddSource(*plant_colliders_, kCollisionWindow, poligon::StreamingMode::Immediate);
    }

    // Grass, ferns, nettles, low shrubs and stones are not planted: they grow on the fly
    // around the player (poligon/world/ground_cover.h) from rules and the land, over the whole map.
    void GrowGroundCover() {
        ground_cover_ = std::make_unique<poligon::GroundCover>(*terrain_, kGroundCoverRadius);
        if (use_occlusion_) {
            ground_cover_->GetVegetation().SetOcclusion(&occlusion_);
        }
        // One layer per variant of a kind, sharing the kind's density between them. A
        // kind's levels are cut at the radius, so that plants fade out before their cell goes.
        // every_nth: use only every n-th variant of the kind (each variant is a draw call per
        // pass). shadow_limit: shadows of the layer stop here at the latest; grass shadows
        // matter only at the player's feet.
        ground_cover_->SetLand(land_.get());
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
                const int layer_index = ground_cover_->AddLayer(layer);
                if (rule.kind.find("grass") != std::string::npos) {
                    grass_layers_.push_back(layer_index);
                }
            }
        }
    }

    poligon::Renderer& renderer_;
    std::filesystem::path sources_;
    // No sources: the cooked archive is used as it is (a released copy).
    bool cooked_only_ = false;
    const poligon::Input& input_;
    ForestActions actions_;

    std::filesystem::path cooked_;
    std::filesystem::path terrain_cache_;
    // Cooked models and textures, loaded by name, and the level with its ground layers.
    poligon::AssetLibrary library_;
    poligon::Level level_;
    std::vector<poligon::GroundLayer> ground_layers_;
    // Map position = world position + map_offset_.
    glm::vec2 map_offset_{0.0f};
    // World height = map height + world_height_offset_.
    double world_height_offset_ = 0.0;
    float start_yaw_ = 270.0f;
    float start_lake_level_ = -1.0e30f;
    // Direction (map x, z) and distance from the start to the largest lake's middle.
    glm::vec2 lake_direction_{0.0f};
    float lake_distance_ = 0.0f;
    std::unique_ptr<poligon::Terrain> terrain_;
    // How the lakes' water looks (the defaults: clear lake water).
    poligon::WaterMaterial water_material_;
    // The player's prints on bare ground, and the trail they walked in on: 40 m to the start.
    void SetUpTracesAndWeather() {
        print_material_.texture = renderer_.CreateTexture(MakeBootPrintImage());
        print_material_.roughness = 0.95f;
        blood_material_.texture = renderer_.CreateTexture(MakeBloodImage());
        blood_material_.roughness = 0.35f;
        // To the level's start along its heading, whatever view the command line asked for,
        // so that every view sees the same trail.
        const glm::vec2 start(0.0f);
        const float yaw = glm::radians(start_yaw_);
        const glm::vec2 forward(-std::sin(yaw), -std::cos(yaw));
        for (int step = 0; step <= 800; ++step) {
            const glm::vec2 at = start - forward * (40.0f - 0.05f * static_cast<float>(step));
            footprints_.Update({at.x, Ground(at.x, at.y), at.y}, [this](double x, double z) { return TakesPrints(x, z); });
        }
        // Blood a little in front of the fox that stands still.
        blood_transform_ = poligon::Transform({-1.1, Ground(-1.1, -2.0), -2.0}, glm::angleAxis(0.7f, glm::vec3(0.0f, 1.0f, 0.0f)),
                                              {0.7f, 0.6f, 0.5f});
        dot_texture_ = renderer_.CreateTexture(poligon::MakeParticleDotImage(32));
        // The weather starts as if it had been going on for a while.
        for (int step = 0; step < 240; ++step) {
            UpdateWeather(1.0f / 40.0f);
        }
    }

    // Bare ground keeps prints: where the ground cover grows fewer than 4 grass plants per
    // square meter (half of what the short grass alone grows on open ground), not in water.
    bool TakesPrints(double x, double z) const {
        const glm::vec2 map_place = ToMap(x, z);
        if (LandAt(x, z)->maps.GetWaterDepth(map_place.x, map_place.y) > 0.0f) {
            return false;
        }
        float grass = 0.0f;
        for (const int layer : grass_layers_) {
            grass += ground_cover_->GetDensity(layer, x, z);
        }
        return grass < 4.0f;
    }

    // Rain and dust follow the camera; how much of each comes from the settings file
    // (weather.rain, weather.dust). Rain is slanted by the wind.
    void UpdateWeather(float delta) {
        const glm::vec2 wind = settings_.environment.wind_direction * (1.5f * settings_.environment.wind_strength);
        rain_.SetSettings(poligon::MakeRainSettings(settings_.weather.rain, wind));
        rain_.SetEmitting(settings_.weather.rain > 0.0f);
        rain_.SetPosition(camera_.position + glm::dvec3(0.0, 8.0, 0.0));
        rain_.Update(delta);
        dust_.SetSettings(poligon::MakeDustSettings(settings_.weather.dust));
        dust_.SetEmitting(settings_.weather.dust > 0.0f);
        dust_.SetPosition(camera_.position);
        dust_.Update(delta);
    }

    void DrawWeather() {
        if (rain_.GetParticleCount() > 0) {
            poligon::ParticleOptions streaks;
            streaks.stretch = 0.03f;
            streaks.soft_distance = 0.1f;
            renderer_.DrawParticles(poligon::TextureHandle{}, rain_.GetOrigin(), rain_.GetParticles().data(), rain_.GetParticleCount(), streaks);
        }
        if (dust_.GetParticleCount() > 0) {
            renderer_.DrawParticles(dot_texture_, dust_.GetOrigin(), dust_.GetParticles().data(), dust_.GetParticleCount());
        }
    }

    // A crosshair and a line of text in the built-in font: where the player is and which way
    // they look (no clock in it, so that headless frames repeat).
    void DrawHud(const poligon::Camera& camera) {
        const float cx = static_cast<float>(renderer_.GetWidth()) * 0.5f;
        const float cy = static_cast<float>(renderer_.GetHeight()) * 0.5f;
        const poligon::Color white{1.0f, 1.0f, 1.0f, 0.85f};
        renderer_.DrawRect({cx - 14.0f, cy - 1.0f, 9.0f, 2.0f}, white);
        renderer_.DrawRect({cx + 5.0f, cy - 1.0f, 9.0f, 2.0f}, white);
        renderer_.DrawRect({cx - 1.0f, cy - 14.0f, 2.0f, 9.0f}, white);
        renderer_.DrawRect({cx - 1.0f, cy + 5.0f, 2.0f, 9.0f}, white);
        const float heading = std::fmod(std::fmod(camera_.yaw_degrees, 360.0f) + 360.0f, 360.0f);
        char text[96];
        // + 0.0f: no "-0" for small negative positions.
        std::snprintf(text, sizeof(text), "x %.0f  z %.0f  heading %03.0f  prints %zu", std::round(camera.position.x) + 0.0f,
                      std::round(camera.position.z) + 0.0f, heading, footprints_.GetPrints().size());
        renderer_.DrawText(renderer_.GetBuiltinFont(), text, {16.0f, static_cast<float>(renderer_.GetHeight()) - 40.0f}, 24.0f,
                           {1.0f, 1.0f, 1.0f, 0.9f});
    }

    std::vector<poligon::RenderModel> render_models_;
    Kind pine_, spruce_, birch_, oak_, young_spruce_, young_pine_, shrub_, low_shrub_, fern_, nettle_, grass_, boulder_,
        rocks_, stump_, fallen_trunk_, reed_, cattail_, water_lily_, short_grass_, meadow_grass_, flowers_, grass_medium_, grass_tall_, pine_b_,
        spruce_b_, birch_b_, oak_b_;
    // Models made in memory (water plants), kept for their meshes and textures.
    std::vector<std::unique_ptr<poligon::LoadedModel>> procedural_models_;
    // The level's plant rules, and where plants have been walked flat.
    std::vector<poligon::PlantRule> rules_;
    poligon::TrampleMap trample_{128.0f, 256};
    int loaded_kind_count_ = 0;
    // All plants, rocks and stumps: placed once, drawn by distance.
    poligon::Vegetation vegetation_;
    std::unique_ptr<poligon::GroundCover> ground_cover_;
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
    float time_scale_ = 1.0f;  // see SetTimeScale
    double cpu_milliseconds_sum_ = 0.0;
    double wait_milliseconds_sum_ = 0.0;
    double physics_milliseconds_sum_ = 0.0;
    int physics_steps_ = 0;
    int cpu_frames_ = 0;

    // Ground, trunks, rocks, crates and the walking player.
    // Trunks and rocks of 3 x 3 patches around the player and each crate: room for many bodies.
    poligon::PhysicsWorld physics_{[] {
        poligon::PhysicsConfig config;
        config.max_bodies = 65536;
        return config;
    }()};
    poligon::CharacterHandle walker_;
    std::vector<poligon::BodyHandle> crates_;
    poligon::MeshHandle cube_mesh_;
    poligon::Material crate_material_;
    bool show_collision_ = false;
    bool test_lights_ = false;
    bool use_occlusion_ = true;
    poligon::TerrainOcclusion occlusion_;

    // Traces and weather (SetUpTracesAndWeather).
    poligon::FootprintTrail footprints_;
    std::vector<int> grass_layers_;  // ground cover layers of grass, for TakesPrints
    poligon::DecalMaterial print_material_;
    poligon::DecalMaterial blood_material_;
    poligon::Transform blood_transform_;
    poligon::ParticleEmitter rain_{poligon::ParticleEmitterSettings{}, 11};
    poligon::ParticleEmitter dust_{poligon::ParticleEmitterSettings{}, 12};
    poligon::TextureHandle dot_texture_;

    poligon::RenderSettings settings_;
    std::string settings_path_;
    poligon::FreeCamera camera_;
    bool flying_ = false;
    bool show_windows_ = true;
    bool fullscreen_toggle_pending_ = false;

    // The streamed land (CreateGround) and what is loaded from it by patches. The streamer is
    // declared last, so that it (and its loading thread) goes before the sources it uses.
    struct PlantGroup {
        std::vector<const poligon::PlantRule*> rules;
        std::vector<const Kind*> kinds;
        std::vector<int> variant_counts;
    };
    std::vector<PlantGroup> plant_groups_;
    poligon::Planter planter_;
    std::unique_ptr<poligon::LandFromFiles> land_;
    std::unique_ptr<poligon::StreamingGrid> grid_;
    std::unique_ptr<poligon::TerrainPatchSource> terrain_source_;
    std::unique_ptr<poligon::TerrainPhysicsSource> terrain_collisions_;
    std::unique_ptr<poligon::WaterPatchSource> water_;
    std::unique_ptr<poligon::PlantPatchSource> plants_;
    std::unique_ptr<poligon::PlantColliderSource> plant_colliders_;
    std::unique_ptr<poligon::SkyOcclusionPatchSource> sky_;
    std::unique_ptr<poligon::WorldStreamer> streamer_;
};

// The look the forest starts with: a fairly low sun, 29 degrees up, so that trunks throw
// long shadows through the haze. Sky and sunlight come from the atmosphere model, which
// warms and dims the sun by itself; sky.time_of_day in a settings file moves the sun to an
// hour of the day instead.
poligon::RenderSettings MakeForestSettings() {
    poligon::RenderSettings settings;
    settings.sun.direction = {-0.62f, -0.48f, -0.62f};
    // The sun's color above the air.
    settings.sun.color = {3.6f, 3.6f, 3.6f, 1.0f};

    poligon::Environment& environment = settings.environment;
    environment.draw_sky = true;
    environment.sky_model = poligon::SkyModel::Atmosphere;
    environment.sky_light = true;
    environment.sky_zenith = {0.18f, 0.34f, 0.72f, 1.0f};
    environment.sky_horizon = {0.72f, 0.76f, 0.80f, 1.0f};
    environment.sky_ground = {0.20f, 0.19f, 0.16f, 1.0f};
    environment.shadow_distance = kShadowDistance;
    // The real sun's size: shadows sharp at a trunk's foot, softer under the crowns.
    environment.sun_angular_size = 0.53f;
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
    // Fog and haze take the sky's color in the direction one looks (from the sky light map).
    environment.fog_sky_color = 1.0f;
    environment.wind_direction = {0.6f, 0.8f};
    environment.wind_strength = 1.0f;
    // Fair-weather clouds, their shadows drifting over the land, and the haze of distance
    // that turns far hills blue.
    environment.clouds = true;
    // Scattered fair-weather cumulus: coverage is not the share of the land in shade, and
    // with the sun 30 degrees up a cloud's shadow falls far to one side, so 0.24 already
    // shades about two fifths of the land (seen with post.debug_view cloud_shadow).
    environment.cloud_coverage = 0.24f;
    environment.cloud_bottom = 1400.0f;
    environment.cloud_top = 3200.0f;
    environment.cloud_density = 0.025f;
    environment.cloud_speed = 6.0f;
    environment.aerial_density = 0.00012f;
    // Dust floating in the sun around the player; rain off (weather.rain in the file).
    settings.weather.dust = 1.0f;
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

// A boot print: the sole and the heel, dark and soft-edged, the toe at the top of the picture.
poligon::Image MakeBootPrintImage() {
    constexpr int kWidth = 32;
    constexpr int kHeight = 64;
    poligon::Image image = poligon::MakeSolidImage(kWidth, kHeight, 38, 30, 22);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / kWidth * 2.0f - 1.0f;
            const float v = (static_cast<float>(y) + 0.5f) / kHeight * 2.0f - 1.0f;
            const float sole = 1.0f - std::hypot(u / 0.9f, (v + 0.3f) / 0.62f);
            const float heel = 1.0f - std::hypot(u / 0.75f, (v - 0.68f) / 0.28f);
            const float inside = std::max(sole, heel);
            image.pixels[static_cast<size_t>(y * kWidth + x) * 4 + 3] = static_cast<std::uint8_t>(glm::clamp(inside * 6.0f, 0.0f, 1.0f) * 190.0f);
        }
    }
    return image;
}

// A splash of blood with a ragged edge.
poligon::Image MakeBloodImage() {
    constexpr int kSize = 64;
    poligon::Image image = poligon::MakeSolidImage(kSize, kSize, 120, 8, 6);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / kSize * 2.0f - 1.0f;
            const float v = (static_cast<float>(y) + 0.5f) / kSize * 2.0f - 1.0f;
            const float angle = std::atan2(v, u);
            const float edge = 0.6f + 0.18f * std::sin(angle * 5.0f) + 0.1f * std::sin(angle * 11.0f + 1.0f);
            const float inside = edge - std::hypot(u, v);
            image.pixels[static_cast<size_t>(y * kSize + x) * 4 + 3] = static_cast<std::uint8_t>(glm::clamp(inside * 10.0f, 0.0f, 1.0f) * 230.0f);
        }
    }
    return image;
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
        for (size_t i = 4; i < options.positional.size(); ++i) {
            if (options.positional[i] != "collision" && options.positional[i] != "lights" && options.positional[i] != "noocclusion") {
                error = "expected at most X Z TURN TILT and the words collision, lights and noocclusion";
            }
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
                     "usage: forest_scene [X Z TURN TILT [collision] [lights]] [--headless --ticks N] [--backend d3d12|vulkan]\n"
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
    if (assets.empty() || (!std::filesystem::is_directory(assets / "source", assets_error) &&
                           !std::filesystem::is_regular_file(assets / "cooked" / "assets.pak", assets_error))) {
        const char* message = "No folder named poligon-assets found next to the program or the project folder, with the asset "
                              "sources in its folder source or the cooked archive cooked/assets.pak.";
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
    // the asset list and the level are in the project's tools/forest_scene, or, in a released
    // copy, next to the program.
    std::error_code path_error;
    const std::filesystem::path program_folder = std::filesystem::absolute(argv[0], path_error).parent_path();
    const std::filesystem::path build_folder = program_folder.parent_path().parent_path();
    std::filesystem::path scene_folder = build_folder.parent_path() / "tools" / "forest_scene";
    if (std::filesystem::is_regular_file(program_folder / "forest.level", path_error)) {
        scene_folder = program_folder;
    }
    ForestGame game(renderer, assets, build_folder, scene_folder / "assets.txt", scene_folder / "forest.level", input, actions, view,
                    options.positional.size() >= 3, settings, settings_path, options.headless);
    const auto has_word = [&options](const char* word) {
        for (size_t i = 4; i < options.positional.size(); ++i) {
            if (options.positional[i] == word) {
                return true;
            }
        }
        return false;
    };
    game.ShowCollision(has_word("collision"));
    game.ShowTestLights(has_word("lights"));
    game.UseTerrainOcclusion(!has_word("noocclusion"));
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

    // Exact, so that two runs (or two builds) can be compared to the last digit.
    const glm::dvec3 standing = game.GetCameraPosition();
    std::printf("camera at %.17g %.17g %.17g\n", standing.x, standing.y, standing.z);
    game.PrintCrates();
    game.PrintOcclusion();
    const poligon::FrameStats stats = renderer.GetFrameStats();
    std::printf("last frame: %d meshes submitted, %d drawn, %d shadow draws; processor per frame on average: %.2f ms "
                "preparing, %.2f ms waiting for the card\n",
                stats.meshes_submitted, stats.meshes_drawn, stats.shadow_draws,
                static_cast<double>(game.GetAverageCpuMilliseconds()), static_cast<double>(game.GetAverageWaitMilliseconds()));
    std::printf("physics step on average: %.3f ms\n", static_cast<double>(game.GetAveragePhysicsMilliseconds()));
    std::printf("instances: %d submitted, %d in view, %d in shadow cascades, %d draw calls\n", stats.instances_submitted,
                stats.instances_drawn, stats.shadow_instances, stats.instance_draw_calls);
    std::printf("memory: graphics card %.1f MB in textures and %.1f MB in buffers, process %.1f MB\n",
                static_cast<double>(stats.texture_bytes) / 1048576.0, static_cast<double>(stats.buffer_bytes) / 1048576.0,
                static_cast<double>(poligon::GetProcessMemoryBytes()) / 1048576.0);
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
