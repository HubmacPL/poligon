// Toy game: boxes that fall, bounce and expire; the player steers the big red one.
// It exists to exercise the engine.
#include <cstdio>
#include <filesystem>
#include <string>

#include "poligon/core/command_line.h"
#include "poligon/core/crash_handler.h"
#include "poligon/debug/debug_ui.h"
#include "poligon/ecs/ecs.h"
#include "poligon/core/game_loop.h"
#include "poligon/core/input.h"
#include "poligon/core/platform.h"
#include "poligon/render/renderer.h"
#include "poligon/ecs/state_dump.h"
#include "poligon/core/version.h"
#include "toy_world.h"

namespace {

// A light square with a darker frame and two diagonal planks, like the side of a wooden crate.
// It is asymmetric on purpose (one corner is marked), so a flipped or rotated texture is visible.
poligon::Image MakeCrateImage() {
    const int size = 64;
    poligon::Image image = poligon::MakeSolidImage(size, size, 235, 235, 235);
    const auto set_gray = [&](int x, int y, std::uint8_t value) {
        std::uint8_t* pixel = &image.pixels[static_cast<size_t>(y * size + x) * 4];
        pixel[0] = value;
        pixel[1] = value;
        pixel[2] = value;
    };
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool frame = x < 6 || y < 6 || x >= size - 6 || y >= size - 6;
            const bool diagonal = (x - y >= -3 && x - y <= 3) || (x + y >= size - 4 && x + y <= size + 2);
            const bool corner_mark = x >= 8 && x < 20 && y >= 8 && y < 14;  // near the top-left corner
            if (frame || corner_mark) {
                set_gray(x, y, 120);
            } else if (diagonal) {
                set_gray(x, y, 170);
            }
        }
    }
    return image;
}

class ToyGame : public poligon::Game {
public:
    ToyGame(flecs::world& world, poligon::Renderer& renderer, const poligon::Input& input,
            const toy::ToyActions& actions, poligon::StateHashLog& hash_log, const poligon::RenderSettings& settings,
            const std::string& settings_path)
        : world_(world),
          renderer_(renderer),
          input_(input),
          actions_(actions),
          hash_log_(hash_log),
          settings_(settings),
          settings_path_(settings_path),
          box_query_(world.query<const toy::Position, const toy::Rotation, const toy::Box>()),
          ground_query_(world.query<const toy::Ground, const toy::Position>()) {}

    void Start(poligon::GameLoop& /*loop*/) override {
        cube_mesh_ = renderer_.CreateCubeMesh();
        plane_mesh_ = renderer_.CreatePlaneMesh();

        // Both textures are generated here, so the toy needs no image files.
        const std::uint8_t light_gray[4] = {255, 255, 255, 255};
        const std::uint8_t dark_gray[4] = {190, 190, 190, 255};
        ground_texture_ = renderer_.CreateTexture(poligon::MakeCheckerImage(128, 128, 64, light_gray, dark_gray));
        box_texture_ = renderer_.CreateTexture(MakeCrateImage());
    }

    void BeginFrame(poligon::GameLoop& loop) override {
        if (!poligon::ProcessPlatformEvents()) {
            loop.RequestQuit();
        }
    }

    void FixedUpdate(poligon::GameLoop& /*loop*/, const poligon::TickInfo& tick) override {
        // The loop has already advanced Input to this tick.
        toy::UpdatePlayerInput(world_, input_, actions_);
        // Runs every system once with the fixed time step. flecs never reads the clock itself here.
        world_.progress(static_cast<float>(tick.delta_seconds));
        hash_log_.AfterTick(world_, tick.tick_index + 1);
    }

    void Render(poligon::GameLoop& /*loop*/, const poligon::FrameInfo& frame) override {
        // The camera circles the scene; CameraRig holds the angle.
        const float yaw = glm::radians(world_.get<toy::CameraRig>().yaw_degrees);
        const glm::mat4 identity(1.0f);
        const glm::vec3 up = {0.0f, 1.0f, 0.0f};
        poligon::Camera camera;
        camera.position = glm::vec3(glm::rotate(identity, yaw, up) * glm::vec4(9.0f, 6.0f, 13.0f, 1.0f));
        camera.target = {0.0f, 3.5f, 0.0f};
        // The free camera is a viewing aid only: it changes what is drawn, never the world.
        if (free_camera_on_) {
            camera = free_camera_.ToCamera(camera);
        }

        renderer_.SetPostSettings(settings_.post);
        poligon::Environment environment = settings_.environment;
        environment.frame_seconds = static_cast<float>(frame.delta_seconds);  // automatic exposure adapts over game time
        renderer_.BeginFrame(camera, settings_.sun, environment);
        ShowDebugWindows(camera);

        ground_query_.each([this](const toy::Ground& ground, const toy::Position& position) {
            poligon::Material material;
            material.color = ground.color;
            material.texture = ground_texture_;
            // The checker image holds 2x2 squares; repeat it so that one square is one meter.
            material.uv_scale = {ground.size * 0.5f, ground.size * 0.5f};
            renderer_.DrawMesh(plane_mesh_, poligon::Transform(position.value, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), {ground.size, 1.0f, ground.size}),
                               material);
        });

