#include "poligon/render/render_settings.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace poligon {

void VisitRenderSettings(RenderSettings& settings, RenderSettingsVisitor& visitor) {
    DirectionalLight& sun = settings.sun;
    Environment& environment = settings.environment;
    PostSettings& post = settings.post;

    visitor.Section("sun");
    visitor.Direction("sun.direction", sun.direction);
    visitor.ColorValue("sun.color", sun.color);
    visitor.ColorValue("sun.ambient", sun.ambient);

    visitor.Section("sky");
    visitor.Bool("sky.draw", environment.draw_sky);
    visitor.ColorValue("sky.zenith", environment.sky_zenith);
    visitor.ColorValue("sky.horizon", environment.sky_horizon);
    visitor.ColorValue("sky.ground", environment.sky_ground);
    visitor.Bool("sky.light", environment.sky_light);
    visitor.Float("sky.light_intensity", environment.sky_light_intensity, 0.0f, 4.0f);

    visitor.Section("shadows");
    visitor.Bool("shadows.on", environment.shadows);
    visitor.Float("shadows.distance", environment.shadow_distance, 5.0f, 300.0f);
    visitor.Bool("shadows.show_cascades", environment.show_shadow_cascades);
    visitor.Float("shadows.sun_size", environment.sun_angular_size, 0.0f, 10.0f);

    visitor.Section("fog");
    visitor.Float("fog.density", environment.fog_density, 0.0f, 0.1f);
    visitor.ColorValue("fog.color", environment.fog_color);
    visitor.Bool("fog.volumetric", environment.volumetric_fog);
    visitor.Float("fog.height_falloff", environment.fog_height_falloff, 0.0f, 0.5f);
    visitor.Float("fog.base_height", environment.fog_base_height, -20.0f, 50.0f);
    visitor.Float("fog.shadow_brightness", environment.fog_shadow_brightness, 0.0f, 1.0f);
    visitor.Float("fog.sun_glow", environment.fog_sun_glow, 0.0f, 5.0f);
    visitor.Float("fog.sky_color", environment.fog_sky_color, 0.0f, 1.0f);

    visitor.Section("clouds and haze");
    visitor.Bool("clouds.on", environment.clouds);
    visitor.Float("clouds.coverage", environment.cloud_coverage, 0.0f, 1.0f);
    visitor.Float("clouds.bottom", environment.cloud_bottom, 200.0f, 6000.0f);
    visitor.Float("clouds.top", environment.cloud_top, 300.0f, 9000.0f);
    visitor.Float("clouds.density", environment.cloud_density, 0.001f, 0.2f);
    visitor.Float("clouds.size", environment.cloud_size, 1000.0f, 40000.0f);
    visitor.Float("clouds.speed", environment.cloud_speed, 0.0f, 60.0f);
    visitor.Float("clouds.variation", environment.cloud_variation, 0.0f, 1.0f);
    visitor.Bool("clouds.shadows", environment.cloud_shadows);
    visitor.Float("haze.density", environment.aerial_density, 0.0f, 0.002f);
    visitor.Float("foliage.translucency", environment.foliage_translucency, 0.0f, 2.0f);

    visitor.Section("wind");
    visitor.Float("wind.strength", environment.wind_strength, 0.0f, 3.0f);
    visitor.Float("wind.direction_x", environment.wind_direction.x, -1.0f, 1.0f);
    visitor.Float("wind.direction_z", environment.wind_direction.y, -1.0f, 1.0f);

    visitor.Section("ambient occlusion");
    visitor.Bool("ambient_occlusion.on", environment.ambient_occlusion);
    visitor.Float("ambient_occlusion.radius", environment.ambient_occlusion_radius, 0.02f, 2.0f);
    visitor.Float("ambient_occlusion.strength", environment.ambient_occlusion_strength, 0.0f, 3.0f);

    visitor.Section("exposure and bloom");
    static const char* const kToneMappings[] = {"none", "aces"};
    int tone_mapping = static_cast<int>(post.tone_mapping);
    visitor.Choice("post.tone_mapping", tone_mapping, kToneMappings, 2);
    post.tone_mapping = static_cast<ToneMapping>(tone_mapping);
    visitor.Float("post.exposure", post.exposure, 0.05f, 4.0f);
    visitor.Bool("post.auto_exposure", post.auto_exposure);
    visitor.Float("post.auto_exposure_target", post.auto_exposure_target, 0.02f, 1.0f);
    visitor.Float("post.auto_exposure_min", post.auto_exposure_min, 0.001f, 1.0f);
    visitor.Float("post.auto_exposure_max", post.auto_exposure_max, 1.0f, 64.0f);
    visitor.Float("post.auto_exposure_brighter_seconds", post.auto_exposure_brighter_seconds, 0.02f, 20.0f);
    visitor.Float("post.auto_exposure_darker_seconds", post.auto_exposure_darker_seconds, 0.02f, 20.0f);
    visitor.Bool("post.bloom", post.bloom);
    visitor.Float("post.bloom_strength", post.bloom_strength, 0.0f, 0.5f);

    visitor.Section("anti-aliasing and grading");
    visitor.Bool("post.temporal_aa", post.temporal_aa);
    visitor.Float("post.temporal_aa_blend", post.temporal_aa_blend, 0.02f, 1.0f);
    visitor.Float("post.sharpen", post.sharpen, 0.0f, 1.5f);
    visitor.Float("post.temperature", post.temperature, -1.0f, 1.0f);
    visitor.Float("post.tint", post.tint, -1.0f, 1.0f);
    visitor.Float("post.saturation", post.saturation, 0.0f, 2.0f);
    visitor.Float("post.contrast", post.contrast, 0.5f, 1.5f);
    visitor.Float("post.vignette", post.vignette, 0.0f, 1.0f);

    visitor.Section("quality");
    static const char* const kPresets[] = {"low", "medium", "high", "ultra"};
    GraphicsQuality& quality = settings.quality;
    int preset = static_cast<int>(quality.preset);
    visitor.Choice("quality.preset", preset, kPresets, 4);
    quality.preset = static_cast<GraphicsPreset>(preset);
    float shadow_map_size = static_cast<float>(quality.shadow_map_size);
    visitor.Float("quality.shadow_map_size", shadow_map_size, 512.0f, 4096.0f);
    quality.shadow_map_size = static_cast<int>(shadow_map_size);
    visitor.Float("quality.far_shadow_scale", quality.far_shadow_scale, 0.25f, 1.0f);
    visitor.Bool("quality.clouds", quality.clouds);
    float cloud_steps = static_cast<float>(quality.cloud_steps);
    visitor.Float("quality.cloud_steps", cloud_steps, 4.0f, 256.0f);
    quality.cloud_steps = static_cast<int>(cloud_steps);
    float cloud_light_steps = static_cast<float>(quality.cloud_light_steps);
    visitor.Float("quality.cloud_light_steps", cloud_light_steps, 1.0f, 16.0f);
    quality.cloud_light_steps = static_cast<int>(cloud_light_steps);
    visitor.Float("quality.cloud_resolution", quality.cloud_resolution, 0.125f, 1.0f);
    float water_steps = static_cast<float>(quality.water_reflection_steps);
    visitor.Float("quality.water_reflection_steps", water_steps, 0.0f, 128.0f);
    quality.water_reflection_steps = static_cast<int>(water_steps);
    float msaa_samples = static_cast<float>(quality.msaa_samples);
    visitor.Float("quality.msaa_samples", msaa_samples, 1.0f, 8.0f);
    quality.msaa_samples = static_cast<int>(msaa_samples);
    visitor.Float("quality.render_scale", quality.render_scale, 0.5f, 1.0f);
    visitor.Bool("quality.shadows", quality.shadows);
    visitor.Bool("quality.soft_shadows", quality.soft_shadows);
    visitor.Bool("quality.ambient_occlusion", quality.ambient_occlusion);
    visitor.Bool("quality.volumetric_fog", quality.volumetric_fog);
    visitor.Bool("quality.bloom", quality.bloom);
    visitor.Bool("quality.temporal_aa", quality.temporal_aa);
    visitor.Float("quality.shadow_distance_scale", quality.shadow_distance_scale, 0.2f, 3.0f);
    float ao_samples = static_cast<float>(quality.ambient_occlusion_samples);
    visitor.Float("quality.ambient_occlusion_samples", ao_samples, 4.0f, 64.0f);
    quality.ambient_occlusion_samples = static_cast<int>(ao_samples);
    float fog_steps = static_cast<float>(quality.fog_steps);
    visitor.Float("quality.fog_steps", fog_steps, 4.0f, 128.0f);
    quality.fog_steps = static_cast<int>(fog_steps);
    visitor.Float("quality.vegetation_detail", quality.vegetation_detail, 0.2f, 3.0f);
    visitor.Float("quality.mesh_detail", quality.mesh_detail, 0.2f, 3.0f);
    visitor.Float("quality.vegetation_shadow", quality.vegetation_shadow, 0.0f, 3.0f);
    visitor.Float("quality.ground_cover_density", quality.ground_cover_density, 0.0f, 3.0f);
    visitor.Float("quality.ground_cover_radius", quality.ground_cover_radius, 0.2f, 2.0f);
    visitor.Float("quality.terrain_detail", quality.terrain_detail, 0.2f, 3.0f);

    visitor.Section("debug view");
    static const char* const kDebugViews[] = {"none", "ambient_occlusion", "fog", "bloom", "cloud_shadow", "motion"};
    int debug_view = static_cast<int>(post.debug_view);
    visitor.Choice("post.debug_view", debug_view, kDebugViews, 6);
    post.debug_view = static_cast<DebugView>(debug_view);
}

