#pragma once

#include <cstdint>
#include <memory>

#include "poligon/image.h"
#include "poligon/math.h"

namespace poligon {

// Which graphics API the renderer runs on.
enum class GraphicsBackend {
    Default,     // the engine's preferred backend
    Direct3D12,
    Vulkan,
};

// Color components in the 0..1 range. Object and light colors are linear;
// RendererConfig::clear_color is used as given (already display-encoded).
struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct RendererConfig {
    int width = 1280;
    int height = 720;
    // true: no window is created; frames are rendered to an off-screen image only.
    bool headless = false;
    GraphicsBackend backend = GraphicsBackend::Default;
    // Turns on the graphics API's debug layer when it is installed. Slower; for verification runs.
    bool enable_validation = false;
    const char* window_title = "Poligon";
    // Background color every frame starts with. Fixed for the lifetime of the renderer,
    // because Direct3D 12 wants to know the clear color when the image is created.
    Color clear_color{0.0f, 0.0f, 0.0f, 1.0f};
};

struct Camera {
    glm::vec3 position{0.0f, 2.0f, 5.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float vertical_fov_degrees = 60.0f;
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
};

struct DirectionalLight {
    // Direction the light travels (from the light towards the scene).
    glm::vec3 direction{-0.4f, -1.0f, -0.3f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    Color ambient{0.15f, 0.15f, 0.15f, 1.0f};
};

struct MeshVertex {
    glm::vec3 position;
    glm::vec3 normal;
    // Texture coordinates: (0, 0) is the top-left corner of the image, (1, 1) the bottom-right.
    glm::vec2 uv;
};

// Identifies a mesh created by Renderer. An id of 0 is never a valid mesh.
struct MeshHandle {
    std::uint32_t id = 0;
};

// Identifies a texture created by Renderer. An id of 0 means "no texture".
struct TextureHandle {
    std::uint32_t id = 0;
};

struct TextureOptions {
    // true for images that hold colors (almost all): the bytes are sRGB-encoded and are
    // converted to linear values when sampled. false for images that hold other data.
    bool is_color = true;
    // Mipmaps are smaller copies of the texture used when it is drawn small or far away;
    // without them distant textures shimmer.
    bool generate_mipmaps = true;
};

// How a mesh is drawn: its texture is multiplied by `color`. Without a texture the mesh has the plain color.
struct Material {
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    TextureHandle texture;
    // Multiplies the mesh's texture coordinates; (4, 4) repeats the texture four times in each direction.
    glm::vec2 uv_scale{1.0f, 1.0f};
};

// Draws lit meshes, plain or textured. Every frame is rendered to an off-screen image first;
// with a window that image is then shown, without one it can only be saved.
// The same pixels are produced in both cases.
//
// Frame order: BeginFrame, any number of DrawMesh, EndFrame. CaptureFrame and SaveFrameToPng
// may be called after EndFrame and return the frame that was just finished.
// Meshes and textures live until the Renderer is destroyed.
// Unrecoverable problems (no usable graphics device, calls in the wrong order) end the
// process through FatalError.
class Renderer {
public:
    explicit Renderer(const RendererConfig& config);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Triangle list, counter-clockwise front faces.
    MeshHandle CreateMesh(const MeshVertex* vertices, int vertex_count, const std::uint32_t* indices,
                          int index_count);
    // Cube with edge length 1, centered at the origin. Every face shows the whole texture.
    MeshHandle CreateCubeMesh();
    // Square with edge length 1 in the XZ plane, centered at the origin, facing +Y.
    // The texture's top edge lies along -Z.
    MeshHandle CreatePlaneMesh();

    // Copies the image to the graphics card. Textures repeat outside the 0..1 coordinate range.
    TextureHandle CreateTexture(const Image& image, const TextureOptions& options = TextureOptions{});

    void BeginFrame(const Camera& camera, const DirectionalLight& light);
    void DrawMesh(MeshHandle mesh, const glm::mat4& transform, const Material& material);
    // Same, with a plain color and no texture.
    void DrawMesh(MeshHandle mesh, const glm::mat4& transform, Color color);
    void EndFrame();

    // Copies the last finished frame from the graphics card into `image`.
    void CaptureFrame(Image& image);
    // Writes the last finished frame as an 8-bit RGBA PNG. Returns false if the file cannot be written.
    bool SaveFrameToPng(const char* path);

    // "direct3d12" or "vulkan": the backend that is actually in use.
    const char* GetBackendName() const;
    int GetWidth() const;
    int GetHeight() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace poligon
