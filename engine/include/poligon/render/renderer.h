#pragma once

#include <cstdint>
#include <memory>

#include "poligon/core/color.h"
#include "poligon/core/graphics_backend.h"
#include "poligon/core/image.h"
#include "poligon/core/math.h"

namespace poligon {

// How the wide range of scene brightness is squeezed into what a display can show.
enum class ToneMapping {
    None,  // values are used as they are and clipped at white; exact, for tests
    Aces,  // filmic curve: bright areas roll off smoothly instead of clipping
};


// A picture of one stage of the frame instead of the finished frame, for finding out which
// stage is responsible for something that looks wrong.
enum class DebugView {
    None,              // the finished frame
    AmbientOcclusion,  // white = open, dark = occluded
    Fog,               // the light added by volumetric fog
    Bloom,             // the blurred image that bloom mixes in
    CloudShadow,       // the cloud shadow image seen from above (north up, the camera in the middle): white = sunlit
    Motion,            // motion vectors (with TAA): gray-yellow = still, red and green = moved, blue = worked out from depth
};

// What happens to the picture after the scene is drawn. Starts as RendererConfig::post and
// can be changed at any time with Renderer::SetPostSettings.
struct PostSettings {
    ToneMapping tone_mapping = ToneMapping::Aces;
    // Brightness multiplier applied before tone mapping.
    float exposure = 1.0f;

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

    DebugView debug_view = DebugView::None;

    // Temporal anti-aliasing (TAA): the camera shifts by a different fraction of a pixel
    // every frame and each frame is blended into the result of the previous ones, which
    // smooths edges, thin leaves, shimmering highlights and dithered fades. The first frame
    // after turning it on, after ResetTemporalHistory and after a camera jump of more than
    // 25 m has no history and is shown as rendered. Off by default, so that a single frame
    // (tests, examples) looks the same however many frames came before.
    bool temporal_aa = false;
    // Share of the new frame in each blend: smaller is smoother and more stable, larger
    // reacts faster to change. 0.1 means about the last ten frames count.
    float temporal_aa_blend = 0.1f;
    // Undoes some of the softness of TAA: 0 = none, 0.5 = moderate.
    float sharpen = 0.0f;

    // Color grading, applied around tone mapping. temperature: -1 cold (blue) to 1 warm
    // (orange); tint: -1 green to 1 magenta; saturation: 0 gray, 1 unchanged, above 1
    // stronger; contrast: 1 unchanged, around middle gray; vignette: how much the corners
    // darken (0 = none, 0.3 = noticeable).
    float temperature = 0.0f;
    float tint = 0.0f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float vignette = 0.0f;
};

// How the window is shown (Renderer::SetWindowMode).
enum class WindowMode {
    Windowed,
    // Exclusive fullscreen: the display switches to the asked resolution and only the game
    // is shown, as in most games; not a borderless window over the desktop.
    Fullscreen,
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
    // Width and height in pixels of each of the four shadow images (cascades).
    int shadow_map_size = 2048;
    // Share of the drawn width and height of the cloud image (Environment::clouds); the clouds
    // are walked at this size and stretched over the sky.
    float cloud_resolution = 0.25f;
    // The two far cascades use only this share of their image's width and height: they
    // cover large areas where fine shadow detail is lost anyway, and in a forest their cost
    // grows with every pixel (crowns overlap many times as seen from the sun).
    float far_shadow_scale = 1.0f;
    // The scene is drawn at this share of the width and height and brought back to full
    // size by temporal anti-aliasing, which fills in the detail over several frames ("TAA
    // upscaling"). 1 = full resolution. Used only when multisampling is off; while TAA is
    // off the smaller picture is simply stretched.
    float render_scale = 1.0f;
    // Tone mapping, exposure and bloom; see PostSettings.
    PostSettings post;
    // Debug windows drawn over the picture with Dear ImGui (see poligon/debug/debug_ui.h).
    // Ignored in headless mode: without a window there is nobody to look at them.
    bool debug_ui = false;
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
    // 0 to 1: how much fog (volumetric or simple) and the haze of distance take the color of
    // the sky in the direction one looks, from the sky light map (with sky_light), instead of
    // fog_color and the horizon's color: at 1 the fog matches the sky behind it, clouds included.
    float fog_sky_color = 0.0f;
    // Steps along each line of sight of volumetric fog, and samples per pixel of ambient
    // occlusion: more is smoother and costs more (set by GraphicsQuality).
    int fog_steps = 32;
    int ambient_occlusion_samples = 20;
    // Water mirrors what is on screen by walking each reflected ray this many steps through
    // the depth image, up to water_reflection_distance meters; 0 mirrors only the sky.
    int water_reflection_steps = 24;
    float water_reflection_distance = 400.0f;

