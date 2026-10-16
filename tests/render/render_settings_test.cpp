// Checks the render settings file (save, load, error reports) and the free camera's math.
// Usage: render_settings_test OUTPUT_DIRECTORY
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "poligon/core/command_line.h"
#include "poligon/debug/free_camera.h"
#include "poligon/render/graphics_quality.h"
#include "poligon/render/render_settings.h"
#include "test_support.h"

namespace {

bool Near(float a, float b) {
    return std::fabs(a - b) < 1.0e-4f;
}

bool Near(glm::vec3 a, glm::vec3 b) {
    return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z);
}

void WriteText(const std::string& path, const char* text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;
    if (argc != 2) {
        std::printf("usage: render_settings_test OUTPUT_DIRECTORY\n");
        return 2;
    }
    const std::string directory = argv[1];
    std::string error;

    // --- Save and load give back exactly the same values ---
    {
        poligon::RenderSettings saved;
        saved.sun.direction = {-0.123456789f, -0.5f, 0.25f};
        saved.sun.color = {3.4f, 2.9f, 2.2f, 1.0f};
        saved.environment.draw_sky = true;
        saved.environment.fog_density = 0.0123456f;
        saved.environment.volumetric_fog = true;
        saved.environment.ambient_occlusion_radius = 0.37f;
        saved.environment.show_shadow_cascades = true;
        saved.post.tone_mapping = poligon::ToneMapping::None;
        saved.post.exposure = 0.61f;
        saved.post.bloom = true;
        saved.post.debug_view = poligon::DebugView::Fog;
        const std::string path = directory + "/render_settings_test.txt";
        Expect(poligon::SaveRenderSettings(path.c_str(), saved), "settings are saved");

        poligon::RenderSettings loaded;
        Expect(poligon::LoadRenderSettings(path.c_str(), loaded, error), "saved settings load");
        Expect(loaded.sun.direction == saved.sun.direction && loaded.sun.color.g == saved.sun.color.g,
               "sun direction and color come back bit for bit");
        Expect(loaded.environment.draw_sky && loaded.environment.volumetric_fog && loaded.environment.show_shadow_cascades,
               "switches come back");
        Expect(loaded.environment.fog_density == saved.environment.fog_density &&
                   loaded.environment.ambient_occlusion_radius == saved.environment.ambient_occlusion_radius &&
                   loaded.post.exposure == saved.post.exposure,
               "numbers come back bit for bit");
        Expect(loaded.post.tone_mapping == poligon::ToneMapping::None && loaded.post.bloom &&
                   loaded.post.debug_view == poligon::DebugView::Fog,
               "choices come back");

        // Saving what was loaded gives the same file: nothing is lost on the way.
        const std::string second_path = directory + "/render_settings_test_2.txt";
        poligon::SaveRenderSettings(second_path.c_str(), loaded);
        std::ifstream first(path, std::ios::binary);
        std::ifstream second(second_path, std::ios::binary);
        const std::string first_text((std::istreambuf_iterator<char>(first)), std::istreambuf_iterator<char>());
        const std::string second_text((std::istreambuf_iterator<char>(second)), std::istreambuf_iterator<char>());
        Expect(!first_text.empty() && first_text == second_text, "a loaded file saves to identical text");
    }

    // --- A short file written by hand changes only what it names ---
    {
        const std::string path = directory + "/render_settings_partial.txt";
        WriteText(path.c_str(), "poligon-render-settings 1\n# a comment\nfog.density 0.05\nsun.color 1 2 3\n");
        poligon::RenderSettings settings;
        settings.environment.shadow_distance = 77.0f;
        Expect(poligon::LoadRenderSettings(path.c_str(), settings, error), "a partial file loads");
        Expect(settings.environment.fog_density == 0.05f && settings.sun.color.b == 3.0f, "named settings are set");
        Expect(settings.environment.shadow_distance == 77.0f, "settings the file does not name keep their value");
    }

    // --- Mistakes are reported and leave the settings untouched ---
    {
        poligon::RenderSettings settings;
        settings.environment.fog_density = 0.5f;
        Expect(!poligon::LoadRenderSettings((directory + "/no_such_file.txt").c_str(), settings, error) && !error.empty(),
               "a missing file is an error");

        const std::string path = directory + "/render_settings_bad.txt";
        WriteText(path.c_str(), "something else\n");
        Expect(!poligon::LoadRenderSettings(path.c_str(), settings, error), "a file without the header is an error");

        WriteText(path.c_str(), "poligon-render-settings 1\nfog.density 0.1\nfog.densty 0.2\n");
        Expect(!poligon::LoadRenderSettings(path.c_str(), settings, error) && error.find("fog.densty") != std::string::npos,
               "a misspelled setting is named in the error");
        Expect(settings.environment.fog_density == 0.5f, "a file with an error changes nothing");

        WriteText(path.c_str(), "poligon-render-settings 1\nsun.color 1 2\n");
        Expect(!poligon::LoadRenderSettings(path.c_str(), settings, error), "too few values are an error");
        WriteText(path.c_str(), "poligon-render-settings 1\nfog.density thick\n");
        Expect(!poligon::LoadRenderSettings(path.c_str(), settings, error), "a word in place of a number is an error");
        WriteText(path.c_str(), "poligon-render-settings 1\npost.tone_mapping filmic\n");
        Expect(!poligon::LoadRenderSettings(path.c_str(), settings, error), "an unknown choice is an error");
    }

    // --- Graphics presets ---
    {
        // A file names a preset, then changes one of its values: the preset is applied first.
        const std::string path = directory + "/render_settings_quality.txt";
        WriteText(path.c_str(), "poligon-render-settings 1\nquality.fog_steps 40\nquality.preset low\n");
        poligon::RenderSettings settings;
        Expect(poligon::LoadRenderSettings(path.c_str(), settings, error), "a file with a preset loads");
        const poligon::GraphicsQuality low = poligon::GetGraphicsPreset(poligon::GraphicsPreset::Low);
        Expect(settings.quality.preset == poligon::GraphicsPreset::Low &&
                   settings.quality.shadow_map_size == low.shadow_map_size &&
                   settings.quality.render_scale == low.render_scale && !settings.quality.ambient_occlusion,
               "the preset's values are set");
        Expect(settings.quality.fog_steps == 40, "a value the file names wins over the preset, wherever it stands");

        // Saved and loaded again, the changed preset comes back as it was.
        const std::string saved_path = directory + "/render_settings_quality_saved.txt";
        Expect(poligon::SaveRenderSettings(saved_path.c_str(), settings), "settings with a preset are saved");
        poligon::RenderSettings loaded;
        Expect(poligon::LoadRenderSettings(saved_path.c_str(), loaded, error) &&
                   loaded.quality.preset == poligon::GraphicsPreset::Low && loaded.quality.fog_steps == 40 &&
                   loaded.quality.far_shadow_scale == low.far_shadow_scale,
               "a saved preset with a change loads back the same");

        WriteText(path.c_str(), "poligon-render-settings 1\nquality.preset extreme\n");
        Expect(!poligon::LoadRenderSettings(path.c_str(), settings, error), "an unknown preset is an error");

        // The presets get cheaper from ultra down, and apply to a frame and a renderer.
        const poligon::GraphicsQuality medium = poligon::GetGraphicsPreset(poligon::GraphicsPreset::Medium);
        const poligon::GraphicsQuality high = poligon::GetGraphicsPreset(poligon::GraphicsPreset::High);
        const poligon::GraphicsQuality ultra = poligon::GetGraphicsPreset(poligon::GraphicsPreset::Ultra);
        Expect(low.shadow_map_size <= medium.shadow_map_size && medium.shadow_map_size <= high.shadow_map_size &&
                   high.shadow_map_size <= ultra.shadow_map_size && low.render_scale <= medium.render_scale &&
                   medium.render_scale <= high.render_scale && high.render_scale <= ultra.render_scale,
               "presets grow from low to ultra");
        poligon::Environment environment;
        environment.ambient_occlusion = true;
        environment.shadow_distance = 100.0f;
        poligon::PostSettings post;
        post.temporal_aa = true;
        poligon::ApplyGraphicsQuality(low, environment, post);
        Expect(!environment.ambient_occlusion && Near(environment.shadow_distance, 100.0f * low.shadow_distance_scale) &&
                   environment.fog_steps == low.fog_steps && post.temporal_aa,
               "a preset limits a frame's effects and scales its shadow range");
        poligon::RendererConfig config;
        poligon::ApplyGraphicsQuality(high, config);
        Expect(config.shadow_map_size == high.shadow_map_size && config.msaa_samples == 1 &&
                   config.render_scale == high.render_scale && config.far_shadow_scale == high.far_shadow_scale,
               "with TAA a preset draws without multisampling, at its render scale");
        poligon::GraphicsQuality no_taa = high;
        no_taa.temporal_aa = false;
        poligon::ApplyGraphicsQuality(no_taa, config);
        Expect(config.msaa_samples == high.msaa_samples && config.render_scale == 1.0f,
               "without TAA a preset uses multisampling at full size");
    }

    // --- Free camera ---
    {
        poligon::FreeCamera camera;
        camera.position = {0.0f, 0.0f, 0.0f};
        Expect(Near(camera.GetForward(), {0.0f, 0.0f, -1.0f}), "a fresh camera looks along -Z");
        camera.Move(2.0f, 1.0f, 0.5f);
        Expect(Near(camera.position, {1.0f, 0.5f, -2.0f}), "forward is -Z, right is +X, up is +Y");

        camera.Turn(90.0f, 0.0f);
        Expect(Near(camera.GetForward(), {-1.0f, 0.0f, 0.0f}), "turning by 90 degrees looks along -X");
        camera.Turn(0.0f, 200.0f);
        Expect(Near(camera.pitch_degrees, 89.0f), "looking up stops just short of straight up");

        // A camera taken over from the renderer's kind and converted back looks the same way.
        poligon::Camera source;
        source.position = {3.0f, 4.0f, 5.0f};
        source.target = {0.0f, 1.0f, -2.0f};
        source.far_plane = 250.0f;
        camera.SetFromCamera(source);
        const poligon::Camera result = camera.ToCamera(source);
        const glm::vec3 expected = glm::normalize(source.target - source.position);
        Expect(Near(result.position, source.position) && Near(glm::normalize(result.target - result.position), expected),
               "SetFromCamera and ToCamera keep position and direction");
        Expect(result.far_plane == 250.0f, "ToCamera keeps the clip planes of the base camera");
    }

    std::printf("render_settings_test finished\n");
    // --quality takes exactly the names of the graphics presets (the command line checks them
    // on its own, without the renderer).
    for (const char* name : {"low", "medium", "high", "ultra", "extreme"}) {
        const char* arguments[] = {"program", "--quality", name};
        poligon::CommandLineOptions options;
        std::string command_error;
        poligon::GraphicsPreset preset = poligon::GraphicsPreset::High;
        Expect(poligon::ParseCommandLine(3, arguments, options, command_error) == poligon::ParseGraphicsPreset(name, preset),
               "--quality and the graphics presets know the same names");
    }

    return test::ExitCode();
}
