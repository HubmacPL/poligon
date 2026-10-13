// Toy game: boxes that fall, bounce and expire; the player steers the big red one.
// It exists to exercise the engine.
#include <cmath>
#include <cstdio>
#include <string>

#include "poligon/command_line.h"
#include "poligon/crash_handler.h"
#include "poligon/ecs.h"
#include "poligon/game_loop.h"
#include "poligon/input.h"
#include "poligon/platform.h"
#include "poligon/renderer.h"
#include "poligon/state_dump.h"
#include "poligon/version.h"
#include "toy_world.h"

namespace {

class ToyGame : public poligon::Game {
public:
    ToyGame(flecs::world& world, poligon::Renderer& renderer, const poligon::Input& input,
            const toy::ToyActions& actions, poligon::StateHashLog& hash_log)
        : world_(world),
          renderer_(renderer),
          input_(input),
          actions_(actions),
          hash_log_(hash_log),
          box_query_(world.query<const toy::Position, const toy::Box>()),
          ground_query_(world.query<const toy::Ground>()) {}

    void Start(poligon::GameLoop& /*loop*/) override {
        cube_mesh_ = renderer_.CreateCubeMesh();
        plane_mesh_ = renderer_.CreatePlaneMesh();
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

    void Render(poligon::GameLoop& /*loop*/, const poligon::FrameInfo& /*frame*/) override {
        // The camera circles the scene; CameraRig holds the angle.
        const float yaw = poligon::DegreesToRadians(world_.get<toy::CameraRig>().yaw_degrees);
        const poligon::Vec3 start_offset = {9.0f, 6.0f, 13.0f};
        poligon::Camera camera;
        camera.position = {start_offset.x * std::cos(yaw) + start_offset.z * std::sin(yaw), start_offset.y,
                           -start_offset.x * std::sin(yaw) + start_offset.z * std::cos(yaw)};
        camera.target = {0.0f, 3.5f, 0.0f};
        const poligon::DirectionalLight light;

        renderer_.BeginFrame(camera, light);

        ground_query_.each([this](const toy::Ground& ground) {
            renderer_.DrawMesh(plane_mesh_, poligon::MakeScale({ground.size, 1.0f, ground.size}), ground.color);
        });

        box_query_.each([this](const toy::Position& position, const toy::Box& box) {
            // Order of operations, right to left: scale, rotate, then move. Position is the
            // bottom of the box, the cube mesh is centered, hence the half-height offset.
            const poligon::Vec3 center = {position.value.x, position.value.y + box.size.y * 0.5f, position.value.z};
            const poligon::Mat4 transform = poligon::Multiply(
                poligon::MakeTranslation(center),
                poligon::Multiply(poligon::MakeRotationY(poligon::DegreesToRadians(box.rotation_y_degrees)),
                                  poligon::MakeScale(box.size)));
            renderer_.DrawMesh(cube_mesh_, transform, box.color);
        });

        renderer_.EndFrame();
    }

private:
    flecs::world& world_;
    poligon::Renderer& renderer_;
    const poligon::Input& input_;
    toy::ToyActions actions_;
    poligon::StateHashLog& hash_log_;
    flecs::query<const toy::Position, const toy::Box> box_query_;
    flecs::query<const toy::Ground> ground_query_;
    poligon::MeshHandle cube_mesh_;
    poligon::MeshHandle plane_mesh_;
};

} // namespace

int main(int argc, char** argv) {
    poligon::InstallCrashHandler();

    poligon::CommandLineOptions options;
    std::string error;
    if (!poligon::ParseCommandLine(argc, argv, options, error)) {
        std::fprintf(stderr,
                     "toy: %s\n"
                     "usage: toy [--headless] [--ticks N] [--backend d3d12|vulkan] [--gpu-validation]\n"
                     "           [--record PATH] [--replay PATH]\n"
                     "           [--dump-frame PATH.png] [--dump-state PATH.json]\n"
                     "           [--hash-log PATH] [--hash-interval N]\n"
                     "keys: A/D/W/S or arrows move the red box, Space jumps,\n"
                     "      hold the right mouse button and move the mouse to turn the camera, Escape quits\n",
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
    renderer_config.clear_color = {0.53f, 0.73f, 0.90f, 1.0f};  // sky
    poligon::Renderer renderer(renderer_config);

    flecs::world world;
    toy::SetUpToyWorld(world);

    poligon::StateHashLog hash_log;
    if (!options.hash_log_path.empty() && !hash_log.Open(options.hash_log_path.c_str(), options.hash_interval)) {
        std::fprintf(stderr, "toy: cannot create %s\n", options.hash_log_path.c_str());
        return 2;
    }

    poligon::GameLoopResult result{};
    {
        // The game owns queries, which must be released before the world is destroyed.
        ToyGame game(world, renderer, input, actions, hash_log);
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
