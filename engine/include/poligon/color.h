#pragma once

namespace poligon {

// Color components. Material and light colors are linear and may exceed 1 for lights;
// RendererConfig::clear_color is given as it should look on screen.
struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

} // namespace poligon
