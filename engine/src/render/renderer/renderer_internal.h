#pragma once

// Private to the renderer (engine/src/render): the uniform layouts the shaders read, the
// renderer's internal types and its state, Renderer::Impl. The public API is in
// poligon/render/renderer.h. Renderer::Impl's functions are spread by subject:
//   renderer/   the public API and the frame (renderer.cpp), render targets and samplers
//               (targets.cpp), shaders and pipelines (pipelines.cpp), drawing inside a pass
//               and culling instances (drawing.cpp)
//   resources/  what lives on the graphics card: meshes, textures, terrain, impostors,
//               uploads, the cloud noise volumes; decals, particles and 2D each with a
//               state header (decal_state.h, particle_state.h, overlay_state.h) and the
//               file that records them and makes their pipelines
//   passes/     one file per pass of a frame, in the order EndFrame runs them: shadows, cloud
//               shadows, depth and ambient occlusion, scene, decals, water, clouds, fog,
//               temporal anti-aliasing, particles, bloom and exposure, output (tone mapping,
//               2D overlay and debug windows)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <deque>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "poligon/core/fatal_error.h"
#include "poligon/core/texture_compression.h"
#include "poligon/render/renderer.h"
#include "../resources/atmosphere_state.h"
#include "../resources/decal_state.h"
#include "../resources/light_state.h"
#include "../resources/occlusion_state.h"
#include "../resources/overlay_state.h"
#include "../resources/particle_state.h"