    // Volumetric clouds in a layer of the sky, with their shadows on the land (drawn where
    // the sky shows, so only with draw_sky). Coverage: 0 = clear, 1 = overcast (it moves a
    // threshold on the clouds' shape, so it is not the share of sky or land covered); they also
    // turn the sky's light greyer and softer. Heights in meters; density is the light lost
    // per meter inside a cloud. They drift downwind (wind_direction, wind_time) at
    // cloud_speed meters per second. Steps along each line of sight and towards the sun:
    // more is smoother and costs more (set by GraphicsQuality).
    bool clouds = false;
    float cloud_coverage = 0.45f;
    float cloud_bottom = 1500.0f;
    float cloud_top = 3500.0f;
    float cloud_density = 0.02f;
    float cloud_size = 6000.0f;     // meters across one repeat of the cloud shapes
    float cloud_speed = 8.0f;
    // How much the weather map varies the coverage across the land: 0 = the same everywhere
    // (scattered clouds of one kind), 1 = large clear areas and large overcast ones, several
    // kilometers across. Fair-weather cumulus is about 0.4.
    float cloud_variation = 0.9f;
    bool cloud_shadows = true;
    int cloud_steps = 48;
    int cloud_light_steps = 5;

    // Haze of distance (aerial perspective): far things fade into a haze that is bluish away
    // from the sun and bright towards it. Density per meter; 0 = off. Its color is the sky's
    // horizon color.
    float aerial_density = 0.0f;
    // How much sunlight thin leaves (double-sided materials) let through to their far side.
    float foliage_translucency = 0.35f;

    // Wind: the direction it blows towards (world x and z), its strength (1 = a moderate
    // breeze; 0 turns the wind off) and how long it has been blowing, in seconds. Give the
    // game's own time, not the clock, so that replays match. Plants move according to
    // their materials (Material::wind_bend, wind_flutter); shadows move with them.
    glm::vec2 wind_direction{1.0f, 0.0f};
    float wind_strength = 0.0f;
    float wind_time = 0.0f;

    // Ambient occlusion: crevices and places close to other surfaces (the inside of a tree
    // crown, where a trunk meets the ground) receive less ambient light. Computed from what
    // is on screen. Does not affect direct sunlight.
    bool ambient_occlusion = false;
    // How far (meters) a surface can be to still darken another.
    float ambient_occlusion_radius = 0.6f;
    float ambient_occlusion_strength = 1.0f;

