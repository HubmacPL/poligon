#pragma once

// Private to the renderer (engine/src/render): the uniform layouts the shaders read, the
// renderer's internal types and its state, Renderer::Impl. The public API is in
// poligon/render/renderer.h. Renderer::Impl's functions are spread by subject:
//   renderer/   the public API and the frame (renderer.cpp), render targets and samplers
//               (targets.cpp), shaders and pipelines (pipelines.cpp), drawing inside a pass
//               and culling instances (drawing.cpp)
//   resources/  what lives on the graphics card: meshes, textures, terrain, impostors,
//               uploads, the cloud noise volumes
//   passes/     one file per pass of a frame, in the order EndFrame runs them: shadows, cloud
//               shadows, depth and ambient occlusion, scene, water, clouds, fog, temporal
//               anti-aliasing, bloom and exposure, output (tone mapping and debug windows)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "poligon/core/fatal_error.h"
#include "poligon/core/texture_compression.h"
#include "poligon/render/renderer.h"

namespace poligon {

// Everything below is used only by the renderer's own files.
namespace render_internal {

constexpr int kCascadeCount = 4;

// Uniform layouts. Each must match the cbuffer of the same name in engine/src/render/shaders.
struct VertexFrameUniforms {
    glm::mat4 view_projection;
    glm::vec4 wind{0.0f};
    glm::vec4 trample{0.0f};  // xy: corner of the trample map, z: 1 / its size, w: 1 = on
};

struct ObjectUniforms {
    glm::mat4 model;
    glm::vec4 color;
    glm::vec4 uv_scale;
    glm::vec4 wind_response{0.0f};
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
};

// One water surface to draw (Renderer::DrawWater).
struct WaterCommand {
    std::uint32_t mesh_id = 0;
    glm::mat4 transform{1.0f};
    WaterMaterial material;
};

struct TerrainMaterialUniforms {
    glm::vec4 map;
    glm::vec4 map_heights;
    glm::vec4 steep;
    glm::vec4 steep_color;
    glm::vec4 tile_scales[2];
    glm::vec4 weight_map;
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
constexpr SDL_GPUTextureFormat kLuminanceFormat = SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
// Terrain heights as 16-bit values between the lowest and the highest point of the map:
// 65535 steps, which is millimeters for hills. Unlike a float format it can always be
// read with blending between points.
constexpr SDL_GPUTextureFormat kHeightFormat = SDL_GPU_TEXTUREFORMAT_R16_UNORM;
// Cells along one edge of the grid mesh that every terrain patch is drawn with.
constexpr int kTerrainGridCells = 64;

// The fog is walked at most this far (meters) along each line of sight.
constexpr float kFogWalkDistance = 1000.0f;
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
};

// The three passes that draw meshes.
// kSceneAfterDepthPass: the scene pass when the depth pass has already drawn the final
// depth; it tests against that depth before shading and writes none (see EndFrame).
enum MeshPass { kScenePass, kShadowPass, kDepthPass, kSceneAfterDepthPass, kMeshPassCount };

inline bool IsScenePass(MeshPass pass) {
    return pass == kScenePass || pass == kSceneAfterDepthPass;
}
// How a mesh's vertices are placed: by one transform, by a skeleton, or once per instance.
enum MeshKind { kRigid, kSkinned, kInstanced, kMeshKindCount };

// The lists of instances a batch is culled into: the camera's view, then one per cascade.
constexpr int kInstanceListCount = 1 + kCascadeCount;

// A height map on the graphics card.
struct TerrainData {
    SDL_GPUTexture* height_texture = nullptr;
    int width = 0;
    int depth = 0;
    float spacing = 1.0f;
    glm::vec3 origin{0.0f};  // world position of the first point; y is that of image value 0
    float height_range = 1.0f;
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
           a.back_faces_keep_normal == b.back_faces_keep_normal && a.normal_up == b.normal_up;
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

    SDL_GPUTexture* CreateVolume(int size, const std::vector<std::uint8_t>& bytes);

    void CreateCloudTextures();

    // Temporal anti-aliasing: two images written in turn, each frame reading the other as
    // its history, the previous frame's camera (without its shift), and the frame count
    // that picks the shift and moves the noise patterns from frame to frame.
    SDL_GPUTexture* taa_targets[2] = {nullptr, nullptr};
    int taa_index = 0;
    bool has_taa_history = false;
    WindowMode window_mode = WindowMode::Windowed;
    glm::mat4 previous_view_projection{1.0f};
    glm::vec3 previous_camera_position{0.0f};
    std::uint64_t frame_index = 0;
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
    // Stand-ins for materials without the respective texture, so that one shader serves all.
    TextureHandle white_texture;
    TextureHandle flat_normal_texture;
    // Clear color as linear values, the form the scene target expects.
    SDL_FColor linear_clear_color{};

    // Recorded between BeginFrame and EndFrame.
    bool in_frame = false;
    bool has_finished_frame = false;
    Camera camera;
    DirectionalLight light;
    Environment environment;
    std::vector<DrawCommand> draw_commands;
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
    SDL_GPUGraphicsPipeline* CreateFullscreenPipeline(SDL_GPUShader* vertex_shader, SDL_GPUShader* fragment_shader,
                                                      SDL_GPUTextureFormat format, FullscreenBlend blend);

    // Draws one full-screen pass into `target`. keep_target: blend with what the target
    // holds instead of overwriting it.
    void DrawFullscreenPass(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target, bool keep_target,
                            SDL_GPUGraphicsPipeline* pipeline, const SDL_GPUTextureSamplerBinding* bindings,
                            Uint32 binding_count, const void* uniforms, Uint32 uniform_size);

    void CreatePipelines();

    // Copies a square of single bytes into a one-channel texture of that size.
    void UploadR8(SDL_GPUTexture* texture, const std::uint8_t* values, int resolution);

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
        glm::vec2 cloud_shadow_corner{0.0f};
        // Filled by the scene pass; the water pass uses it again.
        FragmentFrameUniforms frame_uniforms{};
        // The scene's image with one sample per pixel; temporal anti-aliasing replaces it with its result.
        SDL_GPUTexture* scene_image = nullptr;
        SDL_GPUTexture* white = nullptr;
    };
    // The passes of a frame, in the order EndFrame runs them; each in engine/src/render/passes.
    void RenderShadowPass(FrameContext& frame);
    void RenderCloudShadowPass(FrameContext& frame);
    void RenderDepthPass(FrameContext& frame);
    void RenderScenePass(FrameContext& frame);
    void RenderWaterPass(FrameContext& frame);
    void RenderCloudPass(FrameContext& frame);
    void RenderFogPass(FrameContext& frame);
    void RenderTemporalAaPass(FrameContext& frame);
    void RenderBloomPass(FrameContext& frame);
    void RenderOutputPass(FrameContext& frame);
};

} // namespace poligon