namespace poligon {

// Everything below is used only by the renderer's own files.
namespace render_internal {

constexpr int kCascadeCount = 4;

// Uniform layouts. Each must match the cbuffer of the same name in engine/src/render/shaders.
struct VertexFrameUniforms {
    glm::mat4 view_projection;
    glm::vec4 wind{0.0f};
    glm::vec4 trample{0.0f};  // xy: corner of the trample map, z: 1 / its size, w: 1 = on
    // Motion vectors, worked out in the depth pass only (motion.x = 1).
    glm::mat4 motion_view_projection{1.0f};    // this frame's camera without its sub-pixel shift
    glm::mat4 previous_view_projection{1.0f};  // the previous frame's, without its shift
    glm::vec4 previous_wind{0.0f};
    glm::vec4 motion{0.0f};
};

struct ObjectUniforms {
    glm::mat4 model;
    glm::vec4 color;
    glm::vec4 uv_scale;  // w: where the previous frame's joint matrices start (skinned meshes)
    glm::vec4 wind_response{0.0f};
    glm::mat4 previous_model{1.0f};  // the transform in the previous frame
    // Turns normals into the world: the inverse transpose of the model's 3 x 3 part, so that
    // normals stay square to the surface under any scale (columns 0 to 2; the rest unused).
    glm::mat4 normal_matrix{1.0f};
    // Added to camera-relative positions where a shader needs world-fixed noise or waves
    // (wind gusts): see NoiseShift.
    glm::vec4 noise_shift{0.0f};
};

struct FragmentFrameUniforms {
    glm::mat4 shadow_matrices[kCascadeCount];
    glm::vec4 cascade_far;
    glm::vec4 cascade_texel;
    glm::vec4 camera_position;
    glm::vec4 camera_forward;
    glm::vec4 light_direction;
    glm::vec4 light_color;
    glm::vec4 ambient_sky;
    glm::vec4 ambient_ground;
    glm::vec4 sky_zenith;
    glm::vec4 sky_horizon;
    glm::vec4 sky_ground;
    glm::vec4 fog;
    glm::vec4 options;
    glm::vec4 screen;
    glm::vec4 cascade_scale;
    glm::vec4 cloud_shadow_area;
    glm::vec4 sky_occlusion_area;
    glm::vec4 aerial;
    glm::vec4 foliage;
    glm::vec4 sky_radiance;  // x: the sky light map's level for roughness 1, y: 1 = in use, z: its level for ambient light
    glm::vec4 cascade_depth;  // meters across each cascade's depth range
    glm::vec4 shadow_soft;    // x: penumbra width per meter from the caster (0 = fixed filter), y: widest filter, in filter widths
};

// The sky light map (RenderSkyLightPass): the captured sky, faces kSkyCaptureSize pixels wide
// with a full chain of smaller levels, and the map surfaces read, one level per roughness.
constexpr int kSkyCaptureSize = 64;
constexpr int kSkyCaptureLevels = 7;   // 64 down to 1
// 64 down to 4: levels 0 to 3 for roughness 0, 1/3, 2/3, 1; the last for ambient light.
constexpr int kSkyRadianceLevels = 5;
// The map is renewed over this many frames: one face captured per frame, then the smaller
// levels, then the blurred levels.
constexpr int kSkyLightCycle = 8;

// Must match sky_capture.frag.hlsl (the clouds' uniforms come first, as CloudUniforms).
struct SkyCaptureUniforms {
    glm::vec4 capture;
    glm::vec4 zenith;
    glm::vec4 horizon;
    glm::vec4 ground;
    glm::vec4 eye;
    glm::vec4 atmosphere;  // rgb: the sun above the air times pi and the sky light's strength, w: 1 = on
    glm::vec4 to_sun;
};

// Must match sky_prefilter.frag.hlsl.
struct SkyPrefilterUniforms {
    glm::vec4 params;
};

// Must match clouds_resolve.frag.hlsl.
struct CloudResolveUniforms {
    glm::mat4 inverse_view_projection;
    glm::mat4 previous_view_projection;
    glm::vec4 camera_position;
    glm::vec4 params;
};

struct CloudUniforms {
    glm::mat4 inverse_view_projection;
    glm::vec4 camera_position;
    glm::vec4 light_direction;
    glm::vec4 light_color;
    glm::vec4 sky_zenith;
    glm::vec4 sky_horizon;
    glm::vec4 layer;
    glm::vec4 shape;
    glm::vec4 quality;
    glm::vec4 shadow_area;
    glm::vec4 weather;  // x: Environment::cloud_variation
};

struct MaterialUniforms {
    glm::vec4 factors;
    glm::vec4 flags;
    glm::vec4 shading;  // x: Material::normal_up
    glm::vec4 emissive;  // rgb: emitted light (Material::emissive times its strength), w: 1 = has an emissive texture
};

// What the blend pass's fragment shader adds to MaterialUniforms (mesh.frag.hlsl, BLENDED):
// how to work out the volumetric fog in front of a blended surface from the fog of the scene
// behind it.
struct BlendUniforms {
    glm::vec4 fog;      // x: 1 = volumetric fog image bound, y: density at the base height, z: height falloff, w: base height relative to the camera
    glm::vec4 screen;   // xy: scene image size in pixels, z: how far the fog was walked (kFogWalkDistance)
    glm::mat4 inverse_view_projection;
};

struct ImpostorUniforms {
    glm::vec4 center_radius;
    glm::vec4 camera_frames;
    glm::vec4 batch;
};

struct ImpostorMaterial {
    glm::vec4 material;
};

struct SkyUniforms {
    glm::mat4 inverse_view_projection;
    glm::vec4 camera_position;
    glm::vec4 light_direction;
    glm::vec4 sun_color;
    glm::vec4 sky_zenith;
    glm::vec4 sky_horizon;
    glm::vec4 sky_ground;
    glm::vec4 atmosphere;  // rgb: the sun above the air times pi, w: 1 = atmosphere model
};

struct TerrainVertexUniforms {
    glm::vec4 map;
    glm::vec4 map_heights;
};

struct WaterUniforms {
    glm::mat4 view_projection;
    glm::mat4 inverse_view_projection;
    glm::vec4 absorption;
    glm::vec4 water_color;
    glm::vec4 waves;
    glm::vec4 wind;
    glm::vec4 screen;
    glm::vec4 noise_shift;  // xyz: see NoiseShift (the waves stay in place as the camera moves)
};

// One water surface to draw (Renderer::DrawWater).
struct WaterCommand {
    std::uint32_t mesh_id = 0;
    glm::mat4 transform{1.0f};  // relative to the camera
    WaterMaterial material;
    glm::vec3 noise_shift{0.0f};
};

struct TerrainMaterialUniforms {
    glm::vec4 map;
    glm::vec4 map_heights;
    glm::vec4 steep;
    glm::vec4 steep_color;
    glm::vec4 tile_scales[2];
    glm::vec4 weight_map;
    // Per layer: where the texture's repeat stands at the camera (xy for the layer's own
    // size, zw for seven times larger), so that camera-relative positions tile as world ones.
    glm::vec4 tile_phases[8];
    glm::vec4 noise_shift;  // xy: camera-relative x and z to the terrain's own (for its mottling)
};

struct TonemapUniforms {
    glm::vec4 parameters;
    glm::vec4 auto_exposure;
    glm::vec4 white_balance;
    glm::vec4 grading;
    glm::vec4 input;
};

struct TaaUniforms {
    glm::mat4 inverse_view_projection;
    glm::mat4 current_view_projection;
    glm::mat4 previous_view_projection;
    glm::vec4 parameters;
    glm::vec4 input;
};

// What a shader adds to a camera-relative position to get one fixed in the world for noise
// and waves (wind gusts, water ripples): the camera minus `origin` rounded to 4096 m, so that
// the sum (world position minus that snapped origin) stays small and exact in float. The
// pattern is continuous within a 4096 m square and jumps at its edges, once, for things
// whose origin crosses one.
inline glm::vec3 NoiseShift(const glm::dvec3& camera, const glm::dvec3& origin) {
    const glm::dvec3 snapped = glm::round(origin / 4096.0) * 4096.0;
    return glm::vec3(camera - snapped);
}

// Sub-pixel camera shifts for temporal anti-aliasing: the first 8 points of the Halton
// sequence in bases 2 and 3, which cover a pixel evenly whatever prefix is used.
inline float Halton(int index, int base) {
    float result = 0.0f;
    float fraction = 1.0f;
    for (int i = index + 1; i > 0; i /= base) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(i % base);
    }
    return result;
}
constexpr int kTaaSequenceLength = 8;
// A camera that moves further than this between two frames has jumped (a cut); the
// history then no longer fits and is dropped.
constexpr float kTaaCutDistance = 25.0f;

struct AoUniforms {
    glm::vec4 projection_info;
    glm::vec4 screen;
    glm::vec4 ao;
};

struct AoBlurUniforms {
    glm::vec4 projection_info;
    glm::vec4 direction;
};

struct FogUniforms {
    glm::mat4 inverse_view_projection;
    glm::mat4 shadow_matrices[kCascadeCount];
    glm::vec4 cascade_far;
    glm::vec4 camera_position;
    glm::vec4 camera_forward;
    glm::vec4 light_direction;
    glm::vec4 light_color;
    glm::vec4 fog;
    glm::vec4 fog_shape;
    glm::vec4 options;
    glm::vec4 quality;
    glm::vec4 cloud_shadow_area;
    GpuLight local_lights[4];       // the four lights nearest the camera
    glm::vec4 local_light_count;
};

struct FogCompositeUniforms {
    glm::vec4 projection_info;
    glm::vec4 sizes;
};

// Shared by the bloom and luminance shaders.
struct ParameterUniforms {
    glm::vec4 parameters;
};

// The GPU reads these as tightly packed floats.
static_assert(sizeof(glm::vec2) == 2 * sizeof(float), "glm::vec2 must be two floats");
static_assert(sizeof(glm::vec3) == 3 * sizeof(float), "glm::vec3 must be three floats");
static_assert(sizeof(glm::vec4) == 4 * sizeof(float), "glm::vec4 must be four floats");
static_assert(sizeof(glm::mat4) == 16 * sizeof(float), "glm::mat4 must be sixteen floats");
static_assert(sizeof(MeshVertex) == 8 * sizeof(float), "MeshVertex must have no padding");
static_assert(sizeof(SkinVertex) == 20 && offsetof(SkinVertex, weights) == 4, "SkinVertex must have no padding");

// A vertex stores a joint number in one byte.
constexpr int kMaxJoints = 256;

// The scene is drawn in linear colors with a wide range (values above 1 are kept), so that
// lighting adds up correctly and bright areas can be tone mapped instead of clipped.
constexpr SDL_GPUTextureFormat kSceneFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
// The final image stores sRGB-encoded bytes, like image files and displays do. The GPU
// converts automatically: shaders write linear values, the stored bytes are sRGB.
constexpr SDL_GPUTextureFormat kOutputFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
constexpr SDL_GPUTextureFormat kDepthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
constexpr SDL_GPUTextureFormat kAoFormat = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
// Motion vectors: each pixel's motion on screen since the previous frame, in texture coordinates.
constexpr SDL_GPUTextureFormat kMotionFormat = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
// Stored where nothing drawn worked out a motion (the sky, impostors); the shaders return
// it as float2(4, 4).
constexpr float kNoMotion = 4.0f;
constexpr SDL_GPUTextureFormat kLuminanceFormat = SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
// Terrain heights as 16-bit values between the lowest and the highest point of the map:
// 65535 steps, which is millimeters for hills. Unlike a float format it can always be
// read with blending between points.
constexpr SDL_GPUTextureFormat kHeightFormat = SDL_GPU_TEXTUREFORMAT_R16_UNORM;
// Frames a headless renderer lets the processor be ahead of the graphics card (a window's
// swapchain allows two by default, SDL_SetGPUAllowedFramesInFlight).
constexpr int kMaxFramesInFlight = 2;
// Cells along one edge of the grid mesh that every terrain patch is drawn with.
constexpr int kTerrainGridCells = 64;

// The fog is walked at most this far (meters) along each line of sight.
constexpr float kFogWalkDistance = 1000.0f;
constexpr float kPi = 3.14159265f;
// Share of each smaller (blurrier) bloom image when it is mixed into the next larger one.
constexpr float kBloomSpread = 0.65f;
constexpr int kMaxBloomLevels = 6;

// Shadow casters this far behind the visible area (towards the sun) still cast shadows into it.
constexpr float kShadowCasterDistance = 150.0f;

struct Mesh {
    SDL_GPUBuffer* vertex_buffer = nullptr;
    SDL_GPUBuffer* index_buffer = nullptr;
    Uint32 index_count = 0;
    // Joints and weights per vertex; null for a mesh that does not bend.
    SDL_GPUBuffer* skin_buffer = nullptr;
    // Box around all vertices, in the mesh's own coordinates.
    glm::vec3 bounds_min;
    glm::vec3 bounds_max;
};

// One image of the bloom chain.
struct BloomLevel {
    SDL_GPUTexture* texture = nullptr;
    int width = 0;
    int height = 0;
};

// How a full-screen pass combines what it writes with what the target holds.
enum class FullscreenBlend {
    Replace,
    // target = written color + target * written alpha
    AddAndDim,
    // target = mix(target, written color, written alpha)
    Mix,
    // target = target + written color (light added: sparks, glowing dust)
    Add,
};

struct DrawCommand {
    std::uint32_t mesh_id;
    glm::mat4 transform;
    Material material;
    // Box around the mesh before `transform`, used for culling. For a skinned mesh it
    // encloses the mesh in its current pose.
    glm::vec3 bounds_min;
    glm::vec3 bounds_max;
    // Skinned meshes: where this draw's joint matrices start in the frame's joint buffer.
    std::uint32_t first_joint = 0;
    bool skinned = false;
    // Where the object was in the previous frame (the same draw when it has no object id):
    // its transform and, for skinned meshes, where that pose's joint matrices start.
    glm::mat4 previous_transform{1.0f};
    std::uint32_t previous_first_joint = 0;
    // Dithered fade between levels of detail (KeptByFade in common.hlsli): 1 = drawn whole.
    float fade = 1.0f;
    glm::vec3 noise_shift{0.0f};  // see NoiseShift
};

// A draw with an object id (Renderer::DrawMesh), told apart from other draws with the same id
// in one frame by its order among them.
struct DrawnObjectKey {
    std::uint64_t object_id = 0;
    std::uint32_t order = 0;
    bool operator==(const DrawnObjectKey& other) const {
        return object_id == other.object_id && order == other.order;
    }
};

struct DrawnObjectKeyHash {
    size_t operator()(const DrawnObjectKey& key) const {
        return std::hash<std::uint64_t>()(key.object_id * 31u + key.order);
    }
};

// What the next frame needs to know about a draw with an object id.
struct DrawnObject {
    glm::mat4 transform{1.0f};  // relative to that frame's camera
    std::uint32_t first_joint = 0;  // in that frame's joint_palette
    int joint_count = 0;
};

// The three passes that draw meshes.
// kSceneAfterDepthPass: the scene pass when the depth pass has already drawn the final
// depth; it tests against that depth before shading and writes none (see EndFrame).
// kBlendPass: blended materials (BlendMode::Blend) over the finished scene, testing its depth.
enum MeshPass { kScenePass, kShadowPass, kDepthPass, kSceneAfterDepthPass, kBlendPass, kMeshPassCount };

inline bool IsScenePass(MeshPass pass) {
    return pass == kScenePass || pass == kSceneAfterDepthPass;
}
// How a mesh's vertices are placed: by one transform, by a skeleton, or once per instance.
enum MeshKind { kRigid, kSkinned, kInstanced, kMeshKindCount };

// The lists of instances a batch is culled into: the camera's view, then one per cascade.
// The view, the sun's cascades, then the spot lights with shadows.
constexpr int kSpotShadowList = 1 + kCascadeCount;
constexpr int kInstanceListCount = 1 + kCascadeCount + kMaxShadowedSpotLights;

// A height map on the graphics card.
// Textures and buffers through these are counted (gpu_memory.cpp, FrameStats::texture_bytes):
// the same as SDL_CreateGPUTexture and friends otherwise.
SDL_GPUTexture* CreateTrackedTexture(SDL_GPUDevice* device, const SDL_GPUTextureCreateInfo* info);
void ReleaseTrackedTexture(SDL_GPUDevice* device, SDL_GPUTexture* texture);
SDL_GPUBuffer* CreateTrackedBuffer(SDL_GPUDevice* device, const SDL_GPUBufferCreateInfo* info);
void ReleaseTrackedBuffer(SDL_GPUDevice* device, SDL_GPUBuffer* buffer);
std::uint64_t GetTrackedTextureBytes();
std::uint64_t GetTrackedBufferBytes();

// What each view keeps of its own: the window's, and every render target's
// (Renderer::CreateRenderTarget). The renderer draws with the fields of Renderer::Impl; a frame
// into a render target swaps its view in at BeginFrame and out at EndFrame (Impl::SwapView),
// so every pass works for either without knowing which.
struct ViewState {
    int width = 0;  // the output's size (RendererConfig::width and height while active)
    int height = 0;
    int render_width = 0;
    int render_height = 0;
    SDL_GPUTexture* scene_color = nullptr;
    SDL_GPUTexture* scene_depth = nullptr;
    SDL_GPUTexture* scene_resolved = nullptr;
    SDL_GPUTexture* output_target = nullptr;
    SDL_GPUTexture* prepass_depth = nullptr;
    SDL_GPUTexture* ao_target = nullptr;
    SDL_GPUTexture* ao_scratch = nullptr;
    SDL_GPUTexture* fog_target = nullptr;
    int fog_width = 0;
    int fog_height = 0;
    std::vector<BloomLevel> bloom_levels;
    SDL_GPUTexture* luminance_targets[2] = {nullptr, nullptr};
    int luminance_index = 0;
    bool has_luminance_history = false;
    SDL_GPUTexture* water_scene_copy = nullptr;
    SDL_GPUTexture* water_depth_copy = nullptr;
    SDL_GPUTexture* cloud_target = nullptr;
    SDL_GPUTexture* cloud_history[2] = {nullptr, nullptr};
    int cloud_history_index = 0;
    bool cloud_history_valid = false;
    std::uint64_t cloud_history_frame = 0;
    glm::mat4 previous_cloud_view_projection{1.0f};
    SDL_GPUTexture* taa_targets[2] = {nullptr, nullptr};
    SDL_GPUTexture* motion_target = nullptr;
    int taa_index = 0;
    bool has_taa_history = false;
    glm::mat4 previous_view_projection{1.0f};
    glm::dvec3 previous_camera_position{0.0};
    std::uint64_t frame_index = 0;
    std::unordered_map<DrawnObjectKey, DrawnObject, DrawnObjectKeyHash> drawn_objects;
    std::unordered_map<DrawnObjectKey, DrawnObject, DrawnObjectKeyHash> previous_drawn_objects;
    std::vector<glm::mat4> previous_joint_palette;
    glm::vec4 previous_wind{0.0f};
    bool has_previous_wind = false;
};

// A render target: its view, and its picture as a texture.
struct RenderTargetData {
    ViewState view;
    TextureHandle texture;
    bool alive = false;
};

// One page of a terrain's height array: a terrain of one map has one page.
struct TerrainPage {
    glm::dvec3 origin{0.0};  // world position of the first point; y is that of image value 0
    float height_range = 1.0f;
    glm::vec2 last_point{0.0f};  // index of the last point that holds land, along x and z
};

// A terrain's heights (and, for paged terrains, ground layer weights) on the graphics card,
// as texture arrays with one layer per page (Renderer::CreateTerrainPages).
struct TerrainData {
    SDL_GPUTexture* height_texture = nullptr;  // R16 array, one layer per page
    int width = 0;
    int depth = 0;
    float spacing = 1.0f;
    std::vector<TerrainPage> pages;
    // Ground layer weights as arrays (layers 0..3 and 4..7), one layer per page. A terrain
    // of one map copies its material's two plain weight images into layer 0 when they change.
    SDL_GPUTexture* low_weights = nullptr;
    SDL_GPUTexture* high_weights = nullptr;
    int weight_points = 0;
    bool paged = false;              // made by CreateTerrainPages
    int border_points = 0;           // rim of each page outside its weights
    bool paged_weights = false;      // the pages bring their own weights
    std::uint32_t copied_weights[2] = {0, 0};  // texture ids copied into the arrays (one map)
};

// An impostor on the graphics card (Renderer::CreateImpostor).
struct ImpostorData {
    TextureHandle color;
    TextureHandle normal;
    glm::vec3 center{0.0f};
    float radius = 0.0f;
    int frames_per_side = 0;
};

// Impostor pixels below this coverage are not drawn.
constexpr float kImpostorAlphaCutoff = 0.5f;
// Impostors are mostly foliage; one roughness for the whole picture.
constexpr float kImpostorRoughness = 0.9f;

// The same two functions as in engine/shaders/impostor.hlsli, for baking.
inline glm::vec3 HemiOctahedronDirection(glm::vec2 grid) {
    glm::vec3 direction((grid.x - grid.y) * 0.5f, 0.0f, (grid.x + grid.y) * 0.5f);
    direction.y = 1.0f - std::fabs(direction.x) - std::fabs(direction.z);
    return glm::normalize(direction);
}

inline void ImpostorFrameAxes(const glm::vec3& direction, glm::vec3& right, glm::vec3& up) {
    const glm::vec3 side = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), direction);
    const float side_length = glm::length(side);
    right = side_length > 0.0001f ? side / side_length : glm::vec3(1.0f, 0.0f, 0.0f);
    up = glm::cross(direction, right);
}

