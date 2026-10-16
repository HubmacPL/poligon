#pragma once

#include <string>

#include "poligon/render/graphics_quality.h"
#include "poligon/render/renderer.h"

namespace poligon {

// Everything that decides how a scene looks apart from the scene itself: the sun, the
// environment (sky, fog, shadows, ambient occlusion) and the post-processing.
//
// The settings window (poligon/debug/debug_ui.h) edits these with sliders; the file functions
// below write and read them, so a look found by hand can be reproduced exactly in a
// headless run (the standard option --render-settings FILE).
struct RenderSettings {
    DirectionalLight sun;
    Environment environment;
    PostSettings post;
    // How much the picture may cost (see poligon/render/graphics_quality.h). In a file the line
    // "quality.preset NAME" sets all quality values to the preset first; quality lines after
    // it change single values.
    GraphicsQuality quality;
};

// One call per setting, in a fixed order. The same list drives the file format and the
// settings window, so the two cannot drift apart. `name` is the key in the file and the
// label in the window.
class RenderSettingsVisitor {
public:
    virtual ~RenderSettingsVisitor() = default;
    // Starts a group of settings ("sun", "fog", ...).
    virtual void Section(const char* name) = 0;
    virtual void Bool(const char* name, bool& value) = 0;
    // `low` and `high` are the slider range; values outside it are still valid in a file.
    virtual void Float(const char* name, float& value, float low, float high) = 0;
    virtual void ColorValue(const char* name, Color& value) = 0;
    virtual void Direction(const char* name, glm::vec3& value) = 0;
    // A choice among `count` named options, stored as the option's index.
    virtual void Choice(const char* name, int& value, const char* const* options, int count) = 0;
};

void VisitRenderSettings(RenderSettings& settings, RenderSettingsVisitor& visitor);

// Text file, one setting per line: "name value" (colors and directions have three values).
// The first line is "poligon-render-settings 1".
bool SaveRenderSettings(const char* path, const RenderSettings& settings);

// Settings missing from the file keep the value they have in `settings`. Returns false and
// fills `error` if the file cannot be read, has the wrong header, or holds a line that is
// not a known setting with the right number of values.
bool LoadRenderSettings(const char* path, RenderSettings& settings, std::string& error);

} // namespace poligon