    // Debug aid: tints surfaces by the shadow cascade that shades them (red, green, blue,
    // yellow from near to far).
    bool show_shadow_cascades = false;
    // Shadows soften with distance from what casts them, as under a real sun: sharp at a
    // trunk's foot, soft under a crown. The sun's apparent size in degrees (the real one is
    // 0.53; a hazy sky spreads it); 0 = every shadow edge as soft as the fixed filter.
    // soft_shadows turns it off (GraphicsQuality::soft_shadows).
    float sun_angular_size = 0.0f;
    bool soft_shadows = true;
};

struct MeshVertex {
    glm::vec3 position;
    glm::vec3 normal;
    // Texture coordinates: (0, 0) is the top-left corner of the image, (1, 1) the bottom-right.
    glm::vec2 uv;
};

// Which joints of a skeleton a vertex follows, and how strongly. One per vertex of a
// skinned mesh, next to its MeshVertex. Weights add up to 1; unused slots have weight 0.
// See poligon/animation/animation.h.
struct SkinVertex {
    std::uint8_t joints[4] = {0, 0, 0, 0};
    float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

// Identifies a mesh created by Renderer. An id of 0 is never a valid mesh.
struct MeshHandle {
    std::uint32_t id = 0;
};

struct CompressedImage;  // poligon/core/texture_compression.h

// A square grid of bytes laid over the ground (world x and z), as the renderer reads it for
// trampled plants (Renderer::SetTrampleMap) and sky occlusion (Renderer::SetSkyOcclusion).
struct GroundMap {
    const std::uint8_t* values = nullptr;  // resolution x resolution, row by row from `corner`
    int resolution = 0;
    glm::vec2 corner{0.0f};  // world x and z of the grid's outer corner with the smallest x and z
    float size = 0.0f;       // meters from edge to edge
    std::uint32_t version = 0;  // changes whenever the values change; the renderer copies them only then
};

// Gives the renderer a GroundMap at the start of every frame. TrampleMap (poligon/world/trample.h)
// and SkyOcclusionMap (poligon/world/sky_occlusion.h) are such sources, so the renderer needs to
// know nothing of how they are made.
class GroundMapSource {
public:
    virtual ~GroundMapSource() = default;
    virtual GroundMap GetGroundMap() const = 0;
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
    // For the color texture of a cut-out material: its alpha cutoff. Each mipmap then has its
    // alpha scaled so that the same share of pixels stays above the cutoff as in the full
    // image; otherwise leaves thin out with distance and vanish from coarse shadow cascades.
    // 0 = off.
    float coverage_cutoff = 0.0f;
};

// Identifies a terrain created by Renderer. An id of 0 is never a valid terrain.
struct TerrainHandle {
    std::uint32_t id = 0;
};

// Most ground textures a terrain can mix.
constexpr int kMaxTerrainLayers = 8;

// How a terrain looks: up to eight ground textures (layers), each covering the land as two
// weight images say, and a plain color on slopes too steep for anything to grow.
//
// The layers' textures are two texture arrays of equal-sized images
// (Renderer::CreateTextureArray): image i of `colors` and of `normals` is layer i. The
// weights come from `weights`: two images (is_color = false) stretched over the whole height
// map, the first corner pixel on its first point, the last on its last; red, green, blue and
// alpha of the first hold the weights of layers 0 to 3, of the second layers 4 to 7.
// MakeTerrainLayerWeights (poligon/world/terrain.h) makes them from the generator's maps and
// rules. Where layers meet, the brighter (higher) texture wins over a short distance, as
// stones poke out of sand.
//
// Without weights layer 0 covers everything; without textures the ground is white.
struct TerrainMaterial {
    TextureHandle colors;
    TextureHandle normals;  // created with TextureOptions::is_color = false
    int layer_count = 0;
    // Meters after which each layer's texture repeats.
    float tile_sizes[kMaxTerrainLayers] = {3.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f};
    TextureHandle weights[2];
    Color steep_color{0.42f, 0.34f, 0.22f, 1.0f};
    // The steep color starts to show at the first slope and is all there is at the second;
    // a start of 90 turns it off (when a rock layer covers steep ground instead).
    float steep_start_degrees = 30.0f;
    float steep_end_degrees = 42.0f;
    float roughness = 0.95f;
};

// How a water surface looks (Renderer::DrawWater). The defaults are clear lake water.
struct WaterMaterial {
    // Share of light lost per meter it travels through the water, per color: red goes
    // first, so deep water turns blue-green and the bottom fades out.
    glm::vec3 absorption{0.45f, 0.12f, 0.08f};
    // How fast, per meter, the water's own color takes over from what lies under it.
    float scattering = 0.35f;
    // Color of the light the water scatters back, lit by the sky and the sun.
    Color color{0.03f, 0.07f, 0.06f, 1.0f};
    // 0 = mirror, larger = blurrier highlight of the sun.
    float roughness = 0.05f;
    // Size of the small waves in meters, and how strongly they tilt the surface. They drift
    // with Environment::wind_direction, faster in a stronger wind.
    float wave_size = 2.0f;
    float wave_strength = 0.1f;
    // How far waves bend the view of what lies under the water (share of the picture).
    float refraction = 0.03f;
    // Water depth in meters over which the surface fades in at the shore.
    float shore_fade = 0.15f;
};

// A square piece of a terrain to draw: (x, z) is its corner with the smallest
// coordinates, in world meters. min_height and max_height enclose the land inside it
// (world heights); they are used for culling.
struct TerrainPatch {
    float x = 0.0f;
    float z = 0.0f;
    float size = 0.0f;
    float min_height = 0.0f;
    float max_height = 0.0f;
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
    // For double_sided: light the back side with the same normal as the front instead of the
    // opposite one. For grass blades, whose normals point mostly up so that a blade is lit
    // like the ground it grows from; flipped, half the blades would face the soil and go dark.
    bool back_faces_keep_normal = false;
    // False: left out of the depth image that ambient occlusion is computed from (and added
    // to it right after). For grass: the screen-space method takes a thin blade for a solid
    // wall and would blacken both the blade and the ground around it.
    bool casts_ambient_occlusion = true;
    // 0 to 1: how far the normal is turned towards straight up before lighting, on both
    // sides. Grass at 1 is lit like the flat ground it stands on, whichever way its cards
    // or blades face; low plants such as ferns look right at about 0.5.
    float normal_up = 0.0f;

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