// One DrawMeshInstances, DrawImpostorInstances or DrawTerrain call.
struct InstanceBatch {
    std::uint32_t mesh_id;
    Material material;
    glm::vec3 noise_shift{0.0f};  // see NoiseShift
    // Not 0: the batch draws patches of this terrain, with this look.
    std::uint32_t terrain_id = 0;
    TerrainMaterial terrain_material;
    // Not 0: the batch draws cards of this impostor (mesh_id is then the card).
    std::uint32_t impostor_id = 0;
    // The batch's transforms as given by the caller, in Impl::instance_sources.
    size_t first_source;
    std::uint32_t count;
    InstanceOptions options;
    // After culling: where each list's surviving transforms start in the frame's instance
    // buffer, and how many there are.
    std::uint32_t list_first[kInstanceListCount];
    std::uint32_t list_count[kInstanceListCount];
};

// True when two materials need the same fragment-shader uniforms and textures, so that a
// draw following another with such a material can skip binding them again.
inline bool SameFragmentBindings(const Material& a, const Material& b) {
    return a.texture.id == b.texture.id && a.normal_texture.id == b.normal_texture.id &&
           a.orm_texture.id == b.orm_texture.id && a.metallic == b.metallic && a.roughness == b.roughness &&
           a.alpha_cutoff == b.alpha_cutoff && a.double_sided == b.double_sided && a.occlusion_in_orm == b.occlusion_in_orm &&
           a.back_faces_keep_normal == b.back_faces_keep_normal && a.normal_up == b.normal_up &&
           a.emissive_texture.id == b.emissive_texture.id && a.emissive.r == b.emissive.r && a.emissive.g == b.emissive.g &&
           a.emissive.b == b.emissive.b && a.emissive_strength == b.emissive_strength;
}