namespace {

class Writer : public RenderSettingsVisitor {
public:
    explicit Writer(std::FILE* file) : file_(file) {}

    void Section(const char* name) override { std::fprintf(file_, "\n# %s\n", name); }
    void Bool(const char* name, bool& value) override { std::fprintf(file_, "%s %d\n", name, value ? 1 : 0); }
    // Nine significant digits tell any two different float values apart.
    void Float(const char* name, float& value, float, float) override {
        std::fprintf(file_, "%s %.9g\n", name, static_cast<double>(value));
    }
    void ColorValue(const char* name, Color& value) override {
        std::fprintf(file_, "%s %.9g %.9g %.9g\n", name, static_cast<double>(value.r), static_cast<double>(value.g),
                     static_cast<double>(value.b));
    }
    void Direction(const char* name, glm::vec3& value) override {
        std::fprintf(file_, "%s %.9g %.9g %.9g\n", name, static_cast<double>(value.x), static_cast<double>(value.y),
                     static_cast<double>(value.z));
    }
    void Choice(const char* name, int& value, const char* const* options, int count) override {
        std::fprintf(file_, "%s %s\n", name, options[value >= 0 && value < count ? value : 0]);
    }

private:
    std::FILE* file_;
};

// Takes values from the lines of a file, keyed by setting name, and reports what is wrong.
class Reader : public RenderSettingsVisitor {
public:
    explicit Reader(std::map<std::string, std::vector<std::string>>& lines) : lines_(lines) {}