    // How the wind moves a mesh with this material (0 = not at all). wind_bend: the whole
    // plant leans downwind, by this much times about 3 cm per meter of height at full
    // strength (1 for a tree, more for short plants, which bend more per meter).
    // wind_flutter: every vertex shivers on its own, by this much times about 4 cm (leaves,
    // grass blades). The model's origin counts as the plant's foot.
    float wind_bend = 0.0f;
    float wind_flutter = 0.0f;
};

// What the last finished frame cost, for checking that culling works and where time goes.
struct FrameStats {
    // Time the processor spent in EndFrame preparing the frame (culling, copying instance
    // matrices, recording the commands), in milliseconds, and the time handing the commands
    // to the card took; the latter grows when the card is behind and the processor has to
    // wait for it. Together with the frame time this says whether the processor or the
    // card is the bottleneck.
    float cpu_milliseconds = 0.0f;
    float wait_milliseconds = 0.0f;
    // DrawMesh calls between BeginFrame and EndFrame.
    int meshes_submitted = 0;
    // Of those, how many were at least partly inside the camera's view and were drawn.
    int meshes_drawn = 0;
    // Mesh draws in the shadow pass, summed over the four cascades.
    int shadow_draws = 0;
    // Instances given to DrawMeshInstances, how many of them were inside the camera's
    // view, and how many were drawn into shadow cascades (summed over the cascades).
    int instances_submitted = 0;
    int instances_drawn = 0;
    int shadow_instances = 0;
    // Draw calls the instances needed, over all passes: one per batch and pass, however
    // many instances the batch has.
    int instance_draw_calls = 0;
};

// Where the four shadow cascades end, as distances in front of the camera (see
// Environment::shadow_distance). Lets a caller choose, by distance, which instances cast
// shadows into which cascade.
glm::vec4 ComputeShadowCascadeEnds(float near_plane, float shadow_distance);

// How a batch of instances takes part in a frame.
struct InstanceOptions {
    // Draw the instances in the picture. false: they only cast shadows.
    bool draw_in_view = true;
    // Which shadow cascades the instances are drawn into: bit 0 = nearest cascade ... bit 3
    // = farthest. 0 = they cast no shadow. Lets distant cascades use simpler meshes.
    std::uint8_t shadow_cascades = 0x0F;
};

// Identifies an impostor created by Renderer::CreateImpostor. An id of 0 means "none".
struct ImpostorHandle {
    std::uint32_t id = 0;
};

// One piece of a model to bake into an impostor: a mesh, its material, and where it sits in
// the model.
struct ImpostorPart {
    MeshHandle mesh;
    Material material;
    glm::mat4 transform{1.0f};
};

// Pictures of a model taken from many directions, for drawing it far away as one flat card
// that always faces the camera (an "impostor"). Made by Renderer::BakeImpostor, kept in
// files by poligon/render/impostor.h, drawn after Renderer::CreateImpostor.
//
// The pictures lie side by side in two images, frames_per_side x frames_per_side of them.
// Frame (column, row) looks at the model from the direction that the grid point
// (column, row) maps to on the half sphere above it: the middle of the grid looks straight
// down, its edge from the horizon ("hemi-octahedral" layout, see engine/shaders/impostor.hlsli).
// The pictures hold what the surface is, not how it is lit, so an impostor is lit by the
// current sun and sky like everything else.
struct Impostor {
    // Base color (sRGB-encoded); alpha says where the model is.
    Image color;
    // Direction each pixel faces, in the model's own coordinates, as 0..1 per axis; same alpha.
    Image normal;
    int frames_per_side = 0;
    // Middle of the model in its own coordinates, and the radius around it that each frame shows.
    glm::vec3 center{0.0f};
    float radius = 0.0f;
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
    // A mesh that bends with a skeleton: `skin` holds one entry per vertex. A skeleton can
    // have at most 256 joints. Draw it with DrawSkinnedMesh.
    MeshHandle CreateSkinnedMesh(const MeshVertex* vertices, const SkinVertex* skin, int vertex_count,
                                 const std::uint32_t* indices, int index_count);
    // Cube with edge length 1, centered at the origin. Every face shows the whole texture.
    MeshHandle CreateCubeMesh();
    // Square with edge length 1 in the XZ plane, centered at the origin, facing +Y.
    // The texture's top edge lies along -Z.
    MeshHandle CreatePlaneMesh();