// A plane as (normal, distance): points with dot(normal, p) + distance >= 0 are inside.
// The six sides of the volume a view-projection matrix shows, normals pointing inwards.
struct ViewVolume {
    glm::vec4 planes[6];
    int plane_count;
};

// reversed_depth: true for the camera, whose depth runs from 1 at the near plane to 0 far
// away (see EndFrame); false for shadow cascades, whose depth runs the usual way and whose
// near plane is not checked (see IsBoxOutside).
inline ViewVolume MakeViewVolume(const glm::mat4& view_projection, bool reversed_depth) {
    const auto row = [&view_projection](int i) {
        return glm::vec4(view_projection[0][i], view_projection[1][i], view_projection[2][i], view_projection[3][i]);
    };
    ViewVolume volume;
    volume.planes[0] = row(3) + row(0);  // left
    volume.planes[1] = row(3) - row(0);  // right
    volume.planes[2] = row(3) + row(1);  // bottom
    volume.planes[3] = row(3) - row(1);  // top
    volume.planes[4] = reversed_depth ? row(2) : row(3) - row(2);  // far
    volume.planes[5] = row(3) - row(2);                            // near, reversed depth only
    volume.plane_count = reversed_depth ? 6 : 5;
    for (int i = 0; i < volume.plane_count; ++i) {
        volume.planes[i] /= glm::length(glm::vec3(volume.planes[i]));
    }
    return volume;
}

inline bool IsSphereOutside(const ViewVolume& volume, const glm::vec3& center, float radius) {
    for (int i = 0; i < volume.plane_count; ++i) {
        if (glm::dot(glm::vec3(volume.planes[i]), center) + volume.planes[i].w < -radius) {
            return true;
        }
    }
    return false;
}

// True when the box, after `clip_from_mesh`, lies entirely outside the visible volume: all
// eight corners are beyond the same side of it. A box that is only partly outside, or that
// surrounds the volume, is never reported as outside.
// reversed_depth: true for the camera (depth 1 near, 0 far, near plane checked); false for
// shadow cascades, where geometry nearer to the sun than the cascade still casts shadows
// into it, so only the far plane counts.
inline bool IsBoxOutside(const glm::mat4& clip_from_mesh, const glm::vec3& box_min, const glm::vec3& box_max, bool reversed_depth) {
    int outside[6] = {0, 0, 0, 0, 0, 0};
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec4 position((corner & 1) ? box_max.x : box_min.x, (corner & 2) ? box_max.y : box_min.y,
                                 (corner & 4) ? box_max.z : box_min.z, 1.0f);
        const glm::vec4 clip = clip_from_mesh * position;
        outside[0] += clip.x < -clip.w ? 1 : 0;
        outside[1] += clip.x > clip.w ? 1 : 0;
        outside[2] += clip.y < -clip.w ? 1 : 0;
        outside[3] += clip.y > clip.w ? 1 : 0;
        if (reversed_depth) {
            outside[4] += clip.z < 0.0f ? 1 : 0;       // beyond the far plane
            outside[5] += clip.z > clip.w ? 1 : 0;     // before the near plane
        } else {
            outside[4] += clip.z > clip.w ? 1 : 0;
        }
    }
    for (const int count : outside) {
        if (count == 8) {
            return true;
        }
    }
    return false;
}