    void Section(const char*) override {}
    void Bool(const char* name, bool& value) override {
        float number = 0.0f;
        if (TakeNumbers(name, &number, 1)) {
            value = number != 0.0f;
        }
    }
    void Float(const char* name, float& value, float, float) override { TakeNumbers(name, &value, 1); }
    void ColorValue(const char* name, Color& value) override {
        float numbers[3];
        if (TakeNumbers(name, numbers, 3)) {
            value.r = numbers[0];
            value.g = numbers[1];
            value.b = numbers[2];
        }
    }
    void Direction(const char* name, glm::vec3& value) override {
        float numbers[3];
        if (TakeNumbers(name, numbers, 3)) {
            value = {numbers[0], numbers[1], numbers[2]};
        }
    }
    void Choice(const char* name, int& value, const char* const* options, int count) override {
        const auto found = lines_.find(name);
        if (found == lines_.end()) {
            return;
        }
        const std::vector<std::string> words = found->second;
        lines_.erase(found);
        for (int i = 0; words.size() == 1 && i < count; ++i) {
            if (words[0] == options[i]) {
                value = i;
                return;
            }
        }
        AddError(std::string(name) + ": unknown value");
    }

    const std::string& GetError() const { return error_; }
    void AddError(const std::string& text) { error_ += (error_.empty() ? "" : "; ") + text; }

private:
    bool TakeNumbers(const char* name, float* numbers, size_t count) {
        const auto found = lines_.find(name);
        if (found == lines_.end()) {
            return false;
        }
        const std::vector<std::string> words = found->second;
        lines_.erase(found);
        if (words.size() != count) {
            AddError(std::string(name) + ": expected " + std::to_string(count) + " value(s)");
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            char* end = nullptr;
            numbers[i] = std::strtof(words[i].c_str(), &end);
            if (end == words[i].c_str() || *end != '\0') {
                AddError(std::string(name) + ": \"" + words[i] + "\" is not a number");
                return false;
            }
        }
        return true;
    }

