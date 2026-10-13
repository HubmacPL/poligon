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

// How the wide range of scene brightness is squeezed into what a display can show.
enum class ToneMapping {
    None,  // values are used as they are and clipped at white; exact, for tests
    Aces,  // filmic curve: bright areas roll off smoothly instead of clipping
};

// Color components. Material and light colors are linear and may exceed 1 for lights;
// RendererConfig::clear_color is given as it should look on screen.
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
    // Background color where no sky is drawn. Fixed for the lifetime of the renderer,
    // because Direct3D 12 wants to know the clear color when the image is created.
    Color clear_color{0.0f, 0.0f, 0.0f, 1.0f};
    // Multisample anti-aliasing: each pixel is covered by this many samples, which smooths
    // edges. 1 (off), 2, 4 or 8. Falls back to fewer if the card does not support the count.
    int msaa_samples = 4;
    ToneMapping tone_mapping = ToneMapping::Aces;
    // Brightness multiplier applied before tone mapping.
    float exposure = 1.0f;
    // Width and height in pixels of each of the four shadow images (cascades).
    int shadow_map_size = 2048;

    // Bloom: bright areas spread a soft glow over their surroundings, as in a camera lens.
    // bloom_strength is the share of the blurred image in the final one.
    bool bloom = false;
    float bloom_strength = 0.05f;

    // Automatic exposure: brightness is adjusted so that the average of the image comes out
    // at auto_exposure_target, the way an eye adapts. `exposure` still multiplies the result.
    // The multiplier stays between auto_exposure_min and auto_exposure_max. Each frame moves
    // auto_exposure_speed of the way to the new value (the first frame goes all the way).
    bool auto_exposure = false;
    float auto_exposure_target = 0.25f;
    float auto_exposure_min = 0.05f;
    float auto_exposure_max = 8.0f;
    float auto_exposure_speed = 0.05f;
};

struct Camera {
    glm::vec3 position{0.0f, 2.0f, 5.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float vertical_fov_degrees = 60.0f;
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
};

// The sun: light arriving everywhere from one direction, plus plain ambient light.
struct DirectionalLight {
    // Direction the light travels (from the light towards the scene).
    glm::vec3 direction{-0.4f, -1.0f, -0.3f};
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    // Light that reaches every surface regardless of direction. With Environment::sky_light
    // this is replaced by light from the sky and the ground.
    Color ambient{0.15f, 0.15f, 0.15f, 1.0f};
};

// Everything around the scene: sky, the light it gives, shadows and fog. The default
// (no sky, no fog, shadows on) keeps a scene looking as plain as possible.
struct Environment {
    // Draw the sky as the background instead of the clear color.
    bool draw_sky = false;
    Color sky_zenith{0.16f, 0.32f, 0.75f, 1.0f};   // straight up
    Color sky_horizon{0.60f, 0.72f, 0.88f, 1.0f};  // at eye level
    Color sky_ground{0.20f, 0.18f, 0.15f, 1.0f};   // below the horizon

    // Light surfaces from the sky colors (upward-facing ones from the zenith, downward-facing
    // ones from the ground) instead of DirectionalLight::ambient, and let smooth surfaces
    // mirror the sky.
    bool sky_light = false;
    float sky_light_intensity = 1.0f;

    // The sun casts shadows up to this distance from the camera (meters). Shadows are skipped
    // when the sun's color is black.
    bool shadows = true;
    float shadow_distance = 120.0f;

    // Fog: 0 turns it off; 0.01 hides things about 300 m away. Distant objects fade into fog_color.
    float fog_density = 0.0f;
    Color fog_color{0.60f, 0.72f, 0.88f, 1.0f};

    // Volumetric fog: the fog is computed along the line of sight of every pixel instead of
    // from the distance alone. Fog in the sun's shadow is darker, so shafts of light show
    // between trees; the fog glows around the sun; it can thin out with height; it also
    // covers the sky. Uses fog_density and fog_color.
    bool volumetric_fog = false;
    // The fog is at full density up to fog_base_height (meters) and above it thins out by
    // this factor per meter: 0 = same everywhere, 0.1 = a layer about 10 m thick.
    float fog_height_falloff = 0.0f;
    float fog_base_height = 0.0f;
    // Brightness of fog in shadow relative to sunlit fog. Lower = stronger shafts.
    float fog_shadow_brightness = 0.35f;
    // Strength of the glow around the sun. 0 turns it off.
    float fog_sun_glow = 1.0f;