[[noreturn]] inline void FatalSdlError(const char* what) {
    const std::string message = std::string("Renderer: ") + what + ": " + SDL_GetError();
    FatalError(message.c_str());
}

inline const char* GetDriverName(GraphicsBackend backend) {
    switch (backend) {
    case GraphicsBackend::Direct3D12:
        return "direct3d12";
    case GraphicsBackend::Vulkan:
        return "vulkan";
    case GraphicsBackend::Default:
        break;
    }
    // Direct3D 12 is the default: its debug layer ships with Windows, so validation works
    // without installing anything. See docs/en/rendering.md.
    return "direct3d12";
}

// Converts one sRGB-encoded color component (0..1) to a linear value.
inline float SrgbToLinear(float encoded) {
    return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

inline Uint32 CountMipLevels(int width, int height) {
    Uint32 levels = 1;
    int size = width > height ? width : height;
    while (size > 1) {
        size /= 2;
        ++levels;
    }
    return levels;
}

inline glm::vec4 ToVec4(Color color) {
    return glm::vec4(color.r, color.g, color.b, color.a);
}

inline SDL_GPUSampleCount ToSampleCount(int samples) {
    switch (samples) {
    case 8:
        return SDL_GPU_SAMPLECOUNT_8;
    case 4:
        return SDL_GPU_SAMPLECOUNT_4;
    case 2:
        return SDL_GPU_SAMPLECOUNT_2;
    default:
        return SDL_GPU_SAMPLECOUNT_1;
    }
}

} // namespace render_internal

using namespace render_internal;

struct Renderer::Impl {
    RendererConfig config;
    PostSettings post;
    bool debug_ui_active = false;
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    int msaa_samples = 1;

    // Render targets. With multisampling the scene is drawn into scene_color (several samples
    // per pixel) and averaged into scene_resolved; without it scene_color is used directly.
    SDL_GPUTexture* scene_color = nullptr;
    SDL_GPUTexture* scene_depth = nullptr;
    SDL_GPUTexture* scene_resolved = nullptr;
    SDL_GPUTexture* output_target = nullptr;
    SDL_GPUTexture* shadow_atlas = nullptr;
    // The scene's depth with one sample per pixel, drawn before the scene so that ambient
    // occlusion and fog can read it.
    SDL_GPUTexture* prepass_depth = nullptr;
    SDL_GPUTexture* ao_target = nullptr;
    SDL_GPUTexture* ao_scratch = nullptr;
    SDL_GPUTexture* fog_target = nullptr;
    int fog_width = 0;
    int fog_height = 0;
    // Size the scene is drawn at (RendererConfig::render_scale); the output may be larger.
    int render_width = 0;
    int render_height = 0;
    std::vector<BloomLevel> bloom_levels;
    // Two one-pixel images, written in turn: each frame reads the previous frame's value.
    SDL_GPUTexture* luminance_targets[2] = {nullptr, nullptr};
    int luminance_index = 0;
    bool has_luminance_history = false;

    SDL_GPUGraphicsPipeline* prepass_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* prepass_pipeline_double_sided = nullptr;
    SDL_GPUGraphicsPipeline* ao_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* ao_blur_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* fog_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* fog_composite_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* bloom_down_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* bloom_up_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* luminance_pipeline = nullptr;
    // Automatic exposure's first half: mean logarithms over 16 x 16 blocks (shared by every view).
    SDL_GPUGraphicsPipeline* luminance_blocks_pipeline = nullptr;
    SDL_GPUTexture* luminance_blocks = nullptr;
    SDL_GPUGraphicsPipeline* taa_pipeline = nullptr;
    // Water: drawn after the scene from copies of the scene's color and depth.
    SDL_GPUGraphicsPipeline* water_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* depth_copy_pipeline = nullptr;
    SDL_GPUTexture* water_scene_copy = nullptr;
    SDL_GPUTexture* water_depth_copy = nullptr;
    std::vector<WaterCommand> water_commands;
    // Trampled plants (Renderer::SetTrampleMap): the map, its copy on the card and which
    // version of it that copy holds; trample_info goes to VertexFrameUniforms::trample.
    const GroundMapSource* trample_source = nullptr;
    SDL_GPUTexture* trample_texture = nullptr;
    int trample_resolution = 0;
    std::uint32_t trample_version = 0xFFFFFFFFu;
    glm::vec4 trample_info{0.0f};
    SDL_GPUTexture* no_trample_texture = nullptr;
    // Clouds: noise volumes (made on the first frame that has clouds), the cloud image, the
    // cloud shadow image over the land around the camera, and their pipelines.
    SDL_GPUTexture* cloud_shape = nullptr;
    SDL_GPUTexture* cloud_detail = nullptr;
    SDL_GPUTexture* cloud_weather = nullptr;
    SDL_GPUTexture* cloud_target = nullptr;
    SDL_GPUTexture* cloud_shadow_target = nullptr;
    SDL_GPUGraphicsPipeline* clouds_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* cloud_shadow_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* clouds_composite_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* clouds_resolve_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* sky_capture_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* sky_prefilter_pipeline = nullptr;
    // The sky light map (RenderSkyLightPass) and the sampler that reads it.
    SDL_GPUTexture* sky_capture = nullptr;
    SDL_GPUTexture* sky_radiance = nullptr;
    SDL_GPUSampler* cube_sampler = nullptr;
    SDL_GPUTexture* dummy_volume = nullptr;  // a 1 x 1 x 1 volume bound for the cloud noise when there are no clouds
    bool sky_radiance_ready = false;
    std::uint64_t sky_light_frame = 0;  // frame of the last step of the cycle
    SDL_GPUSampler* repeat_sampler = nullptr;
    // What the lighting shaders read this frame: the cloud shadow image (or white) and the
    // sky occlusion map (or white).
    SDL_GPUTexture* frame_cloud_shadow = nullptr;
    SDL_GPUTexture* frame_sky_occlusion = nullptr;
    // The sky occlusion map (Renderer::SetSkyOcclusion) and its copy on the card.
    const GroundMapSource* sky_occlusion_source = nullptr;
    GroundMap sky_map;  // this frame's
    SDL_GPUTexture* sky_occlusion_texture = nullptr;
    int sky_occlusion_resolution = 0;
    std::uint32_t sky_occlusion_version = 0xFFFFFFFFu;