    // Copies a height map to the graphics card: `heights` holds width x depth values, row
    // by row, `spacing` meters apart. `origin` is the world position of the first point;
    // its y is added to every height. See poligon/world/terrain.h, which is the usual way in.
    TerrainHandle CreateTerrain(const float* heights, int width, int depth, float spacing, const glm::vec3& origin);
    void DestroyTerrain(TerrainHandle terrain);
    // Draws pieces of a terrain. Each patch is one flat grid of 64 x 64 cells stretched
    // over its square and lifted to the land's height, so a large patch is coarse and a
    // small one fine. All patches of a call cost one draw call per pass, and each is culled
    // on its own. Patch corners should lie on height map points.
    void DrawTerrain(TerrainHandle terrain, const TerrainMaterial& material, const TerrainPatch* patches, int count);
    // Grass and small plants standing on trampled ground are bent flat: instanced meshes
    // whose material bends in the wind (wind_bend above 0) read `map` at their foot, lean
    // over and get lower by its value. The map is copied when its version changes; it must
    // live as long as it is set. nullptr turns it off. See poligon/world/trample.h.
    void SetTrampleMap(const GroundMapSource* map);
    // How much of the sky each place sees (hills, tree crowns): sky light on every surface is
    // darkened by it. Copied when its version changes; it must live as long as it is set.
    // nullptr turns it off. See poligon/world/sky_occlusion.h.
    void SetSkyOcclusion(const GroundMapSource* map);
    // Makes the noise volumes clouds are shaped from (about a second). Otherwise the first
    // frame with Environment::clouds does it; call this while loading to keep that frame short.
    void PrepareClouds();
    // Draws a water surface: usually a flat mesh at the water's level (poligon/world/water.h makes
    // them for lakes). It is drawn after everything else in the scene and reads what was
    // drawn under it: see WaterMaterial and Environment::water_reflection_steps.
    void DrawWater(MeshHandle mesh, const glm::mat4& transform, const WaterMaterial& material);

    // Copies the image to the graphics card. Textures repeat outside the 0..1 coordinate range.
    TextureHandle CreateTexture(const Image& image, const TextureOptions& options = TextureOptions{});
    // Copies `count` images of the same size into one texture array (used for the layers of a
    // terrain, TerrainMaterial). A shader picks the image by number.
    TextureHandle CreateTextureArray(const Image* images, int count, const TextureOptions& options = TextureOptions{});
    // The same from block-compressed images with their mipmaps (poligon/core/texture_compression.h),
    // as the asset cooker makes them: they are copied as they are. Normal maps in Bc5 hold x
    // and y only; shaders rebuild z.
    TextureHandle CreateTexture(const CompressedImage& image);
    TextureHandle CreateTextureArray(const CompressedImage* images, int count);

    // Free a mesh or texture before the renderer is destroyed, to release graphics memory.
    // The handle must not be used afterwards; its number is not given out again.
    // Call outside BeginFrame/EndFrame.
    void DestroyMesh(MeshHandle mesh);
    void DestroyTexture(TextureHandle texture);

    void BeginFrame(const Camera& camera, const DirectionalLight& light,
                    const Environment& environment = Environment{});
    // `object_id`: any nonzero number that stays the same for this object from frame to
    // frame (an entity, a physics body). The renderer then remembers where the object was
    // drawn in the previous frame, and temporal anti-aliasing follows it when it moves
    // (motion vectors) instead of smearing it. 0 = the object stands still (plants moving in
    // the wind are followed anyway). Several draws with one id in a frame (the meshes of one
    // model) are told apart by their order.
    void DrawMesh(MeshHandle mesh, const glm::mat4& transform, const Material& material, std::uint64_t object_id = 0);
    // Same, with a plain color and no texture.
    void DrawMesh(MeshHandle mesh, const glm::mat4& transform, Color color);
    // Draws one mesh many times in a single draw call per pass: `transforms` holds one
    // matrix per copy. This is the way to draw vegetation. Each copy is culled on its own
    // against the camera's view and against each shadow cascade. The matrices are copied,
    // so the array need not outlive the call. Not for skinned meshes.
    // The last value of each matrix ([3][3]), 1 in an ordinary transform, carries a fade
    // between levels of detail: between 0 and 1 that share of the copy's pixels is drawn,
    // between -1 and 0 that share is left out, by a fixed pattern, so that a copy fading out
    // and one fading in at the same place fill each pixel exactly once. Vegetation sets it.
    void DrawMeshInstances(MeshHandle mesh, const Material& material, const glm::mat4* transforms, int count,
                           const InstanceOptions& options = InstanceOptions{});
    // Draws a mesh made by CreateSkinnedMesh in a pose. `joint_matrices` come from
    // ComputeJointMatrices (poligon/animation/animation.h), one per joint of the skeleton; they are
    // copied, so the array need not outlive the call. `transform` places the whole model.
    // `object_id` as in DrawMesh: with it, the previous frame's pose gives motion vectors.
    void DrawSkinnedMesh(MeshHandle mesh, const glm::mat4& transform, const Material& material,
                         const glm::mat4* joint_matrices, int joint_count, std::uint64_t object_id = 0);

