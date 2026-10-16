// Example: debug windows, a render settings file and the free camera.
//
// Started without arguments it opens a window with a cube and three debug windows; drag
// the sliders, fly with I/J/K/L and U/O, close the window or press Escape to quit.
// Usage for a check without a window: example_debug_ui --headless SETTINGS.txt OUTPUT.png
#include <cstdio>
#include <string>

#include "poligon/core/command_line.h"
#include "poligon/debug/debug_ui.h"
#include "poligon/debug/free_camera.h"
#include "poligon/core/platform.h"
#include "poligon/render/render_settings.h"
#include "poligon/render/renderer.h"

int main(int argc, char** argv) {
    poligon::CommandLineOptions options;
    std::string error;
    if (!poligon::ParseCommandLine(argc, argv, options, error) ||
        (options.headless && options.positional.size() != 2)) {
        std::fprintf(stderr, "usage: example_debug_ui [--headless SETTINGS.txt OUTPUT.png]\n");
        return 2;
    }

    // Everything about the look in one struct: sun, environment, post-processing.
    poligon::RenderSettings settings;
    settings.environment.draw_sky = true;
    settings.environment.sky_light = true;
    settings.environment.ambient_occlusion = true;
    settings.post.exposure = 0.7f;

    // The settings can be written to a text file and read back; this is how a look found
    // with the sliders is handed to a headless run (the standard option --render-settings).
    const char* settings_path = options.headless ? options.positional[0].c_str() : "example_render_settings.txt";
    if (options.headless) {
        settings.environment.fog_density = 0.02f;
        if (!poligon::SaveRenderSettings(settings_path, settings)) {
            std::fprintf(stderr, "cannot write %s\n", settings_path);
            return 1;
        }
        poligon::RenderSettings loaded;
        if (!poligon::LoadRenderSettings(settings_path, loaded, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        std::printf("saved and loaded %s: fog.density is %g\n", settings_path,
                    static_cast<double>(loaded.environment.fog_density));
        settings = loaded;
    }

    poligon::RendererConfig config;
    config.width = 960;
    config.height = 540;
    config.headless = options.headless;
    config.post = settings.post;
    config.debug_ui = true;  // ignored in headless mode
    poligon::Renderer renderer(config);
    std::printf("debug windows are %s\n", renderer.IsDebugUiActive() ? "on" : "off (headless)");

    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    // A camera that holds a position and two angles and can be moved and turned.
    poligon::FreeCamera camera;
    camera.position = {3.0f, 2.0f, 4.0f};
    camera.Turn(35.0f, -15.0f);

    int clicks = 0;
    const glm::mat4 identity(1.0f);
    bool keep_running = true;
    while (keep_running) {
        keep_running = poligon::ProcessPlatformEvents() && !options.headless;

        renderer.SetPostSettings(settings.post);
        renderer.BeginFrame(camera.ToCamera(), settings.sun, settings.environment);
        renderer.DrawMesh(plane, glm::scale(identity, {20.0f, 1.0f, 20.0f}), poligon::Color{0.4f, 0.5f, 0.3f, 1.0f});
        renderer.DrawMesh(cube, glm::translate(identity, {0.0f, 0.5f, 0.0f}), poligon::Color{0.8f, 0.3f, 0.2f, 1.0f});

        // Ready-made windows. Each does nothing when there are no debug windows.
        poligon::ShowRenderSettingsWindow(renderer, settings, settings_path);
        poligon::ShowFrameStatsWindow(renderer);
        poligon::FlyFreeCamera(renderer, camera);

        // A window of your own: plain Dear ImGui calls, guarded by IsDebugUiActive.
        if (renderer.IsDebugUiActive()) {
            ImGui::Begin("My window");
            ImGui::Text("camera at %.1f %.1f %.1f", static_cast<double>(camera.position.x),
                        static_cast<double>(camera.position.y), static_cast<double>(camera.position.z));
            if (ImGui::Button("Click me")) {
                ++clicks;
            }
            ImGui::SameLine();
            ImGui::Text("clicked %d times", clicks);
            ImGui::End();
        }
        renderer.EndFrame();
    }

    if (options.headless && !renderer.SaveFrameToPng(options.positional[1].c_str())) {
        std::fprintf(stderr, "cannot write %s\n", options.positional[1].c_str());
        return 1;
    }
    return 0;
}