    // The two lighting textures every lit surface reads, after its others (lighting.hlsli).
    void AddLightingBindings(SDL_GPUTextureSamplerBinding* bindings) const;
    // The nine textures of a lit mesh's fragment shader (mesh.frag.hlsl) for `material`.
    void FillMeshBindings(const Material& material, bool ao_on, SDL_GPUTextureSamplerBinding* bindings) const;

    SDL_GPUTexture* CreateVolume(int size, const std::vector<std::uint8_t>& bytes);

    void CreateCloudTextures();

    // Temporal anti-aliasing: two images written in turn, each frame reading the other as
    // its history, the previous frame's camera (without its shift), and the frame count
    // that picks the shift and moves the noise patterns from frame to frame.
    SDL_GPUTexture* taa_targets[2] = {nullptr, nullptr};
    // Motion vectors written by the depth pass (kMotionFormat, drawn size).
    SDL_GPUTexture* motion_target = nullptr;
    int taa_index = 0;
    bool has_taa_history = false;
    WindowMode window_mode = WindowMode::Windowed;
    // The previous frame's camera without its shift, relative to that frame's camera position.
    glm::mat4 previous_view_projection{1.0f};
    glm::dvec3 previous_camera_position{0.0};
    std::uint64_t frame_index = 0;
    // Clouds built up over frames (RenderCloudPass): two images written in turn, the size of
    // cloud_target; usable when the previous frame wrote one.
    SDL_GPUTexture* cloud_history[2] = {nullptr, nullptr};
    int cloud_history_index = 0;
    bool cloud_history_valid = false;
    std::uint64_t cloud_history_frame = 0;
    glm::mat4 previous_cloud_view_projection{1.0f};
    // Added to the pixel position of every noise pattern (dithered fades, ambient
    // occlusion, fog steps) while TAA is on, so that the patterns change every frame and
    // TAA averages them out. 0 with TAA off, so single frames stay as they were.
    float temporal_noise_offset = 0.0f;

    // Indexed by pass, by "is the mesh skinned" and by "is the material double-sided".
    SDL_GPUGraphicsPipeline* mesh_pipelines[kMeshPassCount][kMeshKindCount][2] = {};
    // Instancing: the transforms as submitted, the batches, and the culled transforms of
    // all batches and lists one after another, with the buffer they are copied to.
    std::vector<glm::mat4> instance_sources;
    std::vector<InstanceBatch> instance_batches;
    std::vector<glm::mat4> instance_data;
    // Scratch: the sphere around each instance of the batch being culled (center, radius).
    std::vector<glm::vec4> instance_spheres;
    SDL_GPUBuffer* instance_buffer = nullptr;
    Uint32 instance_buffer_capacity = 0;
    // The upload buffer the matrices go through, kept from frame to frame (mapping it with
    // "cycle" gives fresh memory while the card still reads the previous frame's).
    SDL_GPUTransferBuffer* instance_transfer = nullptr;
    Uint32 instance_transfer_capacity = 0;

    std::vector<ImpostorData> impostors;
    // The square every impostor is drawn with; made with the first impostor.
    MeshHandle impostor_card;
    // Scene and depth pass; impostors cast no shadows.
    SDL_GPUGraphicsPipeline* impostor_pipelines[kMeshPassCount] = {};

    std::vector<TerrainData> terrains;
    // The flat grid every terrain patch is drawn with; made with the first terrain.
    MeshHandle terrain_grid;
    SDL_GPUGraphicsPipeline* terrain_pipelines[kMeshPassCount] = {};
    // The joint matrices of every skinned mesh drawn this frame, one after another, and
    // the buffer on the graphics card they are copied to in EndFrame.
    std::vector<glm::mat4> joint_palette;
    // Motion vectors: draws with an object id in this frame and in the previous one, the
    // previous frame's joint matrices, how many draws each id has had this frame, and the
    // previous frame's wind.
    std::unordered_map<DrawnObjectKey, DrawnObject, DrawnObjectKeyHash> drawn_objects;
    std::unordered_map<DrawnObjectKey, DrawnObject, DrawnObjectKeyHash> previous_drawn_objects;
    std::vector<glm::mat4> previous_joint_palette;
    std::unordered_map<std::uint64_t, std::uint32_t> object_draw_counts;
    glm::vec4 previous_wind{0.0f};
    bool has_previous_wind = false;
    // Fills command.previous_* from the previous frame's draw with this id, and remembers this one.
    void MatchPreviousDraw(std::uint64_t object_id, DrawCommand& command, int joint_count);
    SDL_GPUBuffer* joint_buffer = nullptr;
    Uint32 joint_buffer_capacity = 0;
    SDL_GPUTransferBuffer* joint_transfer = nullptr;
    Uint32 joint_transfer_capacity = 0;

    SDL_GPUGraphicsPipeline* mesh_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* mesh_pipeline_double_sided = nullptr;
    SDL_GPUGraphicsPipeline* shadow_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* sky_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* tonemap_pipeline = nullptr;

    SDL_GPUSampler* texture_sampler = nullptr;
    SDL_GPUSampler* shadow_sampler = nullptr;
    SDL_GPUSampler* clamp_sampler = nullptr;
    SDL_GPUSampler* linear_clamp_sampler = nullptr;

    std::vector<Mesh> meshes;
    std::vector<SDL_GPUTexture*> textures;
    // Images in each texture: 0 for a plain texture, 1 or more for a texture array.
    std::vector<int> texture_layers;
    // Width and height of each texture's (first) image.
    std::vector<glm::vec2> texture_sizes;
    // Stand-ins for a terrain without textures or weights.
    TextureHandle white_array_texture;
    TextureHandle flat_normal_array_texture;
    TextureHandle first_layer_weights;  // all weight on layer 0
    TextureHandle no_weights;
    TextureHandle first_layer_weights_array;  // the same as one-layer arrays, for terrains
    TextureHandle no_weights_array;
    // Stand-ins for materials without the respective texture, so that one shader serves all.
    TextureHandle white_texture;
    TextureHandle flat_normal_texture;
    // Clear color as linear values, the form the scene target expects.
    SDL_FColor linear_clear_color{};

