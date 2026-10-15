#include "poligon/debug_ui.h"

#include <cstring>
#include <string>
#include <vector>

#include "poligon/state_dump.h"

namespace poligon {

namespace {

// The text after "name." in a setting name, used as the label: "fog.density" -> "density".
const char* ShortLabel(const char* name) {
    const char* dot = std::strchr(name, '.');
    return dot != nullptr ? dot + 1 : name;
}

class SettingsWidgets : public RenderSettingsVisitor {
public:
    void Section(const char* name) override {
        section_open_ = ImGui::CollapsingHeader(name, ImGuiTreeNodeFlags_DefaultOpen);
        // Labels repeat between sections ("on", "color"); the section makes them unique.
        if (pushed_id_) {
            ImGui::PopID();
        }
        ImGui::PushID(name);
        pushed_id_ = true;
    }
    void Bool(const char* name, bool& value) override {
        if (section_open_) {
            changed_ |= ImGui::Checkbox(ShortLabel(name), &value);
        }
    }
    void Float(const char* name, float& value, float low, float high) override {
        if (section_open_) {
            // Small ranges (densities) need more digits than large ones (distances).
            const char* format = high <= 0.5f ? "%.4f" : high <= 10.0f ? "%.3f" : "%.1f";
            changed_ |= ImGui::SliderFloat(ShortLabel(name), &value, low, high, format);
        }
    }
    void ColorValue(const char* name, Color& value) override {
        if (section_open_) {
            // Light colors may exceed 1, hence the plain number fields instead of 0..255.
            changed_ |= ImGui::ColorEdit3(ShortLabel(name), &value.r, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        }
    }
    void Direction(const char* name, glm::vec3& value) override {
        if (section_open_) {
            changed_ |= ImGui::SliderFloat3(ShortLabel(name), &value.x, -1.0f, 1.0f, "%.2f");
        }
    }
    void Choice(const char* name, int& value, const char* const* options, int count) override {
        if (section_open_) {
            changed_ |= ImGui::Combo(ShortLabel(name), &value, options, count);
        }
    }

    bool Finish() {
        if (pushed_id_) {
            ImGui::PopID();
        }
        return changed_;
    }

private:
    bool section_open_ = true;
    bool pushed_id_ = false;
    bool changed_ = false;
};

} // namespace

bool ShowRenderSettingsWindow(const Renderer& renderer, RenderSettings& settings, const char* file_path) {
    if (!renderer.IsDebugUiActive()) {
        return false;
    }
    bool changed = false;
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Render settings")) {
        if (file_path != nullptr) {
            // Shown until the next click, so the result of a click can be read.
            static std::string message;
            if (ImGui::Button("Save")) {
                message = SaveRenderSettings(file_path, settings) ? std::string("saved to ") + file_path
                                                                  : std::string("cannot write ") + file_path;
            }
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                std::string error;
                if (LoadRenderSettings(file_path, settings, error)) {
                    message = std::string("loaded ") + file_path;
                    changed = true;
                } else {
                    message = error;
                }
            }
            if (!message.empty()) {
                ImGui::TextWrapped("%s", message.c_str());
            }
        }
        ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.55f);
        SettingsWidgets widgets;
        VisitRenderSettings(settings, widgets);
        changed |= widgets.Finish();
        ImGui::PopItemWidth();
    }
    ImGui::End();
    return changed;
}

void ShowFrameStatsWindow(const Renderer& renderer) {
    if (!renderer.IsDebugUiActive()) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10.0f, 10.0f), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    if (ImGui::Begin("Frame", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Dear ImGui averages the frame rate over the last 60 frames.
        const float frames_per_second = io.Framerate;
        ImGui::Text("%.1f frames per second", static_cast<double>(frames_per_second));
        ImGui::Text("%.2f ms per frame", frames_per_second > 0.0f ? 1000.0 / static_cast<double>(frames_per_second) : 0.0);
        const FrameStats stats = renderer.GetFrameStats();
        ImGui::Text("processor: %.2f ms preparing, %.2f ms waiting for the card", static_cast<double>(stats.cpu_milliseconds),
                    static_cast<double>(stats.wait_milliseconds));
        ImGui::Text("meshes: %d drawn of %d", stats.meshes_drawn, stats.meshes_submitted);
        ImGui::Text("shadow draws: %d", stats.shadow_draws);
        ImGui::Text("%s, %d x %d, %d samples", renderer.GetBackendName(), renderer.GetWidth(), renderer.GetHeight(),
                    renderer.GetMsaaSamples());
#ifndef NDEBUG
        // A Debug build of the engine and its libraries runs several times slower; say so,
        // because from the outside the two programs look the same.
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "DEBUG BUILD: several times slower.");
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "For speed run the one in build/release/bin.");
#endif
    }
    ImGui::End();
}