    // Ambient occlusion: crevices and places close to other surfaces (the inside of a tree
    // crown, where a trunk meets the ground) receive less ambient light. Computed from what
    // is on screen. Does not affect direct sunlight.
    bool ambient_occlusion = false;
    // How far (meters) a surface can be to still darken another.
    float ambient_occlusion_radius = 0.6f;
    float ambient_occlusion_strength = 1.0f;
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
    // true for images that hold colors: the bytes are sRGB-encoded and are converted to linear
    // values when sampled. false for images that hold other data (normal maps, roughness).
    bool is_color = true;
    // Mipmaps are smaller copies of the texture used when it is drawn small or far away;
    // without them distant textures shimmer.
    bool generate_mipmaps = true;
};

// How a surface looks. This is the metallic-roughness material model of glTF.
struct Material {
    // Base color; multiplied by `texture` when there is one.
    Color color{1.0f, 1.0f, 1.0f, 1.0f};
    TextureHandle texture;
    // Multiplies the mesh's texture coordinates; (4, 4) repeats the textures four times in each direction.
    glm::vec2 uv_scale{1.0f, 1.0f};
    // Pixels whose alpha (texture alpha times color alpha) is below this value are not drawn.
    // 0 turns it off. This is how leaves are drawn: a flat square with a picture of a twig,
    // of which only the twig is visible. A typical value is 0.5.
    float alpha_cutoff = 0.0f;
    // Draw both sides of every triangle. Needed for thin things seen from both sides, such
    // as leaves; such surfaces also let some sunlight shine through.
    bool double_sided = false;

    // 0 = non-metal (wood, stone, leaves), 1 = bare metal.
    float metallic = 0.0f;
    // 0 = mirror-smooth, 1 = completely matte.
    float roughness = 0.8f;
    // Normal map: per-pixel surface direction that adds fine relief without more triangles.
    // Create it with TextureOptions::is_color = false.
    TextureHandle normal_texture;
    // Combined data texture, glTF layout: green = roughness, blue = metallic, both multiplied
    // by the factors above; red = ambient occlusion (how much a crevice is shielded from
    // ambient light), used when occlusion_in_orm is true. Create it with is_color = false.
    TextureHandle orm_texture;
    bool occlusion_in_orm = false;
};

// What the last finished frame cost, for checking that culling works.
struct FrameStats {
    // DrawMesh calls between BeginFrame and EndFrame.
    int meshes_submitted = 0;
    // Of those, how many were at least partly inside the camera's view and were drawn.
    int meshes_drawn = 0;
    // Mesh draws in the shadow pass, summed over the four cascades.
    int shadow_draws = 0;
};

// Draws lit meshes. A frame goes through these stages: shadows of the sun, ambient occlusion,
// the scene itself (multisampled, in linear wide-range colors), volumetric fog, bloom, tone
// mapping to an off-screen image. Stages that are turned off are skipped.
// With a window that image is then shown, without one it can only be saved or captured.
// The same pixels are produced in both cases.
//
// Frame order: BeginFrame, any number of DrawMesh, EndFrame. Drawing is recorded by DrawMesh
// and carried out in EndFrame. Meshes entirely outside the camera's view are skipped
// ("frustum culling"), and so are, per shadow cascade, meshes that cannot cast a shadow into it. CaptureFrame and SaveFrameToPng may be called after EndFrame
// and return the frame that was just finished.
// Meshes and textures live until they are destroyed or the Renderer is.
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

    // Free a mesh or texture before the renderer is destroyed, to release graphics memory.
    // The handle must not be used afterwards; its number is not given out again.
    // Call outside BeginFrame/EndFrame.
    void DestroyMesh(MeshHandle mesh);
    void DestroyTexture(TextureHandle texture);

    void BeginFrame(const Camera& camera, const DirectionalLight& light,
                    const Environment& environment = Environment{});
    void DrawMesh(MeshHandle mesh, const glm::mat4& transform, const Material& material);
    // Same, with a plain color and no texture.
    void DrawMesh(MeshHandle mesh, const glm::mat4& transform, Color color);
    void EndFrame();

    // Copies the last finished frame from the graphics card into `image`.
    void CaptureFrame(Image& image);
    // Writes the last finished frame as an 8-bit RGBA PNG. Returns false if the file cannot be written.
    bool SaveFrameToPng(const char* path);

    // Counts for the last finished frame.
    FrameStats GetFrameStats() const;

    // "direct3d12" or "vulkan": the backend that is actually in use.
    const char* GetBackendName() const;
    int GetWidth() const;
    int GetHeight() const;
    // The multisample count actually in use (see RendererConfig::msaa_samples).
    int GetMsaaSamples() const;
    // Changes the text in the window's title bar. Does nothing in headless mode.
    void SetWindowTitle(const char* title);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace poligon