        box_query_.each([this](const toy::Position& position, const toy::Rotation& rotation, const toy::Box& box) {
            // Position is the center of the box, like the cube mesh's own center.
            poligon::Material material;
            material.color = box.color;
            material.texture = box_texture_;
            renderer_.DrawMesh(cube_mesh_, poligon::Transform(position.value, rotation.value, box.size), material);
        });

        renderer_.EndFrame();
    }

private:
    // Debug windows and keys. They take effect from the next frame on. Without a window
    // (headless runs, tests) nothing here does anything.
    void ShowDebugWindows(const poligon::Camera& camera) {
        if (!renderer_.IsDebugUiActive()) {
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
            show_windows_ = !show_windows_;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !ImGui::GetIO().WantTextInput) {
            free_camera_on_ = !free_camera_on_;
            if (free_camera_on_) {
                free_camera_.SetFromCamera(camera);
            }
        }
        if (free_camera_on_) {
            poligon::FlyFreeCamera(renderer_, free_camera_);
        }
        if (show_windows_) {
            poligon::ShowRenderSettingsWindow(renderer_, settings_, settings_path_.c_str());
            poligon::ShowFrameStatsWindow(renderer_);
            poligon::ShowWorldWindow(renderer_, world_);
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.0f, 130.0f), ImGuiCond_FirstUseEver,
                                    ImVec2(1.0f, 0.0f));
            if (ImGui::Begin("Keys", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Tab: hide or show these windows");
                ImGui::Text("F: free camera (%s)", free_camera_on_ ? "on" : "off");
                ImGui::TextUnformatted("  I/J/K/L move, U/O down and up, Shift faster");
                ImGui::TextUnformatted("  hold the middle mouse button to look");
            }
            ImGui::End();
        }
    }

    flecs::world& world_;
    poligon::Renderer& renderer_;
    const poligon::Input& input_;
    toy::ToyActions actions_;
    poligon::StateHashLog& hash_log_;
    flecs::query<const toy::Position, const toy::Rotation, const toy::Box> box_query_;
    flecs::query<const toy::Ground, const toy::Position> ground_query_;
    poligon::MeshHandle cube_mesh_;
    poligon::MeshHandle plane_mesh_;
    poligon::TextureHandle ground_texture_;
    poligon::TextureHandle box_texture_;
    poligon::RenderSettings settings_;
    std::string settings_path_;
    poligon::FreeCamera free_camera_;
    bool free_camera_on_ = false;
    bool show_windows_ = true;
};

// The look of the toy: a sunny day with a light haze.
poligon::RenderSettings MakeToySettings() {
    poligon::RenderSettings settings;
    settings.sun.direction = {-0.5f, -0.75f, -0.4f};
    settings.sun.color = {2.6f, 2.45f, 2.2f, 1.0f};
    poligon::Environment& environment = settings.environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    environment.sky_ground = {0.42f, 0.44f, 0.46f, 1.0f};
    environment.shadow_distance = 60.0f;
    environment.fog_density = 0.002f;
    environment.volumetric_fog = true;
    environment.fog_height_falloff = 0.02f;
    environment.ambient_occlusion = true;
    settings.post.bloom = true;
    settings.post.auto_exposure = true;
    return settings;
}

} // namespace