void FlyFreeCamera(const Renderer& renderer, FreeCamera& camera, float meters_per_second) {
    if (!renderer.IsDebugUiActive()) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }
    const auto axis = [](ImGuiKey positive, ImGuiKey negative) {
        return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
    };
    const float distance = meters_per_second * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 4.0f : 1.0f) * io.DeltaTime;
    camera.Move(axis(ImGuiKey_I, ImGuiKey_K) * distance, axis(ImGuiKey_L, ImGuiKey_J) * distance,
                axis(ImGuiKey_O, ImGuiKey_U) * distance);
    if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        camera.Turn(-io.MouseDelta.x * 0.2f, -io.MouseDelta.y * 0.2f);
    }
}

void ShowWorldWindow(const Renderer& renderer, const flecs::world& world) {
    if (!renderer.IsDebugUiActive()) {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(10.0f, 540.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 170.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("World")) {
        static char filter[64] = "";
        ImGui::InputText("filter by name", filter, sizeof(filter));

        // The window shows the state dump itself, cut into entities. This relies on the
        // layout DumpWorldStateToJson writes: one line per field, entities indented by four
        // spaces, their fields by six, their components by eight.
        const std::string dump = DumpWorldStateToJson(world);
        struct Entity {
            std::string id;
            std::string name;
            std::vector<std::string> components;
        };
        std::vector<Entity> entities;
        std::vector<std::string> singletons;
        bool in_singletons = false;
        size_t start = 0;
        while (start < dump.size()) {
            size_t end = dump.find('\n', start);
            if (end == std::string::npos) {
                end = dump.size();
            }
            std::string line = dump.substr(start, end - start);
            start = end + 1;
            if (!line.empty() && line.back() == ',') {
                line.pop_back();
            }
            if (line == "    {") {
                entities.emplace_back();
            } else if (line.rfind("  \"singletons\"", 0) == 0) {
                in_singletons = true;
            } else if (line.rfind("  \"unreflected_components\"", 0) == 0) {
                in_singletons = false;
            } else if (in_singletons && line.rfind("    \"", 0) == 0) {
                singletons.push_back(line.substr(4));
            } else if (!entities.empty() && line.rfind("      \"id\": ", 0) == 0) {
                entities.back().id = line.substr(12);
            } else if (!entities.empty() && line.rfind("      \"name\": ", 0) == 0) {
                entities.back().name = line.substr(14);
            } else if (!entities.empty() && line.rfind("        \"", 0) == 0) {
                entities.back().components.push_back(line.substr(8));
            }
        }

        ImGui::Text("%d entities", static_cast<int>(entities.size()));
        ImGui::BeginChild("entities");
        if (!singletons.empty() && ImGui::TreeNode("singletons")) {
            for (const std::string& singleton : singletons) {
                ImGui::TextWrapped("%s", singleton.c_str());
            }
            ImGui::TreePop();
        }
        for (const Entity& entity : entities) {
            if (filter[0] != '\0' && entity.name.find(filter) == std::string::npos) {
                continue;
            }
            const std::string label = entity.id + " " + (entity.name.empty() ? "(no name)" : entity.name);
            // The id keeps the node open when other entities appear or disappear around it.
            if (ImGui::TreeNode(entity.id.c_str(), "%s", label.c_str())) {
                for (const std::string& component : entity.components) {
                    ImGui::TextWrapped("%s", component.c_str());
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace poligon