    std::map<std::string, std::vector<std::string>>& lines_;
    std::string error_;
};

} // namespace

bool SaveRenderSettings(const char* path, const RenderSettings& settings) {
    std::FILE* file = nullptr;
    // "wb": the same bytes on every system, with plain \n line ends.
    if (fopen_s(&file, path, "wb") != 0 || file == nullptr) {
        return false;
    }
    std::fprintf(file, "poligon-render-settings 1\n");
    Writer writer(file);
    RenderSettings copy = settings;
    VisitRenderSettings(copy, writer);
    return std::fclose(file) == 0;
}

bool LoadRenderSettings(const char* path, RenderSettings& settings, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = std::string("cannot open render settings file ") + path;
        return false;
    }
    std::string line;
    if (!std::getline(file, line) || line.rfind("poligon-render-settings 1", 0) != 0) {
        error = std::string(path) + ": not a render settings file (first line must be \"poligon-render-settings 1\")";
        return false;
    }

    std::map<std::string, std::vector<std::string>> lines;
    while (std::getline(file, line)) {
        std::istringstream words(line);
        std::string name;
        if (!(words >> name) || name[0] == '#') {
            continue;
        }
        std::vector<std::string>& values = lines[name];
        values.clear();
        for (std::string word; words >> word;) {
            values.push_back(word);
        }
    }

    Reader reader(lines);
    RenderSettings loaded = settings;
    // A preset sets every quality value; single quality lines then override it.
    const auto preset_line = lines.find("quality.preset");
    if (preset_line != lines.end() && preset_line->second.size() == 1) {
        GraphicsPreset preset = GraphicsPreset::High;
        if (ParseGraphicsPreset(preset_line->second[0].c_str(), preset)) {
            loaded.quality = GetGraphicsPreset(preset);
        }
    }
    VisitRenderSettings(loaded, reader);
    // Whatever no setting asked for is a misspelled or outdated name.
    for (const auto& [name, values] : lines) {
        reader.AddError("unknown setting \"" + name + "\"");
    }
    if (!reader.GetError().empty()) {
        error = std::string(path) + ": " + reader.GetError();
        return false;
    }
    settings = loaded;
    return true;
}

} // namespace poligon