    // Recorded between BeginFrame and EndFrame.
    bool in_frame = false;
    // The camera's position this frame: everything is drawn relative to it (subtracted in
    // double, then float). See poligon/core/math.h.
    glm::dvec3 origin{0.0};
    // 2D over the picture: recorded quads, fonts (resources/overlay.cpp, passes/overlay_pass.cpp).
    OverlayState overlay;
    // Decals recorded this frame (resources/decals.cpp, passes/decal_pass.cpp).
    DecalState decals;
    // The sky from the atmosphere model (resources/atmosphere.cpp, passes/atmosphere_pass.cpp).
    AtmosphereState atmosphere;
    // Point and spot lights (resources/lights.cpp, passes/spot_shadow_pass.cpp).
    LightState lights;
    // Occlusion culling by depth (passes/occlusion_pass.cpp, renderer/occlusion.cpp).
    OcclusionState occlusion;
    // Particles recorded this frame (resources/particles.cpp, passes/particle_pass.cpp).
    ParticleState particles;
    // Draws per level of detail this frame (FrameStats::mesh_lod_draws).
    int mesh_lod_draws[kMaxMeshLods] = {};
    bool has_finished_frame = false;
    // Without a window nothing waits for the graphics card (a swapchain does that): fences of
    // the frames still in flight, oldest first, at most kMaxFramesInFlight of them.
    std::deque<std::pair<std::uint64_t, SDL_GPUFence*>> submitted;  // (submission number, its fence)
    std::uint64_t submission_count = 0;  // frames submitted so far, render targets' included
    // Waits until the card has finished every submission up to this number, releasing their fences.
    void WaitForSubmission(std::uint64_t submission);
    // Render targets (CreateRenderTarget), and the one this frame draws into (0 = the window).
    std::vector<RenderTargetData> render_targets;
    std::uint32_t active_target = 0;
    // Exchanges the view the renderer draws with (its fields here) with `view`.
    void SwapView(ViewState& view);
    // Makes the images of the view now in the fields (CreateSizedTargets and the exposure pair).
    void CreateViewTargets();
    void ReleaseViewTargets();
    Camera camera;
    DirectionalLight light;
    Environment environment;
    std::vector<DrawCommand> draw_commands;
    // Draws with blended materials (BlendMode::Blend): drawn only by the blend pass.
    std::vector<DrawCommand> blended_commands;
    // Adds a draw to draw_commands or, blended, to blended_commands.
    void AddDrawCommand(const DrawCommand& command);
    // Indices into draw_commands of what survives culling, rebuilt every frame.
    std::vector<std::uint32_t> visible_commands;
    std::vector<std::uint32_t> cascade_commands[kCascadeCount];
    FrameStats stats;

    SDL_GPUShader* CreateShader(SDL_GPUShaderStage stage, Uint32 sampler_count, Uint32 uniform_buffer_count,
                                const unsigned char* dxil, size_t dxil_size, const unsigned char* spirv,
                                size_t spirv_size, Uint32 storage_buffer_count = 0);

    SDL_GPUTexture* CreateTarget(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int width, int height,
                                 SDL_GPUSampleCount samples, const SDL_FColor* clear_color, const char* what,
                                 float clear_depth = 1.0f);

    void CreateTargets();

    // The images drawn at the output's size or the drawing size (render_scale).
    void CreateSizedTargets();

    // The images whose size does not depend on the output: shadows, cloud shadows, the
    // measured brightness for automatic exposure.
    void CreateFixedTargets();

    // Releases what CreateSizedTargets made, so that it can be made again at another size.
    void ReleaseSizedTargets();

    void CreateSamplers();

    // A pipeline for a pass that covers the whole target with one triangle (fullscreen.vert.hlsl).
    // Also for passes whose vertex shader makes its own geometry (overlay quads, decal boxes,
    // particle cards); `cull` leaves out front or back faces.
    SDL_GPUGraphicsPipeline* CreateFullscreenPipeline(SDL_GPUShader* vertex_shader, SDL_GPUShader* fragment_shader,
                                                      SDL_GPUTextureFormat format, FullscreenBlend blend,
                                                      SDL_GPUCullMode cull = SDL_GPU_CULLMODE_NONE);

    // Draws one full-screen pass into `target`. keep_target: blend with what the target
    // holds instead of overwriting it.
    void DrawFullscreenPass(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target, bool keep_target,
                            SDL_GPUGraphicsPipeline* pipeline, const SDL_GPUTextureSamplerBinding* bindings,
                            Uint32 binding_count, const void* uniforms, Uint32 uniform_size);

    void CreatePipelines();

    // Copies a square of single bytes into a one-channel texture of that size.
    void UploadR8(SDL_GPUTexture* texture, const std::uint8_t* values, int resolution);
    // Copies `width` x `height` pixels of `bytes_per_pixel` bytes into one layer of a texture,
    // submitted at once in a command buffer of its own (outside the frame's).
    void UploadTextureLayer(SDL_GPUTexture* texture, Uint32 layer, const void* pixels, int width, int height, int bytes_per_pixel);
    // A terrain's weight arrays for drawing with `material`: a terrain of one map copies the
    // material's weight images into its arrays when they changed.
    void PrepareTerrainWeights(TerrainData& terrain, const TerrainMaterial& material);
    // Fills one page of a terrain's height array (16-bit, between the page's lowest and highest point).
    void SetTerrainHeights(TerrainData& terrain, int page, const float* heights, const glm::dvec3& first_point);

    // Data reaches the GPU through a transfer buffer: CPU-visible memory that a copy pass reads from.
    SDL_GPUTransferBuffer* CreateFilledTransferBuffer(const void* data, Uint32 size);

    SDL_GPUBuffer* CreateBufferWithData(SDL_GPUBufferUsageFlags usage, const void* data, Uint32 size);

    SDL_GPUTexture* GetTexture(TextureHandle handle, TextureHandle fallback) const;

    // Matrices of the shadow cascades for this frame.
    //
    // The visible distance up to shadow_distance is cut into four ranges, near to far. Each
    // gets its own shadow image seen from the sun, so near shadows are sharp and far ones
    // coarse. light_view_projection[i] renders cascade i; shadow_matrix[i] takes a world
    // position to coordinates inside that cascade's tile of the shared shadow image.
    struct Cascades {
        glm::mat4 light_view_projection[kCascadeCount];
        glm::mat4 shadow_matrix[kCascadeCount];
        glm::vec4 far_distance;
        // Size in meters of one shadow image pixel.
        glm::vec4 texel_size;
        // Share of its tile's width and height each cascade uses (see far_shadow_scale).
        glm::vec4 tile_scale{1.0f};
        // Meters across each cascade's depth range (towards the sun).
        glm::vec4 depth_range{1.0f};
    };

    float CascadeTileScale(int cascade) const;

    Cascades ComputeCascades() const;

    void DrawCommandGeometry(SDL_GPURenderPass* pass, const DrawCommand& command);

    static ObjectUniforms MakeObjectUniforms(const DrawCommand& command);

    // The frame's wind as the vertex shaders read it (see ApplyWind in common.hlsli).
    glm::vec4 WindUniform() const;

    // Binds the pipeline a draw command needs in the given pass, unless it is bound already.
    void BindMeshPipeline(SDL_GPURenderPass* pass, MeshPass mesh_pass, const DrawCommand& command,
                          SDL_GPUGraphicsPipeline*& bound_pipeline);

    // Which batches a call to DrawInstanceBatches draws, by Material::casts_ambient_occlusion
    // (terrain and impostors always cast it).
    enum AoFilter { kAllBatches, kAoCasters, kAoNonCasters };

    // The depth-only drawing of the depth pass (pass 2), limited by `filter`.
    void DrawDepthGeometry(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, AoFilter filter);

    // True when something visible this frame casts no ambient occlusion.
    bool HasAoNonCasters() const;

    // Copies this frame's joint matrices to the graphics card.
    void UploadJointMatrices(SDL_GPUCommandBuffer* commands);