    // Photographs a model for an impostor (see Impostor): frames_per_side x frames_per_side
    // pictures of frame_size x frame_size pixels, without perspective. Cut-out materials are
    // cut out as when drawn. Call outside BeginFrame/EndFrame; it waits until the graphics
    // card has finished.
    void BakeImpostor(const ImpostorPart* parts, int count, int frames_per_side, int frame_size, Impostor& impostor);
    // Copies an impostor's pictures to the graphics card.
    ImpostorHandle CreateImpostor(const Impostor& impostor);
    // The same from compressed pictures (as cooked; color with alpha, normal with alpha).
    ImpostorHandle CreateImpostor(const CompressedImage& color, const CompressedImage& normal, int frames_per_side,
                                  const glm::vec3& center, float radius);
    // Call outside BeginFrame/EndFrame. The handle must not be used afterwards.
    void DestroyImpostor(ImpostorHandle impostor);
    // Draws an impostor once per transform, like DrawMeshInstances: each copy is a card the
    // size of the model facing the camera, showing the four baked pictures nearest to the
    // direction of view blended together. The transforms place the model as they would place
    // its meshes; rotation about the vertical and uniform scale are allowed. Impostors cast
    // no shadows (options.shadow_cascades is ignored): they are meant for distances beyond
    // the shadows' reach. The fade in [3][3] works as for DrawMeshInstances.
    void DrawImpostorInstances(ImpostorHandle impostor, const glm::mat4* transforms, int count,
                               const InstanceOptions& options = InstanceOptions{});
    void EndFrame();

    // Copies the last finished frame from the graphics card into `image`.
    void CaptureFrame(Image& image);
    // Writes the last finished frame as an 8-bit RGBA PNG. Returns false if the file cannot be written.
    bool SaveFrameToPng(const char* path);

    // Tone mapping, exposure, bloom and the debug view can be changed between frames.
    const PostSettings& GetPostSettings() const;
    void SetPostSettings(const PostSettings& settings);
    // Forgets the frames temporal anti-aliasing has collected, so that the next frame starts
    // fresh. Call it on a cut (teleport, switch of camera); camera jumps of more than 25 m
    // are detected on their own.
    void ResetTemporalHistory();

    // True when debug windows can be drawn: RendererConfig::debug_ui was set and there is a
    // window. Dear ImGui calls are only allowed between BeginFrame and EndFrame, and only
    // when this is true.
    bool IsDebugUiActive() const;

    // Counts for the last finished frame.
    FrameStats GetFrameStats() const;

    // "direct3d12" or "vulkan": the backend that is actually in use.
    const char* GetBackendName() const;
    int GetWidth() const;
    int GetHeight() const;
    // The multisample count actually in use (see RendererConfig::msaa_samples).
    int GetMsaaSamples() const;
    // Size of the image the scene is drawn into (smaller than the output with render_scale).
    int GetRenderWidth() const;
    int GetRenderHeight() const;
    // Changes the text in the window's title bar. Does nothing in headless mode.
    void SetWindowTitle(const char* title);
    // Shows the game in a window of width x height, or fullscreen with the display switched
    // to that resolution (WindowMode::Fullscreen). The picture is drawn at the new size from
    // the next frame on (every image that depends on it is made again). Call outside
    // BeginFrame/EndFrame. Returns false, and changes nothing, when the display has no mode of
    // exactly that size. Without a window (headless) only the picture's size changes.
    bool SetWindowMode(WindowMode mode, int width, int height);
    WindowMode GetWindowMode() const;

private:
    TextureHandle CreateTextureArray(const CompressedImage* images, int count, bool plain);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace poligon
