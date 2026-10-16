#pragma once

namespace poligon {

// Which graphics API the renderer runs on (RendererConfig::backend, --backend).
enum class GraphicsBackend {
    Default,     // the engine's preferred backend
    Direct3D12,
    Vulkan,
};

} // namespace poligon