    // Draws, in one pass, every instance batch that has something in the given list.
    void DrawInstanceBatches(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, MeshPass mesh_pass, int list,
                             bool ao_on, SDL_GPUGraphicsPipeline*& bound_pipeline, AoFilter filter = kAllBatches);

    void DrawTerrainBatch(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, MeshPass mesh_pass, int list, bool ao_on,
                          const InstanceBatch& batch, SDL_GPUGraphicsPipeline*& bound_pipeline);

    void DrawImpostorBatch(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, MeshPass mesh_pass, int list, bool ao_on,
                           const InstanceBatch& batch, SDL_GPUGraphicsPipeline*& bound_pipeline);

    // Decides, for every instance of every batch, which lists it is in (the camera's view,
    // each shadow cascade), and writes the transforms of each list one after another.
    // Instances that passed culling in one list, with their sort key (see CullInstances).
    std::vector<std::pair<float, std::uint32_t>> sorted_instances;

    void CullInstances(const glm::mat4& view_projection, const Cascades* cascades);

    // Copies a list of matrices into a buffer that vertex shaders read, growing it as needed,
    // through an upload buffer that is kept between frames.
    void UploadMatrices(SDL_GPUCommandBuffer* commands, const std::vector<glm::mat4>& matrices, SDL_GPUBuffer*& buffer,
                        Uint32& capacity, SDL_GPUTransferBuffer*& transfer, Uint32& transfer_capacity);

    // factors.w carries the frame's noise offset (see temporal_noise_offset).
    static MaterialUniforms MakeMaterialUniforms(const Material& material, float noise_offset = 0.0f);

    // What the passes of one frame share: made at the start of Renderer::EndFrame, then handed
    // to each pass (engine/src/render/passes) in turn.
    struct FrameContext {
        SDL_GPUCommandBuffer* commands = nullptr;
        // The camera: shifted by temporal anti-aliasing's sub-pixel step, and not.
        glm::mat4 projection{1.0f};
        glm::mat4 view_projection{1.0f};
        glm::mat4 unshifted_view_projection{1.0f};
        // The previous frame's unshifted camera, taking positions relative to this frame's
        // camera (motion vectors, reprojection).
        glm::mat4 previous_view_projection{1.0f};
        // The camera's world position in float, and the view-projection in world float
        // coordinates: for clouds and the sky capture, which walk world-fixed noise
        // kilometers wide and need no finer precision.
        glm::vec3 camera_world{0.0f};
        glm::mat4 world_view_projection{1.0f};
        glm::vec2 jitter_pixels{0.0f};
        glm::vec3 light_direction{0.0f, -1.0f, 0.0f};
        // What runs this frame.
        bool taa_on = false;
        bool shadows_on = false;
        bool ao_on = false;
        bool fog_on = false;
        bool clouds_on = false;
        bool scene_after_depth = false;
        bool water_on = false;
        bool bloom_on = false;
        // The size the scene is drawn at, and the output's.
        float width = 0.0f;
        float height = 0.0f;
        float output_width = 0.0f;
        float output_height = 0.0f;
        glm::vec4 projection_info{0.0f};
        // Made by the shadow pass.
        Cascades cascades{};
        // Cloud shadows cover this square around the camera.
        CloudUniforms cloud_uniforms{};
        float cloud_shadow_size = 8000.0f;
        glm::dvec2 cloud_shadow_corner{0.0};  // world x and z
        // Filled by the scene pass; the water pass uses it again.
        FragmentFrameUniforms frame_uniforms{};
        // The scene's image with one sample per pixel; temporal anti-aliasing replaces it with its result.
        SDL_GPUTexture* scene_image = nullptr;
        SDL_GPUTexture* white = nullptr;
    };
    // The passes of a frame, in the order EndFrame runs them; each in engine/src/render/passes.
    void RenderShadowPass(FrameContext& frame);
    void RenderCloudShadowPass(FrameContext& frame);
    void RenderSkyLightPass(FrameContext& frame);
    // Draws a full-screen pass into one face (0..5) and level of a cube map.
    void DrawFullscreenToCube(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target, int face, int level,
                              SDL_GPUGraphicsPipeline* pipeline, const SDL_GPUTextureSamplerBinding* bindings, Uint32 binding_count,
                              const void* uniforms0, Uint32 size0, const void* uniforms1 = nullptr, Uint32 size1 = 0);
    void RenderDepthPass(FrameContext& frame);
    void RenderScenePass(FrameContext& frame);
    void RenderWaterPass(FrameContext& frame);
    void RenderCloudPass(FrameContext& frame);
    void RenderFogPass(FrameContext& frame);
    void RenderAtmospherePass(FrameContext& frame);
    void RenderSpotShadowPass(FrameContext& frame);
    void RenderOcclusionPass(FrameContext& frame);
    // Reads the newest finished copy of an earlier frame's shrunk depth and moves it onto this
    // camera (unshifted); before anything is culled.
    void PrepareCoverage(const glm::mat4& view_projection);
    // A sphere (relative to the camera) wholly behind the coverage.
    bool IsOccluded(const glm::vec3& center, float radius) const;
    void ReleaseOcclusion();
    // Keeps the lights that reach the view (the nearest kMaxLights), assigns them to screen
    // tiles and places the spot light shadows; before the instances are culled (EndFrame).
    void PrepareLights(const glm::mat4& view_projection);
    // Copies LightData to the graphics card (outside any render pass).
    void UploadLights(SDL_GPUCommandBuffer* commands);
    // LightUniforms as fragment uniform buffer 3 and LightData as fragment storage buffer 0,
    // for a pass drawing into an image of this size.
    void PushLightUniforms(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, float width, float height);
    void ReleaseLights();
    // With SkyModel::Atmosphere, sets the frame's sun and sky colors from the model (BeginFrame).
    void ApplyAtmosphere();
    void ReleaseAtmosphere();
    // The textures the sky shaders bind for the atmosphere (white stand-ins when it is off).
    SDL_GPUTexture* AtmosphereSkyView() const;
    SDL_GPUTexture* AtmosphereTransmittance() const;
    void RenderBlendPass(FrameContext& frame);
    void RenderTemporalAaPass(FrameContext& frame);
    void RenderBloomPass(FrameContext& frame);
    void RenderOutputPass(FrameContext& frame);
    void RenderOverlayPass(FrameContext& frame);
    void RenderDecalPass(FrameContext& frame);
    void CreateDecalPipeline();
    void ReleaseDecals();
    void RenderParticlePass(FrameContext& frame);
    void CreateParticlePipelines();
    void ReleaseParticles();

    // 2D drawing (resources/overlay.cpp).
    void CreateOverlayPipeline();
    void ReleaseOverlay();
    void AddOverlayQuads(SDL_GPUTexture* texture, bool pixelated, const OverlayQuad* quads, size_t count);
    // Instance matrices relative to `source_origin`, added to instance_sources relative to the camera.
    void AddInstanceSources(const glm::dvec3& source_origin, const glm::mat4* transforms, int count);
    const Font* FindFont(FontHandle handle) const;
};

} // namespace poligon