int main(int argc, char** argv) {
    poligon::InstallCrashHandler();

    poligon::CommandLineOptions options;
    std::string error;
    if (poligon::ParseCommandLine(argc, argv, options, error) && !options.positional.empty()) {
        error = "unexpected argument \"" + options.positional[0] + "\"";
    }
    if (!error.empty()) {
        std::fprintf(stderr,
                     "toy: %s\n"
                     "usage: toy [--headless] [--ticks N] [--backend d3d12|vulkan] [--gpu-validation]\n"
                     "           [--record PATH] [--replay PATH]\n"
                     "           [--dump-frame PATH.png] [--dump-state PATH.json]\n"
                     "           [--hash-log PATH] [--hash-interval N] [--render-settings PATH]\n"
                     "keys: A/D/W/S or arrows move the red box, Space jumps,\n"
                     "      hold the right mouse button and move the mouse to turn the camera, Escape quits,\n"
                     "      Tab hides the debug windows, F switches the free camera\n",
                     error.c_str());
        return 2;
    }

    poligon::GameLoopConfig loop_config;
    loop_config.headless = options.headless;
    loop_config.max_ticks = options.ticks;
    // With a window, presenting a frame waits for the display refresh and paces the loop.
    loop_config.render_every_iteration = !options.headless;

    // Input: a recording if one is given, otherwise the keyboard and mouse when there is a window.
    poligon::Input input;
    const toy::ToyActions actions = toy::SetUpToyInput(input);
    if (!options.replay_path.empty()) {
        if (!input.StartReplay(options.replay_path.c_str(), loop_config.ticks_per_second, error)) {
            std::fprintf(stderr, "toy: %s\n", error.c_str());
            return 2;
        }
        if (loop_config.max_ticks == 0) {
            loop_config.max_ticks = input.GetReplayTickCount();
        }
    } else if (!options.headless) {
        input.UseLiveDevices();
    }
    if (!options.record_path.empty() &&
        !input.StartRecording(options.record_path.c_str(), loop_config.ticks_per_second, error)) {
        std::fprintf(stderr, "toy: %s\n", error.c_str());
        return 2;
    }
    loop_config.input = &input;

    if (options.headless && loop_config.max_ticks == 0) {
        std::fprintf(stderr, "toy: --headless needs --ticks N or --replay PATH, because there is no window to close\n");
        return 2;
    }

    poligon::RendererConfig renderer_config;
    renderer_config.headless = options.headless;
    renderer_config.backend = options.backend;
    renderer_config.enable_validation = options.gpu_validation;
    renderer_config.window_title = "Poligon toy";
    renderer_config.clear_color = {0.53f, 0.73f, 0.90f, 1.0f};
    poligon::RenderSettings settings = MakeToySettings();
    // Where the settings window saves: next to the program, unless a file was given.
    std::string settings_path = (std::filesystem::path(argv[0]).parent_path() / "toy_render_settings.txt").string();
    if (!options.render_settings_path.empty()) {
        settings_path = options.render_settings_path;
        if (!poligon::LoadRenderSettings(settings_path.c_str(), settings, error)) {
            std::fprintf(stderr, "toy: %s\n", error.c_str());
            return 2;
        }
    }
    renderer_config.post = settings.post;
    renderer_config.debug_ui = true;
    poligon::Renderer renderer(renderer_config);

    // The flecs world uses the physics world in its systems, so physics is declared first
    // and thereby destroyed last.
    poligon::PhysicsWorld physics;
    flecs::world world;
    toy::SetUpToyWorld(world, physics);

    poligon::StateHashLog hash_log;
    if (!options.hash_log_path.empty() && !hash_log.Open(options.hash_log_path.c_str(), options.hash_interval)) {
        std::fprintf(stderr, "toy: cannot create %s\n", options.hash_log_path.c_str());
        return 2;
    }

    poligon::GameLoopResult result{};
    {
        // The game owns queries, which must be released before the world is destroyed.
        ToyGame game(world, renderer, input, actions, hash_log, settings, settings_path);
        poligon::GameLoop loop(loop_config);
        result = loop.Run(game);
    }
    if (!hash_log.Close(world, result.tick_count)) {
        std::fprintf(stderr, "toy: cannot write %s\n", options.hash_log_path.c_str());
        return 1;
    }

    std::printf("toy on Poligon %s, backend %s\n", poligon::GetEngineVersionString(), renderer.GetBackendName());
    if (options.headless) {
        std::printf("ticks=%lld frames=%lld\n", static_cast<long long>(result.tick_count),
                    static_cast<long long>(result.frame_count));
    } else {
        // The frame count of a real-time run depends on the machine, so it is not printed as a result.
        std::printf("ticks=%lld\n", static_cast<long long>(result.tick_count));
    }
    std::printf("state_hash=%016llx\n", static_cast<unsigned long long>(poligon::HashWorldState(world)));

    if (!options.record_path.empty()) {
        if (!input.FinishRecording()) {
            std::fprintf(stderr, "toy: cannot write %s\n", options.record_path.c_str());
            return 1;
        }
        std::printf("input recorded to %s\n", options.record_path.c_str());
    }

    if (!options.dump_state_path.empty()) {
        if (!poligon::SaveWorldStateToJson(world, options.dump_state_path.c_str())) {
            std::fprintf(stderr, "toy: cannot write %s\n", options.dump_state_path.c_str());
            return 1;
        }
        std::printf("state saved to %s\n", options.dump_state_path.c_str());
    }

    if (!options.dump_frame_path.empty()) {
        if (result.frame_count == 0) {
            std::fprintf(stderr, "toy: no frame was rendered, nothing to save\n");
            return 1;
        }
        if (!renderer.SaveFrameToPng(options.dump_frame_path.c_str())) {
            std::fprintf(stderr, "toy: cannot write %s\n", options.dump_frame_path.c_str());
            return 1;
        }
        std::printf("frame saved to %s\n", options.dump_frame_path.c_str());
    }
    return 0;
}
