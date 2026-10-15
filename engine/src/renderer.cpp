#include "poligon/renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include "poligon/fatal_error.h"
#include "poligon/texture_compression.h"
#include "poligon/sky_occlusion.h"
#include "poligon/trample.h"
#include "cloud_noise.h"
#include "poligon_shaders/ao_blur_frag_dxil.h"
#include "poligon_shaders/ao_blur_frag_spirv.h"
#include "poligon_shaders/ao_frag_dxil.h"
#include "poligon_shaders/ao_frag_spirv.h"
#include "poligon_shaders/bloom_down_frag_dxil.h"
#include "poligon_shaders/bloom_down_frag_spirv.h"
#include "poligon_shaders/bloom_up_frag_dxil.h"
#include "poligon_shaders/bloom_up_frag_spirv.h"
#include "poligon_shaders/fog_composite_frag_dxil.h"
#include "poligon_shaders/fog_composite_frag_spirv.h"
#include "poligon_shaders/fog_frag_dxil.h"
#include "poligon_shaders/fog_frag_spirv.h"
#include "poligon_shaders/fullscreen_vert_dxil.h"
#include "poligon_shaders/fullscreen_vert_spirv.h"
#include "poligon_shaders/impostor_after_depth_frag_dxil.h"
#include "poligon_shaders/impostor_after_depth_frag_spirv.h"
#include "poligon_shaders/impostor_bake_frag_dxil.h"
#include "poligon_shaders/impostor_bake_frag_spirv.h"
#include "poligon_shaders/impostor_depth_frag_dxil.h"
#include "poligon_shaders/impostor_depth_frag_spirv.h"
#include "poligon_shaders/impostor_frag_dxil.h"
#include "poligon_shaders/impostor_frag_spirv.h"
#include "poligon_shaders/impostor_vert_dxil.h"
#include "poligon_shaders/impostor_vert_spirv.h"
#include "poligon_shaders/luminance_frag_dxil.h"
#include "poligon_shaders/luminance_frag_spirv.h"
#include "poligon_shaders/mesh_after_depth_frag_dxil.h"
#include "poligon_shaders/mesh_after_depth_frag_spirv.h"
#include "poligon_shaders/mesh_frag_dxil.h"
#include "poligon_shaders/mesh_frag_spirv.h"
#include "poligon_shaders/mesh_instanced_vert_dxil.h"
#include "poligon_shaders/mesh_instanced_vert_spirv.h"
#include "poligon_shaders/mesh_skinned_vert_dxil.h"
#include "poligon_shaders/mesh_skinned_vert_spirv.h"
#include "poligon_shaders/mesh_vert_dxil.h"
#include "poligon_shaders/mesh_vert_spirv.h"
#include "poligon_shaders/shadow_frag_dxil.h"
#include "poligon_shaders/shadow_frag_spirv.h"
#include "poligon_shaders/taa_frag_dxil.h"
#include "poligon_shaders/taa_frag_spirv.h"
#include "poligon_shaders/sky_frag_dxil.h"
#include "poligon_shaders/sky_frag_spirv.h"
#include "poligon_shaders/terrain_frag_dxil.h"
#include "poligon_shaders/terrain_frag_spirv.h"
#include "poligon_shaders/terrain_vert_dxil.h"
#include "poligon_shaders/terrain_vert_spirv.h"
#include "poligon_shaders/tonemap_frag_dxil.h"
#include "poligon_shaders/tonemap_frag_spirv.h"
#include "poligon_shaders/clouds_frag_dxil.h"
#include "poligon_shaders/clouds_frag_spirv.h"
#include "poligon_shaders/cloud_shadow_frag_dxil.h"
#include "poligon_shaders/cloud_shadow_frag_spirv.h"
#include "poligon_shaders/clouds_composite_frag_dxil.h"
#include "poligon_shaders/clouds_composite_frag_spirv.h"
#include "poligon_shaders/water_frag_dxil.h"
#include "poligon_shaders/water_frag_spirv.h"
#include "poligon_shaders/depth_copy_frag_dxil.h"
#include "poligon_shaders/depth_copy_frag_spirv.h"

namespace poligon {

namespace {

constexpr int kCascadeCount = 4;

// Uniform layouts. Each must match the cbuffer of the same name in engine/shaders/*.hlsl.
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
};

struct MaterialUniforms {
    glm::vec4 factors;
    glm::vec4 flags;
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
float Halton(int index, int base) {
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

bool IsScenePass(MeshPass pass) {
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
glm::vec3 HemiOctahedronDirection(glm::vec2 grid) {
    glm::vec3 direction((grid.x - grid.y) * 0.5f, 0.0f, (grid.x + grid.y) * 0.5f);
    direction.y = 1.0f - std::fabs(direction.x) - std::fabs(direction.z);
    return glm::normalize(direction);
}

void ImpostorFrameAxes(const glm::vec3& direction, glm::vec3& right, glm::vec3& up) {
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
bool SameFragmentBindings(const Material& a, const Material& b) {
    return a.texture.id == b.texture.id && a.normal_texture.id == b.normal_texture.id &&
           a.orm_texture.id == b.orm_texture.id && a.metallic == b.metallic && a.roughness == b.roughness &&
           a.alpha_cutoff == b.alpha_cutoff && a.double_sided == b.double_sided && a.occlusion_in_orm == b.occlusion_in_orm &&
           a.back_faces_keep_normal == b.back_faces_keep_normal;
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
ViewVolume MakeViewVolume(const glm::mat4& view_projection, bool reversed_depth) {
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

bool IsSphereOutside(const ViewVolume& volume, const glm::vec3& center, float radius) {
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
bool IsBoxOutside(const glm::mat4& clip_from_mesh, const glm::vec3& box_min, const glm::vec3& box_max, bool reversed_depth) {
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

[[noreturn]] void FatalSdlError(const char* what) {
    const std::string message = std::string("Renderer: ") + what + ": " + SDL_GetError();
    FatalError(message.c_str());
}

const char* GetDriverName(GraphicsBackend backend) {
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
float SrgbToLinear(float encoded) {
    return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

Uint32 CountMipLevels(int width, int height) {
    Uint32 levels = 1;
    int size = width > height ? width : height;
    while (size > 1) {
        size /= 2;
        ++levels;
    }
    return levels;
}

glm::vec4 ToVec4(Color color) {
    return glm::vec4(color.r, color.g, color.b, color.a);
}

SDL_GPUSampleCount ToSampleCount(int samples) {
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

} // namespace

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
    const TrampleMap* trample_map = nullptr;
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
    const SkyOcclusionMap* sky_occlusion_map = nullptr;
    SDL_GPUTexture* sky_occlusion_texture = nullptr;
    int sky_occlusion_resolution = 0;
    std::uint32_t sky_occlusion_version = 0xFFFFFFFFu;

    // The two lighting textures every lit surface reads, after its others (lighting.hlsli).
    void AddLightingBindings(SDL_GPUTextureSamplerBinding* bindings) const {
        bindings[0].texture = frame_cloud_shadow;
        bindings[0].sampler = linear_clamp_sampler;
        bindings[1].texture = frame_sky_occlusion;
        bindings[1].sampler = linear_clamp_sampler;
    }

    SDL_GPUTexture* CreateVolume(int size, const std::vector<std::uint8_t>& bytes) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_3D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = static_cast<Uint32>(size);
        info.height = static_cast<Uint32>(size);
        info.layer_count_or_depth = static_cast<Uint32>(size);
        info.num_levels = 1;
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &info);
        if (texture == nullptr) {
            FatalSdlError("cannot create cloud noise volume");
        }
        SDL_GPUTransferBuffer* transfer = CreateFilledTransferBuffer(bytes.data(), static_cast<Uint32>(bytes.size()));
        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.pixels_per_row = static_cast<Uint32>(size);
        source.rows_per_layer = static_cast<Uint32>(size);
        SDL_GPUTextureRegion destination{};
        destination.texture = texture;
        destination.w = static_cast<Uint32>(size);
        destination.h = static_cast<Uint32>(size);
        destination.d = static_cast<Uint32>(size);
        SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
        SDL_EndGPUCopyPass(copy_pass);
        if (!SDL_SubmitGPUCommandBuffer(commands)) {
            FatalSdlError("cannot submit cloud noise upload");
        }
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return texture;
    }

    void CreateCloudTextures() {
        if (cloud_shape != nullptr) {
            return;
        }
        cloud_shape = CreateVolume(64, MakeCloudShapeNoise(64, 1u));
        cloud_detail = CreateVolume(32, MakeCloudDetailNoise(32, 2u));
        const std::vector<std::uint8_t> weather = MakeCloudWeatherMap(256, 3u);
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = 256;
        info.height = 256;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        cloud_weather = SDL_CreateGPUTexture(device, &info);
        if (cloud_weather == nullptr) {
            FatalSdlError("cannot create cloud weather map");
        }
        SDL_GPUTransferBuffer* transfer = CreateFilledTransferBuffer(weather.data(), static_cast<Uint32>(weather.size()));
        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.pixels_per_row = 256;
        source.rows_per_layer = 256;
        SDL_GPUTextureRegion destination{};
        destination.texture = cloud_weather;
        destination.w = 256;
        destination.h = 256;
        destination.d = 1;
        SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
        SDL_EndGPUCopyPass(copy_pass);
        if (!SDL_SubmitGPUCommandBuffer(commands)) {
            FatalSdlError("cannot submit cloud weather upload");
        }
        SDL_ReleaseGPUTransferBuffer(device, transfer);
    }

    // Temporal anti-aliasing: two images written in turn, each frame reading the other as
    // its history, the previous frame's camera (without its shift), and the frame count
    // that picks the shift and moves the noise patterns from frame to frame.
    SDL_GPUTexture* taa_targets[2] = {nullptr, nullptr};
    int taa_index = 0;
    bool has_taa_history = false;
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
                                size_t spirv_size, Uint32 storage_buffer_count = 0) {
        const SDL_GPUShaderFormat supported_formats = SDL_GetGPUShaderFormats(device);

        SDL_GPUShaderCreateInfo info{};
        if (supported_formats & SDL_GPU_SHADERFORMAT_DXIL) {
            info.format = SDL_GPU_SHADERFORMAT_DXIL;
            info.code = dxil;
            info.code_size = dxil_size;
        } else if (supported_formats & SDL_GPU_SHADERFORMAT_SPIRV) {
            info.format = SDL_GPU_SHADERFORMAT_SPIRV;
            info.code = spirv;
            info.code_size = spirv_size;
        } else {
            FatalError("Renderer: the graphics device accepts neither DXIL nor SPIR-V shaders");
        }
        info.entrypoint = "main";
        info.stage = stage;
        info.num_samplers = sampler_count;
        info.num_uniform_buffers = uniform_buffer_count;
        info.num_storage_buffers = storage_buffer_count;

        SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
        if (shader == nullptr) {
            FatalSdlError("cannot create shader");
        }
        return shader;
    }

    SDL_GPUTexture* CreateTarget(SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage, int width, int height,
                                 SDL_GPUSampleCount samples, const SDL_FColor* clear_color, const char* what,
                                 float clear_depth = 1.0f) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = format;
        info.usage = usage;
        info.width = static_cast<Uint32>(width);
        info.height = static_cast<Uint32>(height);
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = samples;
        // Direct3D 12 clears fastest (and without debug layer warnings) when the clear values
        // are declared at creation. Other backends ignore these properties.
        const SDL_PropertiesID properties = SDL_CreateProperties();
        if (clear_color != nullptr) {
            SDL_SetFloatProperty(properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_R_FLOAT, clear_color->r);
            SDL_SetFloatProperty(properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_G_FLOAT, clear_color->g);
            SDL_SetFloatProperty(properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_B_FLOAT, clear_color->b);
            SDL_SetFloatProperty(properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_A_FLOAT, clear_color->a);
        }
        SDL_SetFloatProperty(properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_DEPTH_FLOAT, clear_depth);
        info.props = properties;
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &info);
        SDL_DestroyProperties(properties);
        if (texture == nullptr) {
            const std::string message = std::string("cannot create ") + what;
            FatalSdlError(message.c_str());
        }
        return texture;
    }

    void CreateTargets() {
        // Use the asked multisample count, or the nearest lower one the card supports.
        msaa_samples = config.msaa_samples >= 8 ? 8 : config.msaa_samples >= 4 ? 4 : config.msaa_samples >= 2 ? 2 : 1;
        while (msaa_samples > 1 &&
               (!SDL_GPUTextureSupportsSampleCount(device, kSceneFormat, ToSampleCount(msaa_samples)) ||
                !SDL_GPUTextureSupportsSampleCount(device, kDepthFormat, ToSampleCount(msaa_samples)))) {
            msaa_samples /= 2;
        }
        const SDL_GPUSampleCount samples = ToSampleCount(msaa_samples);
        const float scale = msaa_samples == 1 ? glm::clamp(config.render_scale, 0.25f, 1.0f) : 1.0f;
        render_width = glm::max(16, static_cast<int>(std::lround(static_cast<float>(config.width) * scale)));
        render_height = glm::max(16, static_cast<int>(std::lround(static_cast<float>(config.height) * scale)));
        // Everything drawn from the scene is at the drawing size; from temporal
        // anti-aliasing on (bloom, tone mapping, the output) at the output size.
        const int width = render_width;
        const int height = render_height;

        if (msaa_samples > 1) {
            scene_color = CreateTarget(kSceneFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, width, height, samples,
                                       &linear_clear_color, "multisampled scene target");
            scene_resolved = CreateTarget(kSceneFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                          width, height, SDL_GPU_SAMPLECOUNT_1, &linear_clear_color, "resolved scene target");
        } else {
            scene_color = CreateTarget(kSceneFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, width,
                                       height, SDL_GPU_SAMPLECOUNT_1, &linear_clear_color, "scene target");
        }
        // The camera's depth runs from 1 at the near plane to 0 far away ("reversed depth"):
        // a floating-point depth image then keeps millimeters of precision kilometers away,
        // where the usual direction has meters and distant land and trees fight over pixels.
        scene_depth = CreateTarget(kDepthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, width, height, samples, nullptr,
                                   "scene depth target", 0.0f);
        // SAMPLER is needed because the image is copied (blitted) to the window.
        output_target = CreateTarget(kOutputFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                     config.width, config.height, SDL_GPU_SAMPLECOUNT_1, nullptr, "output target");
        // The four shadow cascades share one depth image, as 2 x 2 tiles.
        shadow_atlas = CreateTarget(kDepthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                    config.shadow_map_size * 2, config.shadow_map_size * 2, SDL_GPU_SAMPLECOUNT_1,
                                    nullptr, "shadow image");

        const SDL_GPUTextureUsageFlags drawn_and_read = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        prepass_depth = CreateTarget(kDepthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                     width, height, SDL_GPU_SAMPLECOUNT_1, nullptr, "depth image", 0.0f);
        ao_target = CreateTarget(kAoFormat, drawn_and_read, width, height, SDL_GPU_SAMPLECOUNT_1, nullptr,
                                 "ambient occlusion image");
        ao_scratch = CreateTarget(kAoFormat, drawn_and_read, width, height, SDL_GPU_SAMPLECOUNT_1, nullptr,
                                  "ambient occlusion image");
        fog_width = (width + 1) / 2;
        fog_height = (height + 1) / 2;
        fog_target = CreateTarget(kSceneFormat, drawn_and_read, fog_width, fog_height, SDL_GPU_SAMPLECOUNT_1, nullptr,
                                  "fog image");
        for (SDL_GPUTexture*& target : taa_targets) {
            target = CreateTarget(kSceneFormat, drawn_and_read, config.width, config.height, SDL_GPU_SAMPLECOUNT_1, nullptr,
                                  "temporal anti-aliasing image");
        }
        water_scene_copy = CreateTarget(kSceneFormat, drawn_and_read, width, height, SDL_GPU_SAMPLECOUNT_1, nullptr,
                                        "copy of the scene for water");
        const float cloud_scale = glm::clamp(config.cloud_resolution, 0.125f, 1.0f);
        cloud_target = CreateTarget(kSceneFormat, drawn_and_read, glm::max(8, static_cast<int>(static_cast<float>(width) * cloud_scale)),
                                    glm::max(8, static_cast<int>(static_cast<float>(height) * cloud_scale)), SDL_GPU_SAMPLECOUNT_1,
                                    nullptr, "cloud image");
        cloud_shadow_target = CreateTarget(SDL_GPU_TEXTUREFORMAT_R8_UNORM, drawn_and_read, 256, 256, SDL_GPU_SAMPLECOUNT_1, nullptr,
                                           "cloud shadow image");
        water_depth_copy = CreateTarget(SDL_GPU_TEXTUREFORMAT_R32_FLOAT, drawn_and_read, width, height, SDL_GPU_SAMPLECOUNT_1,
                                        nullptr, "copy of the depth for water");

        // Bloom chain: each image half the size of the one before, down to a few pixels.
        int level_width = config.width;
        int level_height = config.height;
        do {
            level_width = level_width > 1 ? level_width / 2 : 1;
            level_height = level_height > 1 ? level_height / 2 : 1;
            BloomLevel level;
            level.width = level_width;
            level.height = level_height;
            level.texture = CreateTarget(kSceneFormat, drawn_and_read, level_width, level_height, SDL_GPU_SAMPLECOUNT_1,
                                         nullptr, "bloom image");
            bloom_levels.push_back(level);
        } while (static_cast<int>(bloom_levels.size()) < kMaxBloomLevels && level_width >= 16 && level_height >= 16);

        for (SDL_GPUTexture*& target : luminance_targets) {
            target = CreateTarget(kLuminanceFormat, drawn_and_read, 1, 1, SDL_GPU_SAMPLECOUNT_1, nullptr,
                                  "luminance image");
        }
    }

    void CreateSamplers() {
        SDL_GPUSamplerCreateInfo info{};
        info.min_filter = SDL_GPU_FILTER_LINEAR;
        info.mag_filter = SDL_GPU_FILTER_LINEAR;
        info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        // Anisotropic filtering keeps textures sharp on surfaces seen at a shallow angle, such as the ground.
        info.enable_anisotropy = true;
        info.max_anisotropy = 8.0f;
        info.max_lod = 1000.0f;
        texture_sampler = SDL_CreateGPUSampler(device, &info);

        // Compares a depth with the stored one and blends the yes/no answers of neighboring
        // texels, which softens shadow edges.
        SDL_GPUSamplerCreateInfo shadow_info{};
        shadow_info.min_filter = SDL_GPU_FILTER_LINEAR;
        shadow_info.mag_filter = SDL_GPU_FILTER_LINEAR;
        shadow_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        shadow_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        shadow_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        shadow_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        shadow_info.enable_compare = true;
        shadow_info.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        shadow_sampler = SDL_CreateGPUSampler(device, &shadow_info);

        SDL_GPUSamplerCreateInfo clamp_info{};
        clamp_info.min_filter = SDL_GPU_FILTER_NEAREST;
        clamp_info.mag_filter = SDL_GPU_FILTER_NEAREST;
        clamp_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        clamp_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        clamp_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        clamp_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        clamp_sampler = SDL_CreateGPUSampler(device, &clamp_info);

        SDL_GPUSamplerCreateInfo linear_clamp_info = clamp_info;
        linear_clamp_info.min_filter = SDL_GPU_FILTER_LINEAR;
        linear_clamp_info.mag_filter = SDL_GPU_FILTER_LINEAR;
        linear_clamp_sampler = SDL_CreateGPUSampler(device, &linear_clamp_info);

        SDL_GPUSamplerCreateInfo repeat_info{};
        repeat_info.min_filter = SDL_GPU_FILTER_LINEAR;
        repeat_info.mag_filter = SDL_GPU_FILTER_LINEAR;
        repeat_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        repeat_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        repeat_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        repeat_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        repeat_sampler = SDL_CreateGPUSampler(device, &repeat_info);

        if (texture_sampler == nullptr || shadow_sampler == nullptr || clamp_sampler == nullptr ||
            linear_clamp_sampler == nullptr || repeat_sampler == nullptr) {
            FatalSdlError("cannot create sampler");
        }
    }

    // A pipeline for a pass that covers the whole target with one triangle (fullscreen.vert.hlsl).
    SDL_GPUGraphicsPipeline* CreateFullscreenPipeline(SDL_GPUShader* vertex_shader, SDL_GPUShader* fragment_shader,
                                                      SDL_GPUTextureFormat format, FullscreenBlend blend) {
        SDL_GPUColorTargetDescription color_description{};
        color_description.format = format;
        if (blend != FullscreenBlend::Replace) {
            SDL_GPUColorTargetBlendState& state = color_description.blend_state;
            state.enable_blend = true;
            state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            if (blend == FullscreenBlend::AddAndDim) {
                state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            } else {
                state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            }
            // The target's own alpha stays as it is.
            state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        }

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = vertex_shader;
        info.fragment_shader = fragment_shader;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.target_info.color_target_descriptions = &color_description;
        info.target_info.num_color_targets = 1;
        SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
        if (pipeline == nullptr) {
            FatalSdlError("cannot create full-screen pipeline");
        }
        return pipeline;
    }

    // Draws one full-screen pass into `target`. keep_target: blend with what the target
    // holds instead of overwriting it.
    void DrawFullscreenPass(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target, bool keep_target,
                            SDL_GPUGraphicsPipeline* pipeline, const SDL_GPUTextureSamplerBinding* bindings,
                            Uint32 binding_count, const void* uniforms, Uint32 uniform_size) {
        SDL_GPUColorTargetInfo color_info{};
        color_info.texture = target;
        color_info.load_op = keep_target ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE;
        color_info.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color_info, 1, nullptr);
        if (pass == nullptr) {
            FatalSdlError("cannot begin full-screen pass");
        }
        SDL_BindGPUGraphicsPipeline(pass, pipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, binding_count);
        SDL_PushGPUFragmentUniformData(commands, 0, uniforms, uniform_size);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }

    void CreatePipelines() {
        SDL_GPUShader* mesh_vertex = CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0, 2, kMeshVertDxil, sizeof(kMeshVertDxil),
                                                  kMeshVertSpirv, sizeof(kMeshVertSpirv));
        SDL_GPUShader* mesh_fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 7, 2, kMeshFragDxil,
                                                    sizeof(kMeshFragDxil), kMeshFragSpirv, sizeof(kMeshFragSpirv));
        SDL_GPUShader* shadow_fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, kShadowFragDxil,
                                                      sizeof(kShadowFragDxil), kShadowFragSpirv, sizeof(kShadowFragSpirv));
        SDL_GPUShader* fullscreen_vertex =
            CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0, 0, kFullscreenVertDxil, sizeof(kFullscreenVertDxil),
                         kFullscreenVertSpirv, sizeof(kFullscreenVertSpirv));
        SDL_GPUShader* sky_fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1, kSkyFragDxil, sizeof(kSkyFragDxil),
                                                   kSkyFragSpirv, sizeof(kSkyFragSpirv));
        SDL_GPUShader* tonemap_fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 4, 1, kTonemapFragDxil,
                                                       sizeof(kTonemapFragDxil), kTonemapFragSpirv,
                                                       sizeof(kTonemapFragSpirv));

        SDL_GPUVertexBufferDescription vertex_buffer{};
        vertex_buffer.slot = 0;
        vertex_buffer.pitch = sizeof(MeshVertex);
        vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attributes[3]{};
        attributes[0].location = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[0].offset = offsetof(MeshVertex, position);
        attributes[1].location = 1;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[1].offset = offsetof(MeshVertex, normal);
        attributes[2].location = 2;
        attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attributes[2].offset = offsetof(MeshVertex, uv);

        SDL_GPUColorTargetDescription scene_color_description{};
        scene_color_description.format = kSceneFormat;
        SDL_GPUColorTargetDescription output_color_description{};
        output_color_description.format = kOutputFormat;

        // --- Meshes in the scene pass ---
        SDL_GPUGraphicsPipelineCreateInfo mesh_info{};
        mesh_info.vertex_shader = mesh_vertex;
        mesh_info.fragment_shader = mesh_fragment;
        mesh_info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer;
        mesh_info.vertex_input_state.num_vertex_buffers = 1;
        mesh_info.vertex_input_state.vertex_attributes = attributes;
        mesh_info.vertex_input_state.num_vertex_attributes = 3;
        mesh_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        mesh_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        mesh_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        mesh_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        mesh_info.rasterizer_state.enable_depth_clip = true;
        mesh_info.multisample_state.sample_count = ToSampleCount(msaa_samples);
        // With multisampling, the alpha written by the shader decides how many of a pixel's
        // samples are covered; cut-out edges (leaves) become smooth this way.
        mesh_info.multisample_state.enable_alpha_to_coverage = msaa_samples > 1;
        mesh_info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER;  // reversed depth, see CreateTargets
        mesh_info.depth_stencil_state.enable_depth_test = true;
        mesh_info.depth_stencil_state.enable_depth_write = true;
        mesh_info.target_info.color_target_descriptions = &scene_color_description;
        mesh_info.target_info.num_color_targets = 1;
        mesh_info.target_info.depth_stencil_format = kDepthFormat;
        mesh_info.target_info.has_depth_stencil_target = true;
        mesh_pipeline = SDL_CreateGPUGraphicsPipeline(device, &mesh_info);

        // The same without back-face culling, for double-sided materials.
        mesh_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        mesh_pipeline_double_sided = SDL_CreateGPUGraphicsPipeline(device, &mesh_info);

        // --- Meshes in the shadow pass: depth only, seen from the sun ---
        SDL_GPUGraphicsPipelineCreateInfo shadow_info = mesh_info;
        shadow_info.fragment_shader = shadow_fragment;
        shadow_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        // Shadow depth runs the usual way (0 near the sun), as the shadow matrices expect.
        shadow_info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
        // Geometry nearer to the sun than the shadow range is flattened onto its start instead of cut away.
        shadow_info.rasterizer_state.enable_depth_clip = false;
        // Pushes shadow depths slightly away from the sun, more on slanted surfaces, so that
        // lit surfaces do not shadow themselves ("shadow acne").
        shadow_info.rasterizer_state.enable_depth_bias = true;
        shadow_info.rasterizer_state.depth_bias_constant_factor = 1.0f;
        shadow_info.rasterizer_state.depth_bias_slope_factor = 2.5f;
        shadow_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        shadow_info.multisample_state.enable_alpha_to_coverage = false;
        shadow_info.target_info.color_target_descriptions = nullptr;
        shadow_info.target_info.num_color_targets = 0;
        shadow_pipeline = SDL_CreateGPUGraphicsPipeline(device, &shadow_info);

        // --- Meshes in the depth pass: depth only, seen from the camera, one sample per pixel ---
        SDL_GPUGraphicsPipelineCreateInfo after_depth_info{};  // filled after the skinned pipelines
        SDL_GPUGraphicsPipelineCreateInfo prepass_info = mesh_info;
        prepass_info.fragment_shader = shadow_fragment;
        prepass_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        prepass_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        prepass_info.multisample_state.enable_alpha_to_coverage = false;
        prepass_info.target_info.color_target_descriptions = nullptr;
        prepass_info.target_info.num_color_targets = 0;
        prepass_pipeline = SDL_CreateGPUGraphicsPipeline(device, &prepass_info);
        prepass_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        prepass_pipeline_double_sided = SDL_CreateGPUGraphicsPipeline(device, &prepass_info);

        // --- The same five pipelines for skinned meshes: another vertex shader, fed by a
        // second vertex buffer with joints and weights ---
        SDL_GPUShader* skinned_vertex =
            CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0, 2, kMeshSkinnedVertDxil, sizeof(kMeshSkinnedVertDxil),
                         kMeshSkinnedVertSpirv, sizeof(kMeshSkinnedVertSpirv), 1);
        SDL_GPUVertexBufferDescription skinned_buffers[2]{};
        skinned_buffers[0] = vertex_buffer;
        skinned_buffers[1].slot = 1;
        skinned_buffers[1].pitch = sizeof(SkinVertex);
        skinned_buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        SDL_GPUVertexAttribute skinned_attributes[5]{};
        skinned_attributes[0] = attributes[0];
        skinned_attributes[1] = attributes[1];
        skinned_attributes[2] = attributes[2];
        skinned_attributes[3].location = 3;
        skinned_attributes[3].buffer_slot = 1;
        skinned_attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4;
        skinned_attributes[3].offset = offsetof(SkinVertex, joints);
        skinned_attributes[4].location = 4;
        skinned_attributes[4].buffer_slot = 1;
        skinned_attributes[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        skinned_attributes[4].offset = offsetof(SkinVertex, weights);
        const auto create_skinned = [&](SDL_GPUGraphicsPipelineCreateInfo info, SDL_GPUCullMode cull_mode) {
            info.vertex_shader = skinned_vertex;
            info.vertex_input_state.vertex_buffer_descriptions = skinned_buffers;
            info.vertex_input_state.num_vertex_buffers = 2;
            info.vertex_input_state.vertex_attributes = skinned_attributes;
            info.vertex_input_state.num_vertex_attributes = 5;
            info.rasterizer_state.cull_mode = cull_mode;
            SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
            if (pipeline == nullptr) {
                FatalSdlError("cannot create skinned mesh pipeline");
            }
            return pipeline;
        };
        mesh_pipelines[kScenePass][0][0] = mesh_pipeline;
        mesh_pipelines[kScenePass][0][1] = mesh_pipeline_double_sided;
        mesh_pipelines[kScenePass][1][0] = create_skinned(mesh_info, SDL_GPU_CULLMODE_BACK);
        mesh_pipelines[kScenePass][1][1] = create_skinned(mesh_info, SDL_GPU_CULLMODE_NONE);
        // Shadows are drawn without culling whatever the material.
        mesh_pipelines[kShadowPass][0][0] = shadow_pipeline;
        mesh_pipelines[kShadowPass][0][1] = shadow_pipeline;
        mesh_pipelines[kShadowPass][1][0] = create_skinned(shadow_info, SDL_GPU_CULLMODE_NONE);
        mesh_pipelines[kShadowPass][1][1] = mesh_pipelines[kShadowPass][1][0];
        mesh_pipelines[kDepthPass][0][0] = prepass_pipeline;
        mesh_pipelines[kDepthPass][0][1] = prepass_pipeline_double_sided;
        mesh_pipelines[kDepthPass][1][0] = create_skinned(prepass_info, SDL_GPU_CULLMODE_BACK);
        mesh_pipelines[kDepthPass][1][1] = create_skinned(prepass_info, SDL_GPU_CULLMODE_NONE);
        // The scene pass after the depth pass: the same, but testing the finished depth
        // ("greater or equal" with reversed depth: the surface the depth pass kept) before
        // the shader runs, writing no depth.
        SDL_GPUShader* after_depth_fragment =
            CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 7, 2, kMeshAfterDepthFragDxil, sizeof(kMeshAfterDepthFragDxil),
                         kMeshAfterDepthFragSpirv, sizeof(kMeshAfterDepthFragSpirv));
        after_depth_info = mesh_info;
        after_depth_info.fragment_shader = after_depth_fragment;
        after_depth_info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
        after_depth_info.depth_stencil_state.enable_depth_write = false;
        after_depth_info.multisample_state.enable_alpha_to_coverage = false;
        after_depth_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
        mesh_pipelines[kSceneAfterDepthPass][0][0] = SDL_CreateGPUGraphicsPipeline(device, &after_depth_info);
        after_depth_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        mesh_pipelines[kSceneAfterDepthPass][0][1] = SDL_CreateGPUGraphicsPipeline(device, &after_depth_info);
        mesh_pipelines[kSceneAfterDepthPass][1][0] = create_skinned(after_depth_info, SDL_GPU_CULLMODE_BACK);
        mesh_pipelines[kSceneAfterDepthPass][1][1] = create_skinned(after_depth_info, SDL_GPU_CULLMODE_NONE);
        SDL_ReleaseGPUShader(device, skinned_vertex);
        if (mesh_pipelines[kSceneAfterDepthPass][0][0] == nullptr || mesh_pipelines[kSceneAfterDepthPass][0][1] == nullptr) {
            FatalSdlError("cannot create scene pipeline after the depth pass");
        }

        // --- And for instanced meshes: the usual vertex buffer, another vertex shader ---
        SDL_GPUShader* instanced_vertex =
            CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 1, 2, kMeshInstancedVertDxil, sizeof(kMeshInstancedVertDxil),
                         kMeshInstancedVertSpirv, sizeof(kMeshInstancedVertSpirv), 1);
        const auto create_instanced = [&](SDL_GPUGraphicsPipelineCreateInfo info, SDL_GPUCullMode cull_mode) {
            info.vertex_shader = instanced_vertex;
            info.rasterizer_state.cull_mode = cull_mode;
            SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
            if (pipeline == nullptr) {
                FatalSdlError("cannot create instanced mesh pipeline");
            }
            return pipeline;
        };
        mesh_pipelines[kScenePass][kInstanced][0] = create_instanced(mesh_info, SDL_GPU_CULLMODE_BACK);
        mesh_pipelines[kScenePass][kInstanced][1] = create_instanced(mesh_info, SDL_GPU_CULLMODE_NONE);
        mesh_pipelines[kShadowPass][kInstanced][0] = create_instanced(shadow_info, SDL_GPU_CULLMODE_NONE);
        mesh_pipelines[kShadowPass][kInstanced][1] = mesh_pipelines[kShadowPass][kInstanced][0];
        mesh_pipelines[kDepthPass][kInstanced][0] = create_instanced(prepass_info, SDL_GPU_CULLMODE_BACK);
        mesh_pipelines[kDepthPass][kInstanced][1] = create_instanced(prepass_info, SDL_GPU_CULLMODE_NONE);
        mesh_pipelines[kSceneAfterDepthPass][kInstanced][0] = create_instanced(after_depth_info, SDL_GPU_CULLMODE_BACK);
        mesh_pipelines[kSceneAfterDepthPass][kInstanced][1] = create_instanced(after_depth_info, SDL_GPU_CULLMODE_NONE);
        SDL_ReleaseGPUShader(device, after_depth_fragment);
        SDL_ReleaseGPUShader(device, instanced_vertex);

        // --- Terrain: its own vertex shader in all three passes, its own fragment shader in the scene pass ---
        SDL_GPUShader* terrain_vertex =
            CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 1, 3, kTerrainVertDxil, sizeof(kTerrainVertDxil), kTerrainVertSpirv,
                         sizeof(kTerrainVertSpirv), 1);
        SDL_GPUShader* terrain_fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 9, 2, kTerrainFragDxil,
                                                       sizeof(kTerrainFragDxil), kTerrainFragSpirv, sizeof(kTerrainFragSpirv));
        const auto create_terrain = [&](SDL_GPUGraphicsPipelineCreateInfo info, SDL_GPUShader* fragment,
                                        SDL_GPUCullMode cull_mode) {
            info.vertex_shader = terrain_vertex;
            info.fragment_shader = fragment;
            info.rasterizer_state.cull_mode = cull_mode;
            SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
            if (pipeline == nullptr) {
                FatalSdlError("cannot create terrain pipeline");
            }
            return pipeline;
        };
        terrain_pipelines[kScenePass] = create_terrain(mesh_info, terrain_fragment, SDL_GPU_CULLMODE_BACK);
        terrain_pipelines[kShadowPass] = create_terrain(shadow_info, shadow_fragment, SDL_GPU_CULLMODE_NONE);
        terrain_pipelines[kDepthPass] = create_terrain(prepass_info, shadow_fragment, SDL_GPU_CULLMODE_BACK);
        // The terrain shader discards nothing, so the depth test runs before it anyway.
        terrain_pipelines[kSceneAfterDepthPass] = create_terrain(after_depth_info, terrain_fragment, SDL_GPU_CULLMODE_BACK);
        SDL_ReleaseGPUShader(device, terrain_vertex);
        SDL_ReleaseGPUShader(device, terrain_fragment);

        // --- Impostors: cards facing the camera, in the scene and depth passes ---
        SDL_GPUShader* impostor_vertex =
            CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0, 2, kImpostorVertDxil, sizeof(kImpostorVertDxil), kImpostorVertSpirv,
                         sizeof(kImpostorVertSpirv), 1);
        SDL_GPUShader* impostor_fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 6, 2, kImpostorFragDxil,
                                                        sizeof(kImpostorFragDxil), kImpostorFragSpirv, sizeof(kImpostorFragSpirv));
        SDL_GPUShader* impostor_depth_fragment =
            CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, kImpostorDepthFragDxil, sizeof(kImpostorDepthFragDxil),
                         kImpostorDepthFragSpirv, sizeof(kImpostorDepthFragSpirv));
        const auto create_impostor = [&](SDL_GPUGraphicsPipelineCreateInfo info, SDL_GPUShader* fragment) {
            info.vertex_shader = impostor_vertex;
            info.fragment_shader = fragment;
            // The card always faces the camera; the shader cuts it to the model's outline.
            info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            info.multisample_state.enable_alpha_to_coverage = false;
            SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
            if (pipeline == nullptr) {
                FatalSdlError("cannot create impostor pipeline");
            }
            return pipeline;
        };
        impostor_pipelines[kScenePass] = create_impostor(mesh_info, impostor_fragment);
        impostor_pipelines[kDepthPass] = create_impostor(prepass_info, impostor_depth_fragment);
        SDL_GPUShader* impostor_after_depth_fragment =
            CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 6, 2, kImpostorAfterDepthFragDxil, sizeof(kImpostorAfterDepthFragDxil),
                         kImpostorAfterDepthFragSpirv, sizeof(kImpostorAfterDepthFragSpirv));
        impostor_pipelines[kSceneAfterDepthPass] = create_impostor(after_depth_info, impostor_after_depth_fragment);
        SDL_ReleaseGPUShader(device, impostor_after_depth_fragment);
        SDL_ReleaseGPUShader(device, impostor_vertex);
        SDL_ReleaseGPUShader(device, impostor_fragment);
        SDL_ReleaseGPUShader(device, impostor_depth_fragment);

        // --- Sky: covers the whole image behind the scene ---
        SDL_GPUGraphicsPipelineCreateInfo sky_info{};
        sky_info.vertex_shader = fullscreen_vertex;
        sky_info.fragment_shader = sky_fragment;
        sky_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        sky_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        sky_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        sky_info.multisample_state.sample_count = ToSampleCount(msaa_samples);
        sky_info.target_info.color_target_descriptions = &scene_color_description;
        sky_info.target_info.num_color_targets = 1;
        sky_info.target_info.depth_stencil_format = kDepthFormat;
        sky_info.target_info.has_depth_stencil_target = true;
        sky_pipeline = SDL_CreateGPUGraphicsPipeline(device, &sky_info);

        // --- Tone mapping: scene image to output image ---
        SDL_GPUGraphicsPipelineCreateInfo tonemap_info{};
        tonemap_info.vertex_shader = fullscreen_vertex;
        tonemap_info.fragment_shader = tonemap_fragment;
        tonemap_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        tonemap_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        tonemap_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        tonemap_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        tonemap_info.target_info.color_target_descriptions = &output_color_description;
        tonemap_info.target_info.num_color_targets = 1;
        tonemap_pipeline = SDL_CreateGPUGraphicsPipeline(device, &tonemap_info);

        if (mesh_pipeline == nullptr || mesh_pipeline_double_sided == nullptr || shadow_pipeline == nullptr ||
            sky_pipeline == nullptr || tonemap_pipeline == nullptr || prepass_pipeline == nullptr ||
            prepass_pipeline_double_sided == nullptr) {
            FatalSdlError("cannot create graphics pipeline");
        }

        // --- Full-screen passes: ambient occlusion, fog, bloom, automatic exposure ---
        struct FullscreenPass {
            SDL_GPUGraphicsPipeline** pipeline;
            Uint32 sampler_count;
            const unsigned char* dxil;
            size_t dxil_size;
            const unsigned char* spirv;
            size_t spirv_size;
            SDL_GPUTextureFormat format;
            FullscreenBlend blend;
        };
        const FullscreenPass passes[] = {
            {&ao_pipeline, 1, kAoFragDxil, sizeof(kAoFragDxil), kAoFragSpirv, sizeof(kAoFragSpirv), kAoFormat,
             FullscreenBlend::Replace},
            {&ao_blur_pipeline, 2, kAoBlurFragDxil, sizeof(kAoBlurFragDxil), kAoBlurFragSpirv, sizeof(kAoBlurFragSpirv),
             kAoFormat, FullscreenBlend::Replace},
            {&fog_pipeline, 3, kFogFragDxil, sizeof(kFogFragDxil), kFogFragSpirv, sizeof(kFogFragSpirv), kSceneFormat,
             FullscreenBlend::Replace},
            {&fog_composite_pipeline, 2, kFogCompositeFragDxil, sizeof(kFogCompositeFragDxil), kFogCompositeFragSpirv,
             sizeof(kFogCompositeFragSpirv), kSceneFormat, FullscreenBlend::AddAndDim},
            {&bloom_down_pipeline, 1, kBloomDownFragDxil, sizeof(kBloomDownFragDxil), kBloomDownFragSpirv,
             sizeof(kBloomDownFragSpirv), kSceneFormat, FullscreenBlend::Replace},
            {&bloom_up_pipeline, 1, kBloomUpFragDxil, sizeof(kBloomUpFragDxil), kBloomUpFragSpirv,
             sizeof(kBloomUpFragSpirv), kSceneFormat, FullscreenBlend::Mix},
            {&luminance_pipeline, 2, kLuminanceFragDxil, sizeof(kLuminanceFragDxil), kLuminanceFragSpirv,
             sizeof(kLuminanceFragSpirv), kLuminanceFormat, FullscreenBlend::Replace},
            {&taa_pipeline, 3, kTaaFragDxil, sizeof(kTaaFragDxil), kTaaFragSpirv, sizeof(kTaaFragSpirv), kSceneFormat,
             FullscreenBlend::Replace},
            {&depth_copy_pipeline, 1, kDepthCopyFragDxil, sizeof(kDepthCopyFragDxil), kDepthCopyFragSpirv,
             sizeof(kDepthCopyFragSpirv), SDL_GPU_TEXTUREFORMAT_R32_FLOAT, FullscreenBlend::Replace},
            {&clouds_pipeline, 4, kCloudsFragDxil, sizeof(kCloudsFragDxil), kCloudsFragSpirv, sizeof(kCloudsFragSpirv), kSceneFormat,
             FullscreenBlend::Replace},
            {&cloud_shadow_pipeline, 3, kCloudShadowFragDxil, sizeof(kCloudShadowFragDxil), kCloudShadowFragSpirv,
             sizeof(kCloudShadowFragSpirv), SDL_GPU_TEXTUREFORMAT_R8_UNORM, FullscreenBlend::Replace},
            {&clouds_composite_pipeline, 2, kCloudsCompositeFragDxil, sizeof(kCloudsCompositeFragDxil), kCloudsCompositeFragSpirv,
             sizeof(kCloudsCompositeFragSpirv), kSceneFormat, FullscreenBlend::AddAndDim},
        };
        for (const FullscreenPass& pass : passes) {
            SDL_GPUShader* fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, pass.sampler_count, 1, pass.dxil,
                                                   pass.dxil_size, pass.spirv, pass.spirv_size);
            *pass.pipeline = CreateFullscreenPipeline(fullscreen_vertex, fragment, pass.format, pass.blend);
            SDL_ReleaseGPUShader(device, fragment);
        }

        // --- Water: a mesh drawn over the single-sample scene, testing and writing depth. ---
        {
            SDL_GPUShader* water_fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 6, 2, kWaterFragDxil,
                                                         sizeof(kWaterFragDxil), kWaterFragSpirv, sizeof(kWaterFragSpirv));
            SDL_GPUGraphicsPipelineCreateInfo water_info = mesh_info;
            water_info.fragment_shader = water_fragment;
            water_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            water_info.multisample_state.enable_alpha_to_coverage = false;
            water_pipeline = SDL_CreateGPUGraphicsPipeline(device, &water_info);
            SDL_ReleaseGPUShader(device, water_fragment);
            if (water_pipeline == nullptr) {
                FatalSdlError("cannot create water pipeline");
            }
        }

        // Pipelines keep what they need; the shader objects can go.
        SDL_ReleaseGPUShader(device, mesh_vertex);
        SDL_ReleaseGPUShader(device, mesh_fragment);
        SDL_ReleaseGPUShader(device, shadow_fragment);
        SDL_ReleaseGPUShader(device, fullscreen_vertex);
        SDL_ReleaseGPUShader(device, sky_fragment);
        SDL_ReleaseGPUShader(device, tonemap_fragment);
    }

    // Copies a square of single bytes into a one-channel texture of that size.
    void UploadR8(SDL_GPUTexture* texture, const std::uint8_t* values, int resolution) {
        SDL_GPUTransferBuffer* transfer =
            CreateFilledTransferBuffer(values, static_cast<Uint32>(resolution) * static_cast<Uint32>(resolution));
        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
        if (commands == nullptr) {
            FatalSdlError("cannot acquire command buffer");
        }
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.pixels_per_row = static_cast<Uint32>(resolution);
        source.rows_per_layer = static_cast<Uint32>(resolution);
        SDL_GPUTextureRegion destination{};
        destination.texture = texture;
        destination.w = static_cast<Uint32>(resolution);
        destination.h = static_cast<Uint32>(resolution);
        destination.d = 1;
        SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
        SDL_EndGPUCopyPass(copy_pass);
        if (!SDL_SubmitGPUCommandBuffer(commands)) {
            FatalSdlError("cannot submit texture upload");
        }
        SDL_ReleaseGPUTransferBuffer(device, transfer);
    }

    // Data reaches the GPU through a transfer buffer: CPU-visible memory that a copy pass reads from.
    SDL_GPUTransferBuffer* CreateFilledTransferBuffer(const void* data, Uint32 size) {
        SDL_GPUTransferBufferCreateInfo transfer_info{};
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = size;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
        if (transfer == nullptr) {
            FatalSdlError("cannot create upload transfer buffer");
        }
        void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
        if (mapped == nullptr) {
            FatalSdlError("cannot map upload transfer buffer");
        }
        std::memcpy(mapped, data, size);
        SDL_UnmapGPUTransferBuffer(device, transfer);
        return transfer;
    }

    SDL_GPUBuffer* CreateBufferWithData(SDL_GPUBufferUsageFlags usage, const void* data, Uint32 size) {
        SDL_GPUBufferCreateInfo buffer_info{};
        buffer_info.usage = usage;
        buffer_info.size = size;
        SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, &buffer_info);
        if (buffer == nullptr) {
            FatalSdlError("cannot create buffer");
        }
        SDL_GPUTransferBuffer* transfer = CreateFilledTransferBuffer(data, size);

        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
        if (commands == nullptr) {
            FatalSdlError("cannot acquire command buffer");
        }
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTransferBufferLocation source{};
        source.transfer_buffer = transfer;
        SDL_GPUBufferRegion destination{};
        destination.buffer = buffer;
        destination.size = size;
        SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
        SDL_EndGPUCopyPass(copy_pass);
        if (!SDL_SubmitGPUCommandBuffer(commands)) {
            FatalSdlError("cannot submit upload");
        }
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return buffer;
    }

    SDL_GPUTexture* GetTexture(TextureHandle handle, TextureHandle fallback) const {
        const TextureHandle chosen = handle.id != 0 ? handle : fallback;
        return textures[chosen.id - 1];
    }

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

    float CascadeTileScale(int cascade) const {
        return cascade >= 2 ? glm::clamp(config.far_shadow_scale, 0.125f, 1.0f) : 1.0f;
    }

    Cascades ComputeCascades() const {
        Cascades cascades;
        const float aspect_ratio = static_cast<float>(config.width) / static_cast<float>(config.height);
        const float tan_half_fov = std::tan(glm::radians(camera.vertical_fov_degrees) * 0.5f);
        const glm::vec3 forward = glm::normalize(camera.target - camera.position);
        const glm::vec3 right = glm::normalize(glm::cross(forward, camera.up));
        const glm::vec3 up = glm::cross(right, forward);

        const glm::vec3 light_direction = glm::normalize(light.direction);
        const glm::vec3 light_up = std::fabs(light_direction.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        // Rotation that looks along the light, used to align cascades to the shadow image grid.
        const glm::mat4 light_rotation = glm::lookAt(glm::vec3(0.0f), light_direction, light_up);
        const glm::mat4 light_rotation_inverse = glm::inverse(light_rotation);

        const float near_distance = camera.near_plane;
        cascades.far_distance = ComputeShadowCascadeEnds(near_distance, environment.shadow_distance);
        float range_start = near_distance;
        for (int i = 0; i < kCascadeCount; ++i) {
            const float range_end = cascades.far_distance[i];

            // A sphere around this part of the view. Its size does not change when the camera
            // turns, which keeps shadow edges from crawling.
            glm::vec3 corners[8];
            int corner_index = 0;
            for (const float distance : {range_start, range_end}) {
                const float half_height = distance * tan_half_fov;
                const float half_width = half_height * aspect_ratio;
                const glm::vec3 center = camera.position + forward * distance;
                for (const float x : {-1.0f, 1.0f}) {
                    for (const float y : {-1.0f, 1.0f}) {
                        corners[corner_index++] = center + right * (x * half_width) + up * (y * half_height);
                    }
                }
            }
            glm::vec3 sphere_center(0.0f);
            for (const glm::vec3& corner : corners) {
                sphere_center += corner / 8.0f;
            }
            float radius = 0.0f;
            for (const glm::vec3& corner : corners) {
                radius = glm::max(radius, glm::length(corner - sphere_center));
            }
            radius = std::ceil(radius * 8.0f) / 8.0f;

            // Move the center in whole shadow texels only; otherwise shadows shimmer as the camera moves.
            const float tile_scale = CascadeTileScale(i);
            cascades.tile_scale[i] = tile_scale;
            const float texel_size = radius * 2.0f / (static_cast<float>(config.shadow_map_size) * tile_scale);
            cascades.texel_size[i] = texel_size;
            glm::vec3 center_in_light = glm::vec3(light_rotation * glm::vec4(sphere_center, 1.0f));
            center_in_light.x = std::floor(center_in_light.x / texel_size) * texel_size;
            center_in_light.y = std::floor(center_in_light.y / texel_size) * texel_size;
            sphere_center = glm::vec3(light_rotation_inverse * glm::vec4(center_in_light, 1.0f));

            const glm::vec3 eye = sphere_center - light_direction * (radius + kShadowCasterDistance);
            const glm::mat4 light_view = glm::lookAt(eye, sphere_center, light_up);
            const glm::mat4 light_projection =
                glm::ortho(-radius, radius, -radius, radius, 0.0f, radius * 2.0f + kShadowCasterDistance);
            cascades.light_view_projection[i] = light_projection * light_view;

            // Clip coordinates (-1..1, y up) to this cascade's tile in the 2 x 2 shadow image
            // (0..1, y down).
            const float tile_x = static_cast<float>(i % 2);
            const float tile_y = static_cast<float>(i / 2);
            // A cascade with a smaller share uses the top-left part of its tile.
            glm::mat4 to_tile(1.0f);
            to_tile[0][0] = 0.25f * tile_scale;
            to_tile[1][1] = -0.25f * tile_scale;
            to_tile[3][0] = 0.25f * tile_scale + tile_x * 0.5f;
            to_tile[3][1] = 0.25f * tile_scale + tile_y * 0.5f;
            cascades.shadow_matrix[i] = to_tile * cascades.light_view_projection[i];

            range_start = range_end;
        }
        return cascades;
    }

    void DrawCommandGeometry(SDL_GPURenderPass* pass, const DrawCommand& command) {
        const Mesh& mesh = meshes[command.mesh_id - 1];
        SDL_GPUBufferBinding vertex_bindings[2]{};
        vertex_bindings[0].buffer = mesh.vertex_buffer;
        vertex_bindings[1].buffer = mesh.skin_buffer;
        SDL_BindGPUVertexBuffers(pass, 0, vertex_bindings, command.skinned ? 2 : 1);
        SDL_GPUBufferBinding index_binding{};
        index_binding.buffer = mesh.index_buffer;
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count, 1, 0, 0, 0);
    }

    static ObjectUniforms MakeObjectUniforms(const DrawCommand& command) {
        ObjectUniforms uniforms;
        uniforms.model = command.transform;
        uniforms.color = ToVec4(command.material.color);
        uniforms.uv_scale = glm::vec4(command.material.uv_scale.x, command.material.uv_scale.y,
                                      static_cast<float>(command.first_joint), 0.0f);
        uniforms.wind_response = glm::vec4(command.material.wind_bend, command.material.wind_flutter, 0.0f, 0.0f);
        return uniforms;
    }

    // The frame's wind as the vertex shaders read it (see ApplyWind in common.hlsli).
    glm::vec4 WindUniform() const {
        const float length = glm::length(environment.wind_direction);
        const glm::vec2 direction = length > 0.0001f ? environment.wind_direction / length : glm::vec2(1.0f, 0.0f);
        return glm::vec4(direction.x, direction.y, glm::max(environment.wind_strength, 0.0f), environment.wind_time);
    }

    // Binds the pipeline a draw command needs in the given pass, unless it is bound already.
    void BindMeshPipeline(SDL_GPURenderPass* pass, MeshPass mesh_pass, const DrawCommand& command,
                          SDL_GPUGraphicsPipeline*& bound_pipeline) {
        SDL_GPUGraphicsPipeline* pipeline =
            mesh_pipelines[mesh_pass][command.skinned ? 1 : 0][command.material.double_sided ? 1 : 0];
        if (pipeline != bound_pipeline) {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            bound_pipeline = pipeline;
            if (command.skinned) {
                SDL_BindGPUVertexStorageBuffers(pass, 0, &joint_buffer, 1);
            }
        }
    }

    // Which batches a call to DrawInstanceBatches draws, by Material::casts_ambient_occlusion
    // (terrain and impostors always cast it).
    enum AoFilter { kAllBatches, kAoCasters, kAoNonCasters };

    // The depth-only drawing of the depth pass (pass 2), limited by `filter`.
    void DrawDepthPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, AoFilter filter) {
        SDL_GPUGraphicsPipeline* bound_pipeline = nullptr;
        for (const std::uint32_t command_index : visible_commands) {
            const DrawCommand& command = draw_commands[command_index];
            const bool casts_ao = command.material.casts_ambient_occlusion;
            if ((filter == kAoCasters && !casts_ao) || (filter == kAoNonCasters && casts_ao)) {
                continue;
            }
            BindMeshPipeline(pass, kDepthPass, command, bound_pipeline);
            const ObjectUniforms object_uniforms = MakeObjectUniforms(command);
            SDL_PushGPUVertexUniformData(commands, 1, &object_uniforms, sizeof(object_uniforms));
            const MaterialUniforms material_uniforms = MakeMaterialUniforms(command.material, temporal_noise_offset);
            SDL_PushGPUFragmentUniformData(commands, 0, &material_uniforms, sizeof(material_uniforms));
            SDL_GPUTextureSamplerBinding binding{};
            binding.texture = GetTexture(command.material.texture, white_texture);
            binding.sampler = texture_sampler;
            SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
            DrawCommandGeometry(pass, command);
        }
        DrawInstanceBatches(commands, pass, kDepthPass, 0, false, bound_pipeline, filter);
    }

    // True when something visible this frame casts no ambient occlusion.
    bool HasAoNonCasters() const {
        for (const std::uint32_t command_index : visible_commands) {
            if (!draw_commands[command_index].material.casts_ambient_occlusion) {
                return true;
            }
        }
        for (const InstanceBatch& batch : instance_batches) {
            if (batch.list_count[0] > 0 && batch.terrain_id == 0 && batch.impostor_id == 0 && !batch.material.casts_ambient_occlusion) {
                return true;
            }
        }
        return false;
    }

    // Copies this frame's joint matrices to the graphics card.
    void UploadJointMatrices(SDL_GPUCommandBuffer* commands) {
        UploadMatrices(commands, joint_palette, joint_buffer, joint_buffer_capacity, joint_transfer, joint_transfer_capacity);
    }

    // Draws, in one pass, every instance batch that has something in the given list.
    void DrawInstanceBatches(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, MeshPass mesh_pass, int list,
                             bool ao_on, SDL_GPUGraphicsPipeline*& bound_pipeline, AoFilter filter = kAllBatches) {
        // Consecutive batches often share a material (the levels of one species, the parts
        // of one plant): then the fragment uniforms and textures stay bound. Every call to
        // the graphics API costs several microseconds, and a frame has hundreds of batches.
        const Material* bound_material = nullptr;
        for (const InstanceBatch& batch : instance_batches) {
            if (batch.list_count[list] == 0) {
                continue;
            }
            const bool casts_ao = batch.terrain_id != 0 || batch.impostor_id != 0 || batch.material.casts_ambient_occlusion;
            if ((filter == kAoCasters && !casts_ao) || (filter == kAoNonCasters && casts_ao)) {
                continue;
            }
            if (batch.terrain_id != 0) {
                DrawTerrainBatch(commands, pass, mesh_pass, list, ao_on, batch, bound_pipeline);
                bound_material = nullptr;
                continue;
            }
            if (batch.impostor_id != 0) {
                DrawImpostorBatch(commands, pass, mesh_pass, list, ao_on, batch, bound_pipeline);
                bound_material = nullptr;
                continue;
            }
            const Material& material = batch.material;
            SDL_GPUGraphicsPipeline* pipeline = mesh_pipelines[mesh_pass][kInstanced][material.double_sided ? 1 : 0];
            if (pipeline != bound_pipeline) {
                SDL_BindGPUGraphicsPipeline(pass, pipeline);
                bound_pipeline = pipeline;
                SDL_BindGPUVertexStorageBuffers(pass, 0, &instance_buffer, 1);
                SDL_GPUTextureSamplerBinding trample_binding{};
                trample_binding.texture = trample_texture != nullptr && trample_info.w > 0.0f ? trample_texture : no_trample_texture;
                trample_binding.sampler = linear_clamp_sampler;
                SDL_BindGPUVertexSamplers(pass, 0, &trample_binding, 1);
            }

            ObjectUniforms object_uniforms;
            object_uniforms.model = glm::mat4(1.0f);
            object_uniforms.color = ToVec4(material.color);
            object_uniforms.uv_scale =
                glm::vec4(material.uv_scale.x, material.uv_scale.y, static_cast<float>(batch.list_first[list]), 0.0f);
            object_uniforms.wind_response = glm::vec4(material.wind_bend, material.wind_flutter, 0.0f, 0.0f);
            SDL_PushGPUVertexUniformData(commands, 1, &object_uniforms, sizeof(object_uniforms));

            if (bound_material == nullptr || !SameFragmentBindings(*bound_material, material)) {
                const MaterialUniforms material_uniforms = MakeMaterialUniforms(material, temporal_noise_offset);
                SDL_GPUTextureSamplerBinding bindings[7]{};
                bindings[0].texture = GetTexture(material.texture, white_texture);
                bindings[0].sampler = texture_sampler;
                if (IsScenePass(mesh_pass)) {
                    SDL_PushGPUFragmentUniformData(commands, 1, &material_uniforms, sizeof(material_uniforms));
                    bindings[1].texture = GetTexture(material.normal_texture, flat_normal_texture);
                    bindings[1].sampler = texture_sampler;
                    bindings[2].texture = GetTexture(material.orm_texture, white_texture);
                    bindings[2].sampler = texture_sampler;
                    bindings[3].texture = shadow_atlas;
                    bindings[3].sampler = shadow_sampler;
                    bindings[4].texture = ao_on ? ao_target : textures[white_texture.id - 1];
                    bindings[4].sampler = clamp_sampler;
                    AddLightingBindings(&bindings[5]);
                    SDL_BindGPUFragmentSamplers(pass, 0, bindings, 7);
                } else {
                    // The depth-only passes need the material only to cut out leaves.
                    SDL_PushGPUFragmentUniformData(commands, 0, &material_uniforms, sizeof(material_uniforms));
                    SDL_BindGPUFragmentSamplers(pass, 0, bindings, 1);
                }
                bound_material = &material;
            }

            const Mesh& mesh = meshes[batch.mesh_id - 1];
            SDL_GPUBufferBinding vertex_binding{};
            vertex_binding.buffer = mesh.vertex_buffer;
            SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
            SDL_GPUBufferBinding index_binding{};
            index_binding.buffer = mesh.index_buffer;
            SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count, batch.list_count[list], 0, 0, 0);
            ++stats.instance_draw_calls;
        }
    }

    void DrawTerrainBatch(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, MeshPass mesh_pass, int list, bool ao_on,
                          const InstanceBatch& batch, SDL_GPUGraphicsPipeline*& bound_pipeline) {
        const TerrainData& terrain = terrains[batch.terrain_id - 1];
        SDL_GPUGraphicsPipeline* pipeline = terrain_pipelines[mesh_pass];
        if (pipeline != bound_pipeline) {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            bound_pipeline = pipeline;
            SDL_BindGPUVertexStorageBuffers(pass, 0, &instance_buffer, 1);
        }
        // The vertex shader reads exact points, the fragment shader blends between them.
        SDL_GPUTextureSamplerBinding height_binding{};
        height_binding.texture = terrain.height_texture;
        height_binding.sampler = clamp_sampler;
        SDL_BindGPUVertexSamplers(pass, 0, &height_binding, 1);

        ObjectUniforms object_uniforms;
        object_uniforms.model = glm::mat4(1.0f);
        object_uniforms.color = glm::vec4(1.0f);
        object_uniforms.uv_scale = glm::vec4(1.0f, 1.0f, static_cast<float>(batch.list_first[list]), 0.0f);
        SDL_PushGPUVertexUniformData(commands, 1, &object_uniforms, sizeof(object_uniforms));
        TerrainVertexUniforms vertex_uniforms;
        vertex_uniforms.map = glm::vec4(terrain.origin.x, terrain.origin.z, terrain.spacing, mesh_pass == kShadowPass ? 0.0f : 1.0f);
        vertex_uniforms.map_heights = glm::vec4(terrain.origin.y, terrain.height_range, static_cast<float>(terrain.width - 1),
                                                static_cast<float>(terrain.depth - 1));
        SDL_PushGPUVertexUniformData(commands, 2, &vertex_uniforms, sizeof(vertex_uniforms));

        if (IsScenePass(mesh_pass)) {
            const TerrainMaterial& look = batch.terrain_material;
            TerrainMaterialUniforms material_uniforms;
            material_uniforms.map = vertex_uniforms.map;
            material_uniforms.map_heights = glm::vec4(0.0f, terrain.height_range, 1.0f / static_cast<float>(terrain.width),
                                                      1.0f / static_cast<float>(terrain.depth));
            const bool textured = look.colors.id != 0 && look.layer_count > 0;
            material_uniforms.steep = glm::vec4(std::cos(glm::radians(look.steep_start_degrees)),
                                                std::cos(glm::radians(look.steep_end_degrees)),
                                                textured ? static_cast<float>(look.layer_count) : 1.0f, textured ? 1.0f : 0.0f);
            material_uniforms.steep_color = glm::vec4(look.steep_color.r, look.steep_color.g, look.steep_color.b, look.roughness);
            for (int layer = 0; layer < kMaxTerrainLayers; ++layer) {
                material_uniforms.tile_scales[layer / 4][layer % 4] = 1.0f / glm::max(look.tile_sizes[layer], 0.01f);
            }
            // Height map point -> weight image coordinates: the corner pixels' centers lie on
            // the map's first and last points.
            const glm::vec2 weight_size = look.weights[0].id != 0 ? texture_sizes[look.weights[0].id - 1] : glm::vec2(1.0f);
            material_uniforms.weight_map =
                glm::vec4((weight_size.x - 1.0f) / (weight_size.x * static_cast<float>(terrain.width - 1)),
                          (weight_size.y - 1.0f) / (weight_size.y * static_cast<float>(terrain.depth - 1)), 0.5f / weight_size.x,
                          0.5f / weight_size.y);
            SDL_PushGPUFragmentUniformData(commands, 1, &material_uniforms, sizeof(material_uniforms));

            SDL_GPUTextureSamplerBinding bindings[9]{};
            bindings[0].texture = terrain.height_texture;
            bindings[0].sampler = linear_clamp_sampler;
            bindings[1].texture = GetTexture(textured ? look.colors : white_array_texture, white_array_texture);
            bindings[1].sampler = texture_sampler;
            bindings[2].texture = GetTexture(textured && look.normals.id != 0 ? look.normals : flat_normal_array_texture,
                                             flat_normal_array_texture);
            bindings[2].sampler = texture_sampler;
            bindings[3].texture = GetTexture(look.weights[0], first_layer_weights);
            bindings[3].sampler = linear_clamp_sampler;
            bindings[4].texture = GetTexture(look.weights[1], no_weights);
            bindings[4].sampler = linear_clamp_sampler;
            bindings[5].texture = shadow_atlas;
            bindings[5].sampler = shadow_sampler;
            bindings[6].texture = ao_on ? ao_target : textures[white_texture.id - 1];
            bindings[6].sampler = clamp_sampler;
            AddLightingBindings(&bindings[7]);
            SDL_BindGPUFragmentSamplers(pass, 0, bindings, 9);
        } else {
            // The depth-only passes use the leaf-cutting shader with nothing to cut.
            MaterialUniforms material_uniforms{};
            SDL_PushGPUFragmentUniformData(commands, 0, &material_uniforms, sizeof(material_uniforms));
            SDL_GPUTextureSamplerBinding binding{};
            binding.texture = textures[white_texture.id - 1];
            binding.sampler = texture_sampler;
            SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        }

        const Mesh& mesh = meshes[batch.mesh_id - 1];
        SDL_GPUBufferBinding vertex_binding{};
        vertex_binding.buffer = mesh.vertex_buffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        SDL_GPUBufferBinding index_binding{};
        index_binding.buffer = mesh.index_buffer;
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count, batch.list_count[list], 0, 0, 0);
        ++stats.instance_draw_calls;
    }

    void DrawImpostorBatch(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, MeshPass mesh_pass, int list, bool ao_on,
                           const InstanceBatch& batch, SDL_GPUGraphicsPipeline*& bound_pipeline) {
        SDL_GPUGraphicsPipeline* pipeline = impostor_pipelines[mesh_pass];
        if (pipeline == nullptr) {
            return;  // the shadow pass
        }
        if (pipeline != bound_pipeline) {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            bound_pipeline = pipeline;
            SDL_BindGPUVertexStorageBuffers(pass, 0, &instance_buffer, 1);
        }
        const ImpostorData& impostor = impostors[batch.impostor_id - 1];
        ImpostorUniforms vertex_uniforms;
        vertex_uniforms.center_radius = glm::vec4(impostor.center, impostor.radius);
        vertex_uniforms.camera_frames = glm::vec4(camera.position, static_cast<float>(impostor.frames_per_side));
        vertex_uniforms.batch = glm::vec4(static_cast<float>(batch.list_first[list]), 0.0f, 0.0f, 0.0f);
        SDL_PushGPUVertexUniformData(commands, 1, &vertex_uniforms, sizeof(vertex_uniforms));
        ImpostorMaterial material;
        material.material = glm::vec4(kImpostorAlphaCutoff, kImpostorRoughness, temporal_noise_offset, 0.0f);

        SDL_GPUTextureSamplerBinding bindings[6]{};
        bindings[0].texture = textures[impostor.color.id - 1];
        bindings[0].sampler = texture_sampler;
        if (IsScenePass(mesh_pass)) {
            SDL_PushGPUFragmentUniformData(commands, 1, &material, sizeof(material));
            bindings[1].texture = textures[impostor.normal.id - 1];
            bindings[1].sampler = texture_sampler;
            bindings[2].texture = shadow_atlas;
            bindings[2].sampler = shadow_sampler;
            bindings[3].texture = ao_on ? ao_target : textures[white_texture.id - 1];
            bindings[3].sampler = clamp_sampler;
            AddLightingBindings(&bindings[4]);
            SDL_BindGPUFragmentSamplers(pass, 0, bindings, 6);
        } else {
            SDL_PushGPUFragmentUniformData(commands, 0, &material, sizeof(material));
            SDL_BindGPUFragmentSamplers(pass, 0, bindings, 1);
        }

        const Mesh& card = meshes[batch.mesh_id - 1];
        SDL_GPUBufferBinding vertex_binding{};
        vertex_binding.buffer = card.vertex_buffer;
        SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
        SDL_GPUBufferBinding index_binding{};
        index_binding.buffer = card.index_buffer;
        SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(pass, card.index_count, batch.list_count[list], 0, 0, 0);
        ++stats.instance_draw_calls;
    }

    // Decides, for every instance of every batch, which lists it is in (the camera's view,
    // each shadow cascade), and writes the transforms of each list one after another.
    // Instances that passed culling in one list, with their sort key (see CullInstances).
    std::vector<std::pair<float, std::uint32_t>> sorted_instances;

    void CullInstances(const glm::mat4& view_projection, const Cascades* cascades) {
        instance_data.clear();
        ViewVolume volumes[kInstanceListCount];
        volumes[0] = MakeViewVolume(view_projection, true);
        for (int cascade = 0; cascade < kCascadeCount && cascades != nullptr; ++cascade) {
            volumes[1 + cascade] = MakeViewVolume(cascades->light_view_projection[cascade], false);
        }
        for (InstanceBatch& batch : instance_batches) {
            // A sphere around the mesh; each instance moves and scales it.
            const Mesh& mesh = meshes[batch.mesh_id - 1];
            glm::vec4 local_center((mesh.bounds_min + mesh.bounds_max) * 0.5f, 1.0f);
            float local_radius = glm::length(mesh.bounds_max - mesh.bounds_min) * 0.5f;
            if (batch.impostor_id != 0) {
                // A card facing the camera stays within the model's sphere.
                const ImpostorData& impostor = impostors[batch.impostor_id - 1];
                local_center = glm::vec4(impostor.center, 1.0f);
                local_radius = impostor.radius;
            }
            // Each instance's sphere is computed once; the lists then only test it.
            instance_spheres.resize(batch.count);
            for (std::uint32_t i = 0; i < batch.count; ++i) {
                const glm::mat4& transform = instance_sources[batch.first_source + i];
                if (batch.terrain_id != 0) {
                    // A terrain patch's matrix says: corner, size, and how tall the land in it is.
                    const glm::vec3 extent(transform[0].x, transform[1].y, transform[2].z);
                    instance_spheres[i] = glm::vec4(glm::vec3(transform[3]) + extent * 0.5f, glm::length(extent) * 0.5f);
                    continue;
                }
                const glm::vec3 center = glm::vec3(transform * local_center);
                const float radius = local_radius * std::sqrt(glm::max(glm::dot(transform[0], transform[0]),
                                                                       glm::max(glm::dot(transform[1], transform[1]),
                                                                                glm::dot(transform[2], transform[2]))));
                instance_spheres[i] = glm::vec4(center, radius);
            }
            for (int list = 0; list < kInstanceListCount; ++list) {
                batch.list_first[list] = static_cast<std::uint32_t>(instance_data.size());
                batch.list_count[list] = 0;
                const bool wanted = list == 0 ? batch.options.draw_in_view
                                              : cascades != nullptr && (batch.options.shadow_cascades >> (list - 1)) & 1;
                if (!wanted) {
                    continue;
                }
                const ViewVolume& volume = volumes[list];
                // Visible instances are drawn nearest first: from the camera in the view, from
                // the sun in a cascade. Then the depth test rejects most hidden leaves before
                // their pixels are shaded, which matters most in shadow cascades, where tree
                // crowns overlap many times. Ties keep the instances' order, so the result is
                // the same on every run.
                const glm::mat4& list_projection =
                    list == 0 ? view_projection : cascades->light_view_projection[list - 1];
                sorted_instances.clear();
                for (std::uint32_t i = 0; i < batch.count; ++i) {
                    const glm::vec4& sphere = instance_spheres[i];
                    if (!IsSphereOutside(volume, glm::vec3(sphere), sphere.w)) {
                        const glm::vec4 clip = list_projection * glm::vec4(glm::vec3(sphere), 1.0f);
                        // The view uses distance along the view (w); a cascade, depth from the sun (z).
                        sorted_instances.emplace_back(list == 0 ? clip.w : clip.z, i);
                    }
                }
                if (batch.terrain_id == 0) {
                    std::sort(sorted_instances.begin(), sorted_instances.end());
                }
                for (const auto& [depth, i] : sorted_instances) {
                    instance_data.push_back(instance_sources[batch.first_source + i]);
                }
                batch.list_count[list] = static_cast<std::uint32_t>(sorted_instances.size());
                if (list == 0) {
                    stats.instances_drawn += static_cast<int>(batch.list_count[list]);
                } else {
                    stats.shadow_instances += static_cast<int>(batch.list_count[list]);
                }
            }
            stats.instances_submitted += static_cast<int>(batch.count);
        }
    }

    // Copies a list of matrices into a buffer that vertex shaders read, growing it as needed,
    // through an upload buffer that is kept between frames.
    void UploadMatrices(SDL_GPUCommandBuffer* commands, const std::vector<glm::mat4>& matrices, SDL_GPUBuffer*& buffer,
                        Uint32& capacity, SDL_GPUTransferBuffer*& transfer, Uint32& transfer_capacity) {
        if (matrices.empty()) {
            return;
        }
        const Uint32 count = static_cast<Uint32>(matrices.size());
        if (count > capacity) {
            if (buffer != nullptr) {
                SDL_ReleaseGPUBuffer(device, buffer);
            }
            capacity = count < 1024 ? 1024 : count * 2;
            SDL_GPUBufferCreateInfo buffer_info{};
            buffer_info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
            buffer_info.size = capacity * static_cast<Uint32>(sizeof(glm::mat4));
            buffer = SDL_CreateGPUBuffer(device, &buffer_info);
            if (buffer == nullptr) {
                FatalSdlError("cannot create matrix buffer");
            }
        }
        const Uint32 size = count * static_cast<Uint32>(sizeof(glm::mat4));
        if (count > transfer_capacity) {
            if (transfer != nullptr) {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
            }
            transfer_capacity = capacity;
            SDL_GPUTransferBufferCreateInfo transfer_info{};
            transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transfer_info.size = transfer_capacity * static_cast<Uint32>(sizeof(glm::mat4));
            transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
            if (transfer == nullptr) {
                FatalSdlError("cannot create upload transfer buffer");
            }
        }
        // cycle = true: the previous frame's upload may still be in flight.
        void* mapped = SDL_MapGPUTransferBuffer(device, transfer, true);
        if (mapped == nullptr) {
            FatalSdlError("cannot map upload transfer buffer");
        }
        std::memcpy(mapped, matrices.data(), size);
        SDL_UnmapGPUTransferBuffer(device, transfer);
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTransferBufferLocation source{};
        source.transfer_buffer = transfer;
        SDL_GPUBufferRegion destination{};
        destination.buffer = buffer;
        destination.size = size;
        // cycle = true: the previous frame may still be reading the old contents.
        SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
        SDL_EndGPUCopyPass(copy_pass);
    }

    // factors.w carries the frame's noise offset (see temporal_noise_offset).
    static MaterialUniforms MakeMaterialUniforms(const Material& material, float noise_offset = 0.0f) {
        MaterialUniforms uniforms;
        uniforms.factors = glm::vec4(material.metallic, material.roughness, material.alpha_cutoff, noise_offset);
        uniforms.flags = glm::vec4(material.normal_texture.id != 0 ? 1.0f : 0.0f, material.orm_texture.id != 0 ? 1.0f : 0.0f,
                                   material.occlusion_in_orm ? 1.0f : 0.0f,
                                   material.double_sided ? (material.back_faces_keep_normal ? 2.0f : 1.0f) : 0.0f);
        return uniforms;
    }
};

Renderer::Renderer(const RendererConfig& config) : impl_(std::make_unique<Impl>()) {
    if (config.width < 1 || config.height < 1) {
        FatalError("Renderer: width and height must be at least 1");
    }
    if (config.shadow_map_size < 64 || config.shadow_map_size > 8192) {
        FatalError("Renderer: shadow_map_size must be between 64 and 8192");
    }
    impl_->config = config;
    impl_->post = config.post;
    impl_->linear_clear_color = SDL_FColor{SrgbToLinear(config.clear_color.r), SrgbToLinear(config.clear_color.g),
                                           SrgbToLinear(config.clear_color.b), config.clear_color.a};

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        FatalSdlError("cannot initialize SDL video");
    }

    if (config.headless) {
        // Vulkan loads "implicit layers" registered by other programs (screen recorders, overlays)
        // into every process. A headless run has nothing to overlay or record, and foreign code
        // in the process is a risk for repeatable verification, so ask the Vulkan loader to skip them.
        SDL_setenv_unsafe("VK_LOADER_LAYERS_DISABLE", "~implicit~", 1);
    }

    impl_->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV,
                                        config.enable_validation, GetDriverName(config.backend));
    if (impl_->device == nullptr) {
        FatalSdlError("cannot create graphics device");
    }

    if (!config.headless) {
        impl_->window = SDL_CreateWindow(config.window_title, config.width, config.height, 0);
        if (impl_->window == nullptr) {
            FatalSdlError("cannot create window");
        }
        if (!SDL_ClaimWindowForGPUDevice(impl_->device, impl_->window)) {
            FatalSdlError("cannot attach window to graphics device");
        }
        // SDR_LINEAR: the window image is sRGB-encoded too, so copying the output image to it
        // keeps colors unchanged. VSYNC: presenting waits for the display refresh, which also
        // paces the game loop.
        if (!SDL_WindowSupportsGPUSwapchainComposition(impl_->device, impl_->window,
                                                       SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR)) {
            FatalError("Renderer: the window does not support an sRGB-encoded image");
        }
        if (!SDL_SetGPUSwapchainParameters(impl_->device, impl_->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR,
                                           SDL_GPU_PRESENTMODE_VSYNC)) {
            FatalSdlError("cannot configure window presentation");
        }
    }

    impl_->CreateTargets();
    impl_->CreateSamplers();
    impl_->CreatePipelines();

    TextureOptions plain_color;
    plain_color.generate_mipmaps = false;
    impl_->white_texture = CreateTexture(MakeSolidImage(1, 1, 255, 255, 255), plain_color);
    TextureOptions plain_data;
    plain_data.generate_mipmaps = false;
    plain_data.is_color = false;
    // A normal pointing straight out of the surface.
    impl_->flat_normal_texture = CreateTexture(MakeSolidImage(1, 1, 128, 128, 255), plain_data);
    const Image white = MakeSolidImage(1, 1, 255, 255, 255);
    impl_->white_array_texture = CreateTextureArray(&white, 1, plain_color);
    const Image flat_normal = MakeSolidImage(1, 1, 128, 128, 255);
    impl_->flat_normal_array_texture = CreateTextureArray(&flat_normal, 1, plain_data);
    impl_->first_layer_weights = CreateTexture(MakeSolidImage(1, 1, 255, 0, 0, 0), plain_data);
    impl_->no_weights = CreateTexture(MakeSolidImage(1, 1, 0, 0, 0, 0), plain_data);
    {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = 1;
        info.height = 1;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        impl_->no_trample_texture = SDL_CreateGPUTexture(impl_->device, &info);
        if (impl_->no_trample_texture == nullptr) {
            FatalSdlError("cannot create trample texture");
        }
        const std::uint8_t zero = 0;
        impl_->UploadR8(impl_->no_trample_texture, &zero, 1);
    }

    if (config.debug_ui && impl_->window != nullptr) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        // No settings file: it would be written into whatever folder the program was started from.
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();
        // Dear ImGui's colors are meant for the screen as they are, but our output image
        // expects linear values and encodes them when storing. Decode them once here, or
        // every window would come out pale.
        for (ImVec4& color : ImGui::GetStyle().Colors) {
            color.x = SrgbToLinear(color.x);
            color.y = SrgbToLinear(color.y);
            color.z = SrgbToLinear(color.z);
        }
        ImGui_ImplSDL3_InitForSDLGPU(impl_->window);
        ImGui_ImplSDLGPU3_InitInfo init_info;
        init_info.Device = impl_->device;
        init_info.ColorTargetFormat = kOutputFormat;
        init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        if (!ImGui_ImplSDLGPU3_Init(&init_info)) {
            FatalError("Renderer: cannot set up the debug windows");
        }
        impl_->debug_ui_active = true;
    }
}

Renderer::~Renderer() {
    if (impl_->device != nullptr) {
        SDL_WaitForGPUIdle(impl_->device);
        if (impl_->debug_ui_active) {
            ImGui_ImplSDLGPU3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
        }
        for (const Mesh& mesh : impl_->meshes) {
            if (mesh.vertex_buffer != nullptr) {
                SDL_ReleaseGPUBuffer(impl_->device, mesh.vertex_buffer);
                SDL_ReleaseGPUBuffer(impl_->device, mesh.index_buffer);
                if (mesh.skin_buffer != nullptr) {
                    SDL_ReleaseGPUBuffer(impl_->device, mesh.skin_buffer);
                }
            }
        }
        for (SDL_GPUTexture* texture : impl_->textures) {
            if (texture != nullptr) {
                SDL_ReleaseGPUTexture(impl_->device, texture);
            }
        }
        SDL_ReleaseGPUSampler(impl_->device, impl_->texture_sampler);
        SDL_ReleaseGPUSampler(impl_->device, impl_->shadow_sampler);
        SDL_ReleaseGPUSampler(impl_->device, impl_->clamp_sampler);
        SDL_ReleaseGPUSampler(impl_->device, impl_->linear_clamp_sampler);
        SDL_ReleaseGPUSampler(impl_->device, impl_->repeat_sampler);
        for (SDL_GPUGraphicsPipeline* pipeline :
             {impl_->prepass_pipeline, impl_->prepass_pipeline_double_sided, impl_->ao_pipeline, impl_->ao_blur_pipeline,
              impl_->fog_pipeline, impl_->fog_composite_pipeline, impl_->bloom_down_pipeline, impl_->bloom_up_pipeline,
              impl_->luminance_pipeline, impl_->taa_pipeline, impl_->water_pipeline, impl_->depth_copy_pipeline,
              impl_->clouds_pipeline, impl_->cloud_shadow_pipeline, impl_->clouds_composite_pipeline}) {
            SDL_ReleaseGPUGraphicsPipeline(impl_->device, pipeline);
        }
        for (SDL_GPUTexture* texture : {impl_->prepass_depth, impl_->ao_target, impl_->ao_scratch, impl_->fog_target,
                                        impl_->luminance_targets[0], impl_->luminance_targets[1], impl_->taa_targets[0],
                                        impl_->taa_targets[1], impl_->water_scene_copy, impl_->water_depth_copy,
                                        impl_->no_trample_texture, impl_->trample_texture, impl_->cloud_shape, impl_->cloud_detail,
                                        impl_->cloud_weather, impl_->cloud_target, impl_->cloud_shadow_target,
                                        impl_->sky_occlusion_texture}) {
            SDL_ReleaseGPUTexture(impl_->device, texture);
        }
        for (const BloomLevel& level : impl_->bloom_levels) {
            SDL_ReleaseGPUTexture(impl_->device, level.texture);
        }
        SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->mesh_pipelines[kSceneAfterDepthPass][kRigid][0]);
        SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->mesh_pipelines[kSceneAfterDepthPass][kRigid][1]);
        for (int pass = 0; pass < kMeshPassCount; ++pass) {
            for (const int kind : {kSkinned, kInstanced}) {
                // The shadow pass uses one pipeline for both kinds of material.
                SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->mesh_pipelines[pass][kind][0]);
                if (impl_->mesh_pipelines[pass][kind][1] != impl_->mesh_pipelines[pass][kind][0]) {
                    SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->mesh_pipelines[pass][kind][1]);
                }
            }
        }
        if (impl_->instance_buffer != nullptr) {
            SDL_ReleaseGPUBuffer(impl_->device, impl_->instance_buffer);
        }
        for (SDL_GPUTransferBuffer* transfer : {impl_->instance_transfer, impl_->joint_transfer}) {
            if (transfer != nullptr) {
                SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
            }
        }
        for (SDL_GPUGraphicsPipeline* pipeline : impl_->terrain_pipelines) {
            if (pipeline != nullptr) {
                SDL_ReleaseGPUGraphicsPipeline(impl_->device, pipeline);
            }
        }
        for (SDL_GPUGraphicsPipeline* pipeline : impl_->impostor_pipelines) {
            if (pipeline != nullptr) {
                SDL_ReleaseGPUGraphicsPipeline(impl_->device, pipeline);
            }
        }
        for (const TerrainData& terrain : impl_->terrains) {
            if (terrain.height_texture != nullptr) {
                SDL_ReleaseGPUTexture(impl_->device, terrain.height_texture);
            }
        }
        if (impl_->joint_buffer != nullptr) {
            SDL_ReleaseGPUBuffer(impl_->device, impl_->joint_buffer);
        }
        SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->mesh_pipeline);
        SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->mesh_pipeline_double_sided);
        SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->shadow_pipeline);
        SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->sky_pipeline);
        SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->tonemap_pipeline);
        SDL_ReleaseGPUTexture(impl_->device, impl_->shadow_atlas);
        SDL_ReleaseGPUTexture(impl_->device, impl_->output_target);
        SDL_ReleaseGPUTexture(impl_->device, impl_->scene_depth);
        if (impl_->scene_resolved != nullptr) {
            SDL_ReleaseGPUTexture(impl_->device, impl_->scene_resolved);
        }
        SDL_ReleaseGPUTexture(impl_->device, impl_->scene_color);
        if (impl_->window != nullptr) {
            SDL_ReleaseWindowFromGPUDevice(impl_->device, impl_->window);
            SDL_DestroyWindow(impl_->window);
        }
        SDL_DestroyGPUDevice(impl_->device);
    }
    SDL_Quit();
}

MeshHandle Renderer::CreateMesh(const MeshVertex* vertices, int vertex_count, const std::uint32_t* indices,
                                int index_count) {
    if (vertices == nullptr || indices == nullptr || vertex_count < 3 || index_count < 3 || index_count % 3 != 0) {
        FatalError("Renderer::CreateMesh: needs at least one triangle and an index count divisible by 3");
    }
    for (int i = 0; i < index_count; ++i) {
        if (indices[i] >= static_cast<std::uint32_t>(vertex_count)) {
            FatalError("Renderer::CreateMesh: an index points past the last vertex");
        }
    }

    Mesh mesh;
    mesh.vertex_buffer = impl_->CreateBufferWithData(
        SDL_GPU_BUFFERUSAGE_VERTEX, vertices, static_cast<Uint32>(static_cast<size_t>(vertex_count) * sizeof(MeshVertex)));
    mesh.index_buffer = impl_->CreateBufferWithData(
        SDL_GPU_BUFFERUSAGE_INDEX, indices, static_cast<Uint32>(static_cast<size_t>(index_count) * sizeof(std::uint32_t)));
    mesh.index_count = static_cast<Uint32>(index_count);
    mesh.bounds_min = vertices[0].position;
    mesh.bounds_max = vertices[0].position;
    for (int i = 1; i < vertex_count; ++i) {
        mesh.bounds_min = glm::min(mesh.bounds_min, vertices[i].position);
        mesh.bounds_max = glm::max(mesh.bounds_max, vertices[i].position);
    }
    impl_->meshes.push_back(mesh);

    MeshHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->meshes.size());
    return handle;
}

MeshHandle Renderer::CreateSkinnedMesh(const MeshVertex* vertices, const SkinVertex* skin, int vertex_count,
                                       const std::uint32_t* indices, int index_count) {
    if (skin == nullptr) {
        FatalError("Renderer::CreateSkinnedMesh: needs joints and weights for every vertex");
    }
    const MeshHandle handle = CreateMesh(vertices, vertex_count, indices, index_count);
    impl_->meshes[handle.id - 1].skin_buffer = impl_->CreateBufferWithData(
        SDL_GPU_BUFFERUSAGE_VERTEX, skin, static_cast<Uint32>(static_cast<size_t>(vertex_count) * sizeof(SkinVertex)));
    return handle;
}

MeshHandle Renderer::CreateCubeMesh() {
    // Six faces, four vertices each, so every face has its own flat normal and its own texture corners.
    struct Face {
        glm::vec3 normal;
        glm::vec3 right;
        glm::vec3 up;
    };
    const Face faces[6] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},  {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };

    MeshVertex vertices[24];
    std::uint32_t indices[36];
    for (int face_index = 0; face_index < 6; ++face_index) {
        const Face& face = faces[face_index];
        const glm::vec3 center = face.normal * 0.5f;
        const glm::vec3 half_right = face.right * 0.5f;
        const glm::vec3 half_up = face.up * 0.5f;
        // Seen from outside: bottom-left, bottom-right, top-right, top-left.
        MeshVertex* corner = &vertices[face_index * 4];
        corner[0] = {center - half_right - half_up, face.normal, {0.0f, 1.0f}};
        corner[1] = {center + half_right - half_up, face.normal, {1.0f, 1.0f}};
        corner[2] = {center + half_right + half_up, face.normal, {1.0f, 0.0f}};
        corner[3] = {center - half_right + half_up, face.normal, {0.0f, 0.0f}};

        const std::uint32_t base = static_cast<std::uint32_t>(face_index * 4);
        std::uint32_t* triangle = &indices[face_index * 6];
        triangle[0] = base;
        triangle[1] = base + 1;
        triangle[2] = base + 2;
        triangle[3] = base;
        triangle[4] = base + 2;
        triangle[5] = base + 3;
    }
    return CreateMesh(vertices, 24, indices, 36);
}

MeshHandle Renderer::CreatePlaneMesh() {
    const glm::vec3 up{0, 1, 0};
    const MeshVertex vertices[4] = {
        {{-0.5f, 0, 0.5f}, up, {0.0f, 1.0f}},
        {{0.5f, 0, 0.5f}, up, {1.0f, 1.0f}},
        {{0.5f, 0, -0.5f}, up, {1.0f, 0.0f}},
        {{-0.5f, 0, -0.5f}, up, {0.0f, 0.0f}},
    };
    const std::uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    return CreateMesh(vertices, 4, indices, 6);
}

TextureHandle Renderer::CreateTexture(const Image& image, const TextureOptions& options) {
    if (image.width < 1 || image.height < 1 ||
        image.pixels.size() != static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4) {
        FatalError("Renderer::CreateTexture: the image is empty or its pixel data has the wrong size");
    }
    if (impl_->in_frame) {
        FatalError("Renderer::CreateTexture: do not create textures between BeginFrame and EndFrame");
    }

    const Uint32 level_count = options.generate_mipmaps ? CountMipLevels(image.width, image.height) : 1;

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = options.is_color ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(image.width);
    info.height = static_cast<Uint32>(image.height);
    info.layer_count_or_depth = 1;
    info.num_levels = level_count;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(impl_->device, &info);
    if (texture == nullptr) {
        FatalSdlError("cannot create texture");
    }

    // Mipmap levels are computed here, on the CPU, and uploaded one by one, so their pixels are
    // the same on every backend and driver by construction (see MakeHalfSizeImage).
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
    if (commands == nullptr) {
        FatalSdlError("cannot acquire command buffer");
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    std::vector<SDL_GPUTransferBuffer*> transfers;
    // The chain of halvings is built from unscaled images; a level whose alpha is scaled for
    // coverage is a copy, so that the scaling does not compound into the next level.
    Image smaller_level;
    Image scaled_level;
    const Image* level_image = &image;
    const float coverage = options.coverage_cutoff > 0.0f ? MeasureAlphaCoverage(image, options.coverage_cutoff) : 0.0f;
    for (Uint32 level = 0; level < level_count; ++level) {
        if (level > 0) {
            smaller_level = MakeHalfSizeImage(level_image == &scaled_level ? smaller_level : *level_image, options.is_color);
            level_image = &smaller_level;
            if (options.coverage_cutoff > 0.0f) {
                scaled_level = smaller_level;
                ScaleAlphaToCoverage(scaled_level, options.coverage_cutoff, coverage);
                level_image = &scaled_level;
            }
        }
        SDL_GPUTransferBuffer* transfer = impl_->CreateFilledTransferBuffer(
            level_image->pixels.data(), static_cast<Uint32>(level_image->pixels.size()));
        transfers.push_back(transfer);

        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.pixels_per_row = static_cast<Uint32>(level_image->width);
        source.rows_per_layer = static_cast<Uint32>(level_image->height);
        SDL_GPUTextureRegion destination{};
        destination.texture = texture;
        destination.mip_level = level;
        destination.w = static_cast<Uint32>(level_image->width);
        destination.h = static_cast<Uint32>(level_image->height);
        destination.d = 1;
        SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    }
    SDL_EndGPUCopyPass(copy_pass);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        FatalSdlError("cannot submit texture upload");
    }
    for (SDL_GPUTransferBuffer* transfer : transfers) {
        SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
    }

    impl_->textures.push_back(texture);
    impl_->texture_layers.push_back(0);
    impl_->texture_sizes.emplace_back(static_cast<float>(image.width), static_cast<float>(image.height));
    TextureHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->textures.size());
    return handle;
}

namespace {

SDL_GPUTextureFormat ToGpuFormat(const CompressedImage& image) {
    switch (image.format) {
    case TextureFormat::Bc1:
        return image.is_color ? SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    case TextureFormat::Bc3:
        return image.is_color ? SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    case TextureFormat::Bc4:
        return SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM;
    case TextureFormat::Bc5:
        return SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM;
    case TextureFormat::Rgba8:
        return image.is_color ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    }
    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}

void CheckCompressedImage(const CompressedImage& image, const char* what) {
    bool valid = image.width > 0 && image.height > 0 && !image.levels.empty() && image.levels.size() <= 16;
    if (valid && image.format != TextureFormat::Rgba8) {
        valid = image.width % 4 == 0 && image.height % 4 == 0;
    }
    int width = image.width;
    int height = image.height;
    for (size_t level = 0; valid && level < image.levels.size(); ++level) {
        valid = image.levels[level].size() == GetCompressedLevelSize(image.format, width, height);
        width = glm::max(width / 2, 1);
        height = glm::max(height / 2, 1);
    }
    if (!valid) {
        const std::string message = std::string(what) + ": the compressed image is empty, not a multiple of 4 wide and high, or its levels have the wrong size";
        FatalError(message.c_str());
    }
}

} // namespace

TextureHandle Renderer::CreateTexture(const CompressedImage& image) {
    return CreateTextureArray(&image, 1, true);
}

TextureHandle Renderer::CreateTextureArray(const CompressedImage* images, int count) {
    return CreateTextureArray(images, count, false);
}

TextureHandle Renderer::CreateTextureArray(const CompressedImage* images, int count, bool plain) {
    if (images == nullptr || count < 1 || count > 256) {
        FatalError("Renderer::CreateTextureArray: needs between 1 and 256 images");
    }
    for (int i = 0; i < count; ++i) {
        CheckCompressedImage(images[i], "Renderer::CreateTexture");
        if (images[i].width != images[0].width || images[i].height != images[0].height || images[i].format != images[0].format ||
            images[i].is_color != images[0].is_color || images[i].levels.size() != images[0].levels.size()) {
            FatalError("Renderer::CreateTextureArray: the images must share size, format and number of levels");
        }
    }
    if (impl_->in_frame) {
        FatalError("Renderer::CreateTexture: do not create textures between BeginFrame and EndFrame");
    }
    SDL_GPUTextureCreateInfo info{};
    info.type = plain ? SDL_GPU_TEXTURETYPE_2D : SDL_GPU_TEXTURETYPE_2D_ARRAY;
    info.format = ToGpuFormat(images[0]);
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(images[0].width);
    info.height = static_cast<Uint32>(images[0].height);
    info.layer_count_or_depth = static_cast<Uint32>(count);
    info.num_levels = static_cast<Uint32>(images[0].levels.size());
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(impl_->device, &info);
    if (texture == nullptr) {
        FatalSdlError("cannot create compressed texture");
    }
    for (int layer = 0; layer < count; ++layer) {
        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
        if (commands == nullptr) {
            FatalSdlError("cannot acquire command buffer");
        }
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
        std::vector<SDL_GPUTransferBuffer*> transfers;
        int width = images[layer].width;
        int height = images[layer].height;
        for (size_t level = 0; level < images[layer].levels.size(); ++level) {
            const std::vector<std::uint8_t>& bytes = images[layer].levels[level];
            SDL_GPUTransferBuffer* transfer = impl_->CreateFilledTransferBuffer(bytes.data(), static_cast<Uint32>(bytes.size()));
            transfers.push_back(transfer);
            SDL_GPUTextureTransferInfo source{};
            source.transfer_buffer = transfer;
            SDL_GPUTextureRegion destination{};
            destination.texture = texture;
            destination.mip_level = static_cast<Uint32>(level);
            destination.layer = static_cast<Uint32>(layer);
            destination.w = static_cast<Uint32>(width);
            destination.h = static_cast<Uint32>(height);
            destination.d = 1;
            SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
            width = glm::max(width / 2, 1);
            height = glm::max(height / 2, 1);
        }
        SDL_EndGPUCopyPass(copy_pass);
        if (!SDL_SubmitGPUCommandBuffer(commands)) {
            FatalSdlError("cannot submit compressed texture upload");
        }
        for (SDL_GPUTransferBuffer* transfer : transfers) {
            SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
        }
    }
    impl_->textures.push_back(texture);
    impl_->texture_layers.push_back(plain ? 0 : count);
    impl_->texture_sizes.emplace_back(static_cast<float>(images[0].width), static_cast<float>(images[0].height));
    TextureHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->textures.size());
    return handle;
}

TextureHandle Renderer::CreateTextureArray(const Image* images, int count, const TextureOptions& options) {
    if (images == nullptr || count < 1 || count > 256) {
        FatalError("Renderer::CreateTextureArray: needs between 1 and 256 images");
    }
    for (int i = 0; i < count; ++i) {
        if (images[i].width != images[0].width || images[i].height != images[0].height || images[i].width < 1 ||
            images[i].pixels.size() != static_cast<size_t>(images[i].width) * static_cast<size_t>(images[i].height) * 4) {
            FatalError("Renderer::CreateTextureArray: the images must be non-empty and all of one size");
        }
    }
    if (impl_->in_frame) {
        FatalError("Renderer::CreateTextureArray: do not create textures between BeginFrame and EndFrame");
    }
    const Uint32 level_count = options.generate_mipmaps ? CountMipLevels(images[0].width, images[0].height) : 1;
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
    info.format = options.is_color ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(images[0].width);
    info.height = static_cast<Uint32>(images[0].height);
    info.layer_count_or_depth = static_cast<Uint32>(count);
    info.num_levels = level_count;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(impl_->device, &info);
    if (texture == nullptr) {
        FatalSdlError("cannot create texture array");
    }
    // One image at a time, with its mipmaps computed on the CPU as for CreateTexture.
    for (int layer = 0; layer < count; ++layer) {
        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
        if (commands == nullptr) {
            FatalSdlError("cannot acquire command buffer");
        }
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
        std::vector<SDL_GPUTransferBuffer*> transfers;
        Image smaller_level;
        const Image* level_image = &images[layer];
        for (Uint32 level = 0; level < level_count; ++level) {
            if (level > 0) {
                smaller_level = MakeHalfSizeImage(*level_image, options.is_color);
                level_image = &smaller_level;
            }
            SDL_GPUTransferBuffer* transfer =
                impl_->CreateFilledTransferBuffer(level_image->pixels.data(), static_cast<Uint32>(level_image->pixels.size()));
            transfers.push_back(transfer);
            SDL_GPUTextureTransferInfo source{};
            source.transfer_buffer = transfer;
            source.pixels_per_row = static_cast<Uint32>(level_image->width);
            source.rows_per_layer = static_cast<Uint32>(level_image->height);
            SDL_GPUTextureRegion destination{};
            destination.texture = texture;
            destination.mip_level = level;
            destination.layer = static_cast<Uint32>(layer);
            destination.w = static_cast<Uint32>(level_image->width);
            destination.h = static_cast<Uint32>(level_image->height);
            destination.d = 1;
            SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
        }
        SDL_EndGPUCopyPass(copy_pass);
        if (!SDL_SubmitGPUCommandBuffer(commands)) {
            FatalSdlError("cannot submit texture array upload");
        }
        for (SDL_GPUTransferBuffer* transfer : transfers) {
            SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
        }
    }
    impl_->textures.push_back(texture);
    impl_->texture_layers.push_back(count);
    impl_->texture_sizes.emplace_back(static_cast<float>(images[0].width), static_cast<float>(images[0].height));
    TextureHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->textures.size());
    return handle;
}

void Renderer::DestroyMesh(MeshHandle mesh) {
    if (impl_->in_frame) {
        FatalError("Renderer::DestroyMesh: do not destroy meshes between BeginFrame and EndFrame");
    }
    if (mesh.id == 0 || mesh.id > impl_->meshes.size() || impl_->meshes[mesh.id - 1].vertex_buffer == nullptr) {
        FatalError("Renderer::DestroyMesh: invalid or already destroyed mesh handle");
    }
    Mesh& entry = impl_->meshes[mesh.id - 1];
    SDL_ReleaseGPUBuffer(impl_->device, entry.vertex_buffer);
    SDL_ReleaseGPUBuffer(impl_->device, entry.index_buffer);
    if (entry.skin_buffer != nullptr) {
        SDL_ReleaseGPUBuffer(impl_->device, entry.skin_buffer);
    }
    entry = Mesh{};
}

void Renderer::DestroyTexture(TextureHandle texture) {
    if (impl_->in_frame) {
        FatalError("Renderer::DestroyTexture: do not destroy textures between BeginFrame and EndFrame");
    }
    if (texture.id == 0 || texture.id > impl_->textures.size() || impl_->textures[texture.id - 1] == nullptr) {
        FatalError("Renderer::DestroyTexture: invalid or already destroyed texture handle");
    }
    SDL_ReleaseGPUTexture(impl_->device, impl_->textures[texture.id - 1]);
    impl_->textures[texture.id - 1] = nullptr;
}

void Renderer::BeginFrame(const Camera& camera, const DirectionalLight& light, const Environment& environment) {
    if (impl_->in_frame) {
        FatalError("Renderer::BeginFrame: the previous frame was not finished with EndFrame");
    }
    impl_->in_frame = true;
    impl_->camera = camera;
    impl_->light = light;
    impl_->environment = environment;
    impl_->draw_commands.clear();
    impl_->water_commands.clear();
    // The sky occlusion map, copied to the card when it changed.
    if (impl_->sky_occlusion_map != nullptr && impl_->sky_occlusion_map->resolution >= 2) {
        const SkyOcclusionMap& map = *impl_->sky_occlusion_map;
        if (impl_->sky_occlusion_texture == nullptr || impl_->sky_occlusion_resolution != map.resolution) {
            if (impl_->sky_occlusion_texture != nullptr) {
                SDL_ReleaseGPUTexture(impl_->device, impl_->sky_occlusion_texture);
            }
            SDL_GPUTextureCreateInfo info{};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
            info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            info.width = static_cast<Uint32>(map.resolution);
            info.height = static_cast<Uint32>(map.resolution);
            info.layer_count_or_depth = 1;
            info.num_levels = 1;
            impl_->sky_occlusion_texture = SDL_CreateGPUTexture(impl_->device, &info);
            if (impl_->sky_occlusion_texture == nullptr) {
                FatalSdlError("cannot create sky occlusion texture");
            }
            impl_->sky_occlusion_resolution = map.resolution;
            impl_->sky_occlusion_version = 0xFFFFFFFFu;
        }
        if (impl_->sky_occlusion_version != map.version) {
            impl_->UploadR8(impl_->sky_occlusion_texture, map.values.data(), map.resolution);
            impl_->sky_occlusion_version = map.version;
        }
    }
    if (impl_->environment.clouds && impl_->environment.draw_sky) {
        impl_->CreateCloudTextures();
    }
    // The trample map, copied to the card when it changed.
    if (impl_->trample_map == nullptr) {
        impl_->trample_info = glm::vec4(0.0f);
    } else {
        const TrampleMap& map = *impl_->trample_map;
        if (impl_->trample_texture == nullptr || impl_->trample_resolution != map.GetResolution()) {
            if (impl_->trample_texture != nullptr) {
                SDL_ReleaseGPUTexture(impl_->device, impl_->trample_texture);
            }
            SDL_GPUTextureCreateInfo info{};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
            info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            info.width = static_cast<Uint32>(map.GetResolution());
            info.height = static_cast<Uint32>(map.GetResolution());
            info.layer_count_or_depth = 1;
            info.num_levels = 1;
            impl_->trample_texture = SDL_CreateGPUTexture(impl_->device, &info);
            if (impl_->trample_texture == nullptr) {
                FatalSdlError("cannot create trample texture");
            }
            impl_->trample_resolution = map.GetResolution();
            impl_->trample_version = 0xFFFFFFFFu;
        }
        if (impl_->trample_version != map.GetVersion()) {
            impl_->UploadR8(impl_->trample_texture, map.GetBytes().data(), map.GetResolution());
            impl_->trample_version = map.GetVersion();
        }
        impl_->trample_info = glm::vec4(map.GetOrigin().x, map.GetOrigin().y, 1.0f / map.GetSize(), 1.0f);
    }
    impl_->joint_palette.clear();
    impl_->instance_sources.clear();
    impl_->instance_batches.clear();
    if (impl_->debug_ui_active) {
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }
}

void Renderer::DrawMesh(MeshHandle mesh_handle, const glm::mat4& transform, const Material& material) {
    if (!impl_->in_frame) {
        FatalError("Renderer::DrawMesh: call BeginFrame first");
    }
    if (mesh_handle.id == 0 || mesh_handle.id > impl_->meshes.size() ||
        impl_->meshes[mesh_handle.id - 1].vertex_buffer == nullptr) {
        FatalError("Renderer::DrawMesh: invalid or destroyed mesh handle");
    }
    for (const TextureHandle texture : {material.texture, material.normal_texture, material.orm_texture}) {
        if (texture.id > impl_->textures.size() || (texture.id != 0 && impl_->textures[texture.id - 1] == nullptr)) {
            FatalError("Renderer::DrawMesh: invalid or destroyed texture handle");
        }
    }
    const Mesh& mesh = impl_->meshes[mesh_handle.id - 1];
    if (mesh.skin_buffer != nullptr) {
        FatalError("Renderer::DrawMesh: this mesh is skinned, draw it with DrawSkinnedMesh");
    }
    DrawCommand command;
    command.mesh_id = mesh_handle.id;
    command.transform = transform;
    command.material = material;
    command.bounds_min = mesh.bounds_min;
    command.bounds_max = mesh.bounds_max;
    impl_->draw_commands.push_back(command);
}

void Renderer::DrawMeshInstances(MeshHandle mesh_handle, const Material& material, const glm::mat4* transforms, int count,
                                 const InstanceOptions& options) {
    if (!impl_->in_frame) {
        FatalError("Renderer::DrawMeshInstances: call BeginFrame first");
    }
    if (mesh_handle.id == 0 || mesh_handle.id > impl_->meshes.size() ||
        impl_->meshes[mesh_handle.id - 1].vertex_buffer == nullptr) {
        FatalError("Renderer::DrawMeshInstances: invalid or destroyed mesh handle");
    }
    if (impl_->meshes[mesh_handle.id - 1].skin_buffer != nullptr) {
        FatalError("Renderer::DrawMeshInstances: skinned meshes cannot be instanced");
    }
    if (count < 0 || (count > 0 && transforms == nullptr)) {
        FatalError("Renderer::DrawMeshInstances: needs one transform per instance");
    }
    for (const TextureHandle texture : {material.texture, material.normal_texture, material.orm_texture}) {
        if (texture.id > impl_->textures.size() || (texture.id != 0 && impl_->textures[texture.id - 1] == nullptr)) {
            FatalError("Renderer::DrawMeshInstances: invalid or destroyed texture handle");
        }
    }
    if (count == 0) {
        return;
    }
    InstanceBatch batch{};
    batch.mesh_id = mesh_handle.id;
    batch.material = material;
    batch.first_source = impl_->instance_sources.size();
    batch.count = static_cast<std::uint32_t>(count);
    batch.options = options;
    impl_->instance_sources.insert(impl_->instance_sources.end(), transforms, transforms + count);
    impl_->instance_batches.push_back(batch);
}

void Renderer::BakeImpostor(const ImpostorPart* parts, int count, int frames_per_side, int frame_size, Impostor& impostor) {
    if (impl_->in_frame) {
        FatalError("Renderer::BakeImpostor: call it outside BeginFrame/EndFrame");
    }
    if (parts == nullptr || count < 1 || frames_per_side < 2 || frames_per_side > 32 || frame_size < 8 ||
        frames_per_side * frame_size > 8192) {
        FatalError("Renderer::BakeImpostor: needs parts, 2 to 32 frames per side and at most 8192 pixels per side");
    }
    // The box around all parts gives the sphere every frame shows.
    glm::vec3 low(std::numeric_limits<float>::max());
    glm::vec3 high(-std::numeric_limits<float>::max());
    for (int i = 0; i < count; ++i) {
        const ImpostorPart& part = parts[i];
        if (part.mesh.id == 0 || part.mesh.id > impl_->meshes.size() || impl_->meshes[part.mesh.id - 1].vertex_buffer == nullptr ||
            impl_->meshes[part.mesh.id - 1].skin_buffer != nullptr) {
            FatalError("Renderer::BakeImpostor: invalid, destroyed or skinned mesh");
        }
        const Mesh& mesh = impl_->meshes[part.mesh.id - 1];
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 local((corner & 1) ? mesh.bounds_max.x : mesh.bounds_min.x,
                                  (corner & 2) ? mesh.bounds_max.y : mesh.bounds_min.y,
                                  (corner & 4) ? mesh.bounds_max.z : mesh.bounds_min.z);
            const glm::vec3 position = glm::vec3(part.transform * glm::vec4(local, 1.0f));
            low = glm::min(low, position);
            high = glm::max(high, position);
        }
    }
    const glm::vec3 center = (low + high) * 0.5f;
    // A little room, so that filtering never reaches into the neighboring frame.
    const float radius = glm::length(high - low) * 0.5f * 1.02f;

    Impl& impl = *impl_;
    // Pipelines that write base color and normal into two images. Made here, used once.
    SDL_GPUShader* vertex_shader = impl.CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0, 2, kMeshVertDxil, sizeof(kMeshVertDxil),
                                                     kMeshVertSpirv, sizeof(kMeshVertSpirv));
    SDL_GPUShader* fragment_shader =
        impl.CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1, kImpostorBakeFragDxil, sizeof(kImpostorBakeFragDxil),
                          kImpostorBakeFragSpirv, sizeof(kImpostorBakeFragSpirv));
    SDL_GPUVertexBufferDescription vertex_buffer{};
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(MeshVertex);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[3]{};
    attributes[0].location = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(MeshVertex, position);
    attributes[1].location = 1;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].offset = offsetof(MeshVertex, normal);
    attributes[2].location = 2;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[2].offset = offsetof(MeshVertex, uv);
    SDL_GPUColorTargetDescription target_descriptions[2]{};
    target_descriptions[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    target_descriptions[1].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex_shader;
    info.fragment_shader = fragment_shader;
    info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 3;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    info.target_info.color_target_descriptions = target_descriptions;
    info.target_info.num_color_targets = 2;
    info.target_info.depth_stencil_format = kDepthFormat;
    info.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline* pipelines[2] = {SDL_CreateGPUGraphicsPipeline(impl.device, &info), nullptr};
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipelines[1] = SDL_CreateGPUGraphicsPipeline(impl.device, &info);
    SDL_ReleaseGPUShader(impl.device, vertex_shader);
    SDL_ReleaseGPUShader(impl.device, fragment_shader);
    if (pipelines[0] == nullptr || pipelines[1] == nullptr) {
        FatalSdlError("cannot create impostor baking pipeline");
    }

    const int size = frames_per_side * frame_size;
    const SDL_FColor transparent{0.0f, 0.0f, 0.0f, 0.0f};
    const SDL_GPUTextureUsageFlags color_usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture* color_target = impl.CreateTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, color_usage, size, size,
                                                     SDL_GPU_SAMPLECOUNT_1, &transparent, "impostor color image");
    SDL_GPUTexture* normal_target = impl.CreateTarget(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, color_usage, size, size,
                                                      SDL_GPU_SAMPLECOUNT_1, &transparent, "impostor normal image");
    SDL_GPUTexture* depth_target = impl.CreateTarget(kDepthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, size, size,
                                                     SDL_GPU_SAMPLECOUNT_1, nullptr, "impostor depth image");

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl.device);
    if (commands == nullptr) {
        FatalSdlError("cannot acquire command buffer");
    }
    SDL_GPUColorTargetInfo color_infos[2]{};
    for (int i = 0; i < 2; ++i) {
        color_infos[i].texture = i == 0 ? color_target : normal_target;
        color_infos[i].clear_color = transparent;
        color_infos[i].load_op = SDL_GPU_LOADOP_CLEAR;
        color_infos[i].store_op = SDL_GPU_STOREOP_STORE;
    }
    SDL_GPUDepthStencilTargetInfo depth_info{};
    depth_info.texture = depth_target;
    depth_info.clear_depth = 1.0f;
    depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_info.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, color_infos, 2, &depth_info);
    if (pass == nullptr) {
        FatalSdlError("cannot begin impostor baking pass");
    }
    SDL_GPUGraphicsPipeline* bound_pipeline = nullptr;
    for (int row = 0; row < frames_per_side; ++row) {
        for (int column = 0; column < frames_per_side; ++column) {
            // The camera for this frame looks at the model's middle from the frame's
            // direction, without perspective, showing exactly the sphere.
            const glm::vec2 grid = glm::vec2(static_cast<float>(column), static_cast<float>(row)) /
                                       static_cast<float>(frames_per_side - 1) * 2.0f -
                                   1.0f;
            const glm::vec3 direction = HemiOctahedronDirection(grid);
            glm::vec3 right;
            glm::vec3 up;
            ImpostorFrameAxes(direction, right, up);
            const glm::mat4 view = glm::lookAt(center + direction * (radius * 2.0f), center, up);
            const glm::mat4 projection = glm::ortho(-radius, radius, -radius, radius, 0.0f, radius * 4.0f);
            VertexFrameUniforms frame_uniforms;
            frame_uniforms.view_projection = projection * view;
            SDL_PushGPUVertexUniformData(commands, 0, &frame_uniforms, sizeof(frame_uniforms));

            SDL_GPUViewport viewport{};
            viewport.x = static_cast<float>(column * frame_size);
            viewport.y = static_cast<float>(row * frame_size);
            viewport.w = static_cast<float>(frame_size);
            viewport.h = static_cast<float>(frame_size);
            viewport.min_depth = 0.0f;
            viewport.max_depth = 1.0f;
            SDL_SetGPUViewport(pass, &viewport);

            for (int i = 0; i < count; ++i) {
                const ImpostorPart& part = parts[i];
                SDL_GPUGraphicsPipeline* pipeline = pipelines[part.material.double_sided ? 1 : 0];
                if (pipeline != bound_pipeline) {
                    SDL_BindGPUGraphicsPipeline(pass, pipeline);
                    bound_pipeline = pipeline;
                }
                ObjectUniforms object_uniforms;
                object_uniforms.model = part.transform;
                object_uniforms.color = ToVec4(part.material.color);
                object_uniforms.uv_scale = glm::vec4(part.material.uv_scale, 0.0f, 0.0f);
                SDL_PushGPUVertexUniformData(commands, 1, &object_uniforms, sizeof(object_uniforms));
                const MaterialUniforms material_uniforms = Impl::MakeMaterialUniforms(part.material);
                SDL_PushGPUFragmentUniformData(commands, 0, &material_uniforms, sizeof(material_uniforms));
                SDL_GPUTextureSamplerBinding bindings[2]{};
                bindings[0].texture = impl.GetTexture(part.material.texture, impl.white_texture);
                bindings[0].sampler = impl.texture_sampler;
                bindings[1].texture = impl.GetTexture(part.material.normal_texture, impl.flat_normal_texture);
                bindings[1].sampler = impl.texture_sampler;
                SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);

                const Mesh& mesh = impl.meshes[part.mesh.id - 1];
                SDL_GPUBufferBinding vertex_binding{};
                vertex_binding.buffer = mesh.vertex_buffer;
                SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
                SDL_GPUBufferBinding index_binding{};
                index_binding.buffer = mesh.index_buffer;
                SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count, 1, 0, 0, 0);
            }
        }
    }
    SDL_EndGPURenderPass(pass);

    // Both images back to the CPU.
    const Uint32 byte_count = static_cast<Uint32>(size) * static_cast<Uint32>(size) * 4;
    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transfer_info.size = byte_count * 2;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(impl.device, &transfer_info);
    if (transfer == nullptr) {
        FatalSdlError("cannot create download transfer buffer");
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    for (int i = 0; i < 2; ++i) {
        SDL_GPUTextureRegion source{};
        source.texture = i == 0 ? color_target : normal_target;
        source.w = static_cast<Uint32>(size);
        source.h = static_cast<Uint32>(size);
        source.d = 1;
        SDL_GPUTextureTransferInfo destination{};
        destination.transfer_buffer = transfer;
        destination.offset = byte_count * static_cast<Uint32>(i);
        destination.pixels_per_row = static_cast<Uint32>(size);
        destination.rows_per_layer = static_cast<Uint32>(size);
        SDL_DownloadFromGPUTexture(copy_pass, &source, &destination);
    }
    SDL_EndGPUCopyPass(copy_pass);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    if (fence == nullptr || !SDL_WaitForGPUFences(impl.device, true, &fence, 1)) {
        FatalSdlError("cannot finish impostor baking");
    }
    SDL_ReleaseGPUFence(impl.device, fence);
    const std::uint8_t* pixels = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(impl.device, transfer, false));
    if (pixels == nullptr) {
        FatalSdlError("cannot map download transfer buffer");
    }
    impostor.color.width = size;
    impostor.color.height = size;
    impostor.color.pixels.assign(pixels, pixels + byte_count);
    impostor.normal.width = size;
    impostor.normal.height = size;
    impostor.normal.pixels.assign(pixels + byte_count, pixels + byte_count * 2);
    SDL_UnmapGPUTransferBuffer(impl.device, transfer);
    SDL_ReleaseGPUTransferBuffer(impl.device, transfer);
    SDL_ReleaseGPUTexture(impl.device, color_target);
    SDL_ReleaseGPUTexture(impl.device, normal_target);
    SDL_ReleaseGPUTexture(impl.device, depth_target);
    SDL_ReleaseGPUGraphicsPipeline(impl.device, pipelines[0]);
    SDL_ReleaseGPUGraphicsPipeline(impl.device, pipelines[1]);

    // Filtering reads across the model's outline and into smaller copies of the images, so
    // empty pixels get the colors and directions of their neighbors.
    BleedColorsIntoTransparentPixels(impostor.color, frame_size / 4);
    BleedColorsIntoTransparentPixels(impostor.normal, frame_size / 4);
    impostor.frames_per_side = frames_per_side;
    impostor.center = center;
    impostor.radius = radius;
}

ImpostorHandle Renderer::CreateImpostor(const Impostor& impostor) {
    if (impostor.frames_per_side < 2 || impostor.radius <= 0.0f || impostor.color.width < 2 ||
        impostor.color.width != impostor.color.height || impostor.normal.width != impostor.color.width ||
        impostor.normal.height != impostor.color.height) {
        FatalError("Renderer::CreateImpostor: needs square color and normal pictures of one size, 2 or more frames and a radius");
    }
    if (impl_->impostor_card.id == 0) {
        const MeshVertex corners[4] = {{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
                                       {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                                       {{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                                       {{-1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}};
        const std::uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
        impl_->impostor_card = CreateMesh(corners, 4, indices, 6);
    }
    TextureOptions color_options;
    color_options.is_color = true;
    color_options.coverage_cutoff = kImpostorAlphaCutoff;
    TextureOptions normal_options;
    normal_options.is_color = false;
    ImpostorData data;
    data.color = CreateTexture(impostor.color, color_options);
    data.normal = CreateTexture(impostor.normal, normal_options);
    data.center = impostor.center;
    data.radius = impostor.radius;
    data.frames_per_side = impostor.frames_per_side;
    impl_->impostors.push_back(data);
    ImpostorHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->impostors.size());
    return handle;
}

ImpostorHandle Renderer::CreateImpostor(const CompressedImage& color, const CompressedImage& normal, int frames_per_side,
                                        const glm::vec3& center, float radius) {
    if (frames_per_side < 2 || radius <= 0.0f || color.width < 4 || color.width != color.height || normal.width != color.width ||
        normal.height != color.height) {
        FatalError("Renderer::CreateImpostor: needs square color and normal pictures of one size, 2 or more frames and a radius");
    }
    if (impl_->impostor_card.id == 0) {
        const MeshVertex corners[4] = {{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
                                       {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                                       {{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                                       {{-1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}};
        const std::uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
        impl_->impostor_card = CreateMesh(corners, 4, indices, 6);
    }
    ImpostorData data;
    data.color = CreateTexture(color);
    data.normal = CreateTexture(normal);
    data.center = center;
    data.radius = radius;
    data.frames_per_side = frames_per_side;
    impl_->impostors.push_back(data);
    ImpostorHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->impostors.size());
    return handle;
}

void Renderer::DestroyImpostor(ImpostorHandle impostor) {
    if (impl_->in_frame) {
        FatalError("Renderer::DestroyImpostor: do not destroy impostors between BeginFrame and EndFrame");
    }
    if (impostor.id == 0 || impostor.id > impl_->impostors.size() || impl_->impostors[impostor.id - 1].color.id == 0) {
        FatalError("Renderer::DestroyImpostor: invalid or already destroyed impostor handle");
    }
    ImpostorData& data = impl_->impostors[impostor.id - 1];
    DestroyTexture(data.color);
    DestroyTexture(data.normal);
    data = ImpostorData{};
}

void Renderer::DrawImpostorInstances(ImpostorHandle impostor, const glm::mat4* transforms, int count,
                                     const InstanceOptions& options) {
    if (!impl_->in_frame) {
        FatalError("Renderer::DrawImpostorInstances: call BeginFrame first");
    }
    if (impostor.id == 0 || impostor.id > impl_->impostors.size() || impl_->impostors[impostor.id - 1].color.id == 0) {
        FatalError("Renderer::DrawImpostorInstances: invalid or destroyed impostor handle");
    }
    if (count < 0 || (count > 0 && transforms == nullptr)) {
        FatalError("Renderer::DrawImpostorInstances: needs one transform per instance");
    }
    if (count == 0 || !options.draw_in_view) {
        return;
    }
    InstanceBatch batch{};
    batch.mesh_id = impl_->impostor_card.id;
    batch.impostor_id = impostor.id;
    batch.first_source = impl_->instance_sources.size();
    batch.count = static_cast<std::uint32_t>(count);
    batch.options = options;
    batch.options.shadow_cascades = 0;
    impl_->instance_sources.insert(impl_->instance_sources.end(), transforms, transforms + count);
    impl_->instance_batches.push_back(batch);
}

TerrainHandle Renderer::CreateTerrain(const float* heights, int width, int depth, float spacing, const glm::vec3& origin) {
    if (heights == nullptr || width < 2 || depth < 2 || width > 16384 || depth > 16384 || spacing <= 0.0f) {
        FatalError("Renderer::CreateTerrain: needs between 2 and 16384 points each way and a spacing above zero");
    }
    if (impl_->in_frame) {
        FatalError("Renderer::CreateTerrain: do not create terrains between BeginFrame and EndFrame");
    }
    const size_t point_count = static_cast<size_t>(width) * static_cast<size_t>(depth);
    float lowest = heights[0];
    float highest = heights[0];
    for (size_t i = 1; i < point_count; ++i) {
        lowest = glm::min(lowest, heights[i]);
        highest = glm::max(highest, heights[i]);
    }
    TerrainData terrain;
    terrain.width = width;
    terrain.depth = depth;
    terrain.spacing = spacing;
    terrain.origin = {origin.x, origin.y + lowest, origin.z};
    terrain.height_range = glm::max(highest - lowest, 0.001f);

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = kHeightFormat;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(width);
    info.height = static_cast<Uint32>(depth);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    terrain.height_texture = SDL_CreateGPUTexture(impl_->device, &info);
    if (terrain.height_texture == nullptr) {
        FatalSdlError("cannot create terrain height texture");
    }

    std::vector<std::uint16_t> values(point_count);
    for (size_t i = 0; i < point_count; ++i) {
        values[i] = static_cast<std::uint16_t>((heights[i] - lowest) / terrain.height_range * 65535.0f + 0.5f);
    }
    SDL_GPUTransferBuffer* transfer =
        impl_->CreateFilledTransferBuffer(values.data(), static_cast<Uint32>(point_count * sizeof(std::uint16_t)));
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
    if (commands == nullptr) {
        FatalSdlError("cannot acquire command buffer");
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = static_cast<Uint32>(width);
    source.rows_per_layer = static_cast<Uint32>(depth);
    SDL_GPUTextureRegion destination{};
    destination.texture = terrain.height_texture;
    destination.w = static_cast<Uint32>(width);
    destination.h = static_cast<Uint32>(depth);
    destination.d = 1;
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        FatalSdlError("cannot submit terrain upload");
    }
    SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);

    // The grid mesh all patches share: a flat square from (0, 0) to (1, 1) with a rim of
    // "skirt" vertices (y = -1) that the vertex shader pushes down.
    if (impl_->terrain_grid.id == 0) {
        const int cells = kTerrainGridCells;
        const int row = cells + 1;
        std::vector<MeshVertex> vertices;
        std::vector<std::uint32_t> indices;
        for (int j = 0; j < row; ++j) {
            for (int i = 0; i < row; ++i) {
                const float x = static_cast<float>(i) / static_cast<float>(cells);
                const float z = static_cast<float>(j) / static_cast<float>(cells);
                vertices.push_back({{x, 0.0f, z}, {0.0f, 1.0f, 0.0f}, {x, z}});
            }
        }
        for (int j = 0; j < cells; ++j) {
            for (int i = 0; i < cells; ++i) {
                const std::uint32_t corner = static_cast<std::uint32_t>(j * row + i);
                const std::uint32_t next_row = corner + static_cast<std::uint32_t>(row);
                indices.insert(indices.end(), {corner, next_row, corner + 1, corner + 1, next_row, next_row + 1});
            }
        }
        // Skirts along the four edges, drawn from both sides so that no winding rule can hide them.
        const auto add_skirt = [&vertices, &indices](std::uint32_t a, std::uint32_t b) {
            const std::uint32_t low_a = static_cast<std::uint32_t>(vertices.size());
            MeshVertex vertex = vertices[a];
            vertex.position.y = -1.0f;
            vertices.push_back(vertex);
            vertex = vertices[b];
            vertex.position.y = -1.0f;
            vertices.push_back(vertex);
            const std::uint32_t low_b = low_a + 1;
            indices.insert(indices.end(), {a, b, low_a, low_a, b, low_b, a, low_a, b, b, low_a, low_b});
        };
        for (int i = 0; i < cells; ++i) {
            const std::uint32_t step = static_cast<std::uint32_t>(i);
            const std::uint32_t last = static_cast<std::uint32_t>(cells);
            const std::uint32_t width_row = static_cast<std::uint32_t>(row);
            add_skirt(step, step + 1);                                          // z = 0 edge
            add_skirt(last * width_row + step, last * width_row + step + 1);    // z = 1 edge
            add_skirt(step * width_row, (step + 1) * width_row);                // x = 0 edge
            add_skirt(step * width_row + last, (step + 1) * width_row + last);  // x = 1 edge
        }
        impl_->terrain_grid =
            CreateMesh(vertices.data(), static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
    }

    impl_->terrains.push_back(terrain);
    TerrainHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->terrains.size());
    return handle;
}

void Renderer::DestroyTerrain(TerrainHandle terrain) {
    if (impl_->in_frame) {
        FatalError("Renderer::DestroyTerrain: do not destroy terrains between BeginFrame and EndFrame");
    }
    if (terrain.id == 0 || terrain.id > impl_->terrains.size() || impl_->terrains[terrain.id - 1].height_texture == nullptr) {
        FatalError("Renderer::DestroyTerrain: invalid or already destroyed terrain handle");
    }
    SDL_ReleaseGPUTexture(impl_->device, impl_->terrains[terrain.id - 1].height_texture);
    impl_->terrains[terrain.id - 1].height_texture = nullptr;
}

void Renderer::DrawTerrain(TerrainHandle terrain, const TerrainMaterial& material, const TerrainPatch* patches, int count) {
    if (!impl_->in_frame) {
        FatalError("Renderer::DrawTerrain: call BeginFrame first");
    }
    if (terrain.id == 0 || terrain.id > impl_->terrains.size() || impl_->terrains[terrain.id - 1].height_texture == nullptr) {
        FatalError("Renderer::DrawTerrain: invalid or destroyed terrain handle");
    }
    if (count < 0 || (count > 0 && patches == nullptr)) {
        FatalError("Renderer::DrawTerrain: needs the patches to draw");
    }
    for (const TextureHandle texture : {material.colors, material.normals, material.weights[0], material.weights[1]}) {
        if (texture.id > impl_->textures.size() || (texture.id != 0 && impl_->textures[texture.id - 1] == nullptr)) {
            FatalError("Renderer::DrawTerrain: invalid or destroyed texture handle");
        }
    }
    for (const TextureHandle texture : {material.colors, material.normals}) {
        if (texture.id != 0 && impl_->texture_layers[texture.id - 1] < material.layer_count) {
            FatalError("Renderer::DrawTerrain: the layer textures must be texture arrays with at least layer_count images");
        }
    }
    for (const TextureHandle texture : {material.weights[0], material.weights[1]}) {
        if (texture.id != 0 && impl_->texture_layers[texture.id - 1] != 0) {
            FatalError("Renderer::DrawTerrain: the weights must be plain textures, not texture arrays");
        }
    }
    if (material.layer_count < 0 || material.layer_count > kMaxTerrainLayers) {
        FatalError("Renderer::DrawTerrain: layer_count must be between 0 and 8");
    }
    if (count == 0) {
        return;
    }
    InstanceBatch batch{};
    batch.mesh_id = impl_->terrain_grid.id;
    batch.terrain_id = terrain.id;
    batch.terrain_material = material;
    batch.first_source = impl_->instance_sources.size();
    batch.count = static_cast<std::uint32_t>(count);
    batch.options = InstanceOptions{};
    for (int i = 0; i < count; ++i) {
        // Corner, size and height range of the patch, packed into a matrix like any instance.
        glm::mat4 transform(1.0f);
        transform[0].x = patches[i].size;
        transform[1].y = glm::max(patches[i].max_height - patches[i].min_height, 0.01f);
        transform[2].z = patches[i].size;
        transform[3] = glm::vec4(patches[i].x, patches[i].min_height, patches[i].z, 1.0f);
        impl_->instance_sources.push_back(transform);
    }
    impl_->instance_batches.push_back(batch);
}

void Renderer::DrawSkinnedMesh(MeshHandle mesh_handle, const glm::mat4& transform, const Material& material,
                               const glm::mat4* joint_matrices, int joint_count) {
    if (!impl_->in_frame) {
        FatalError("Renderer::DrawSkinnedMesh: call BeginFrame first");
    }
    if (mesh_handle.id == 0 || mesh_handle.id > impl_->meshes.size() ||
        impl_->meshes[mesh_handle.id - 1].skin_buffer == nullptr) {
        FatalError("Renderer::DrawSkinnedMesh: not a mesh made by CreateSkinnedMesh");
    }
    if (joint_matrices == nullptr || joint_count < 1 || joint_count > kMaxJoints) {
        FatalError("Renderer::DrawSkinnedMesh: needs between 1 and 256 joint matrices");
    }
    for (const TextureHandle texture : {material.texture, material.normal_texture, material.orm_texture}) {
        if (texture.id > impl_->textures.size() || (texture.id != 0 && impl_->textures[texture.id - 1] == nullptr)) {
            FatalError("Renderer::DrawSkinnedMesh: invalid or destroyed texture handle");
        }
    }
    const Mesh& mesh = impl_->meshes[mesh_handle.id - 1];

    DrawCommand command;
    command.mesh_id = mesh_handle.id;
    command.transform = transform;
    command.material = material;
    command.skinned = true;
    command.first_joint = static_cast<std::uint32_t>(impl_->joint_palette.size());
    impl_->joint_palette.insert(impl_->joint_palette.end(), joint_matrices, joint_matrices + joint_count);

    // A posed vertex is a weighted mix of where each joint would put it, so the posed mesh
    // lies inside the box around the mesh's box as moved by every joint.
    command.bounds_min = glm::vec3(1.0e30f);
    command.bounds_max = glm::vec3(-1.0e30f);
    for (int joint = 0; joint < joint_count; ++joint) {
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec4 point((corner & 1) ? mesh.bounds_max.x : mesh.bounds_min.x,
                                  (corner & 2) ? mesh.bounds_max.y : mesh.bounds_min.y,
                                  (corner & 4) ? mesh.bounds_max.z : mesh.bounds_min.z, 1.0f);
            const glm::vec3 moved = glm::vec3(joint_matrices[joint] * point);
            command.bounds_min = glm::min(command.bounds_min, moved);
            command.bounds_max = glm::max(command.bounds_max, moved);
        }
    }
    impl_->draw_commands.push_back(command);
}

void Renderer::PrepareClouds() {
    if (impl_->in_frame) {
        FatalError("Renderer::PrepareClouds: call it outside BeginFrame and EndFrame");
    }
    impl_->CreateCloudTextures();
}

void Renderer::SetSkyOcclusion(const SkyOcclusionMap* map) {
    impl_->sky_occlusion_map = map;
    impl_->sky_occlusion_version = 0xFFFFFFFFu;
}

void Renderer::SetTrampleMap(const TrampleMap* map) {
    impl_->trample_map = map;
    impl_->trample_version = 0xFFFFFFFFu;
}

void Renderer::DrawWater(MeshHandle mesh, const glm::mat4& transform, const WaterMaterial& material) {
    if (!impl_->in_frame) {
        FatalError("Renderer::DrawWater: call BeginFrame first");
    }
    if (mesh.id == 0 || mesh.id > impl_->meshes.size() || impl_->meshes[mesh.id - 1].vertex_buffer == nullptr ||
        impl_->meshes[mesh.id - 1].skin_buffer != nullptr) {
        FatalError("Renderer::DrawWater: invalid, destroyed or skinned mesh handle");
    }
    WaterCommand command;
    command.mesh_id = mesh.id;
    command.transform = transform;
    command.material = material;
    impl_->water_commands.push_back(command);
}

void Renderer::DrawMesh(MeshHandle mesh, const glm::mat4& transform, Color color) {
    Material material;
    material.color = color;
    DrawMesh(mesh, transform, material);
}

void Renderer::EndFrame() {
    if (!impl_->in_frame) {
        FatalError("Renderer::EndFrame: call BeginFrame first");
    }
    Impl& impl = *impl_;
    const Camera& camera = impl.camera;
    const DirectionalLight& light = impl.light;
    const Environment& environment = impl.environment;
    const auto cpu_start = std::chrono::steady_clock::now();

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl.device);
    if (commands == nullptr) {
        FatalSdlError("cannot acquire command buffer");
    }

    const float aspect_ratio = static_cast<float>(impl.config.width) / static_cast<float>(impl.config.height);
    const glm::mat4 view = glm::lookAt(camera.position, camera.target, camera.up);
    // Near and far swapped: depth 1 at the near plane, 0 at the far one (reversed depth, see
    // CreateTargets). Everything that reads depth (common.hlsli, ao, fog) follows this.
    const glm::mat4 unshifted_projection = glm::perspective(glm::radians(camera.vertical_fov_degrees), aspect_ratio,
                                                            camera.far_plane, camera.near_plane);
    const glm::mat4 unshifted_view_projection = unshifted_projection * view;
    // Temporal anti-aliasing: shift the whole picture by a fraction of a pixel, a different
    // one every frame (see TaaUniforms). Everything that renders the scene uses the shifted
    // camera; reprojection uses the unshifted one.
    const bool taa_on = impl.post.temporal_aa;
    if (taa_on && impl.has_taa_history && glm::distance(camera.position, impl.previous_camera_position) > kTaaCutDistance) {
        impl.has_taa_history = false;
    }
    if (!taa_on) {
        impl.has_taa_history = false;
    }
    glm::mat4 projection = unshifted_projection;
    // The shift in drawn pixels, as TAA needs it: x to the right, y down the image.
    glm::vec2 jitter_pixels(0.0f);
    if (taa_on) {
        const int step = static_cast<int>(impl.frame_index % kTaaSequenceLength);
        const float shift_x = Halton(step, 2) - 0.5f;
        const float shift_y = Halton(step, 3) - 0.5f;
        projection[2][0] += shift_x * 2.0f / static_cast<float>(impl.render_width);
        projection[2][1] += shift_y * 2.0f / static_cast<float>(impl.render_height);
        // projection[2][x] multiplies view depth z, and clip w is -z: the picture moves by
        // minus the added amount in normalized coordinates, i.e. -shift_x pixels to the right
        // and +shift_y pixels down.
        jitter_pixels = glm::vec2(-shift_x, shift_y);
        impl.temporal_noise_offset = static_cast<float>(impl.frame_index % 16) * 5.588238f;
    } else {
        impl.temporal_noise_offset = 0.0f;
    }
    const glm::mat4 view_projection = projection * view;
    const glm::vec3 light_direction = glm::normalize(light.direction);
    const bool sun_is_on = light.color.r > 0.0f || light.color.g > 0.0f || light.color.b > 0.0f;
    const bool shadows_on =
        environment.shadows && sun_is_on && (!impl.draw_commands.empty() || !impl.instance_batches.empty());

    // --- Culling: leave out meshes that lie entirely outside the camera's view. ---
    impl.visible_commands.clear();
    for (std::uint32_t i = 0; i < impl.draw_commands.size(); ++i) {
        const DrawCommand& command = impl.draw_commands[i];
        if (!IsBoxOutside(view_projection * command.transform, command.bounds_min, command.bounds_max, true)) {
            impl.visible_commands.push_back(i);
        }
    }
    impl.stats = FrameStats{};
    {
        // Instances are culled against the view and against every cascade before any pass
        // starts, because their transforms must be on the graphics card by then.
        Impl::Cascades instance_cascades{};
        if (shadows_on) {
            instance_cascades = impl.ComputeCascades();
        }
        impl.CullInstances(view_projection, shadows_on ? &instance_cascades : nullptr);
        impl.UploadMatrices(commands, impl.instance_data, impl.instance_buffer, impl.instance_buffer_capacity,
                            impl.instance_transfer, impl.instance_transfer_capacity);
    }
    impl.UploadJointMatrices(commands);
    impl.stats.meshes_submitted = static_cast<int>(impl.draw_commands.size());
    impl.stats.meshes_drawn = static_cast<int>(impl.visible_commands.size());

    // --- Pass 1: shadows. The scene's depth as seen from the sun, once per cascade. ---
    Impl::Cascades cascades{};
    if (shadows_on) {
        cascades = impl.ComputeCascades();
        // A mesh outside the camera's view can still cast a shadow into it, so shadows are
        // culled separately, against each cascade's own volume.
        for (int cascade = 0; cascade < kCascadeCount; ++cascade) {
            impl.cascade_commands[cascade].clear();
            for (std::uint32_t i = 0; i < impl.draw_commands.size(); ++i) {
                const DrawCommand& command = impl.draw_commands[i];
                if (!IsBoxOutside(cascades.light_view_projection[cascade] * command.transform, command.bounds_min,
                                  command.bounds_max, false)) {
                    impl.cascade_commands[cascade].push_back(i);
                }
            }
            impl.stats.shadow_draws += static_cast<int>(impl.cascade_commands[cascade].size());
        }

        SDL_GPUDepthStencilTargetInfo depth_info{};
        depth_info.texture = impl.shadow_atlas;
        depth_info.clear_depth = 1.0f;
        depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
        depth_info.store_op = SDL_GPU_STOREOP_STORE;
        depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &depth_info);
        if (pass == nullptr) {
            FatalSdlError("cannot begin shadow pass");
        }
        SDL_GPUGraphicsPipeline* bound_pipeline = nullptr;
        const float tile_size = static_cast<float>(impl.config.shadow_map_size);
        for (int cascade = 0; cascade < kCascadeCount; ++cascade) {
            SDL_GPUViewport viewport{};
            viewport.x = static_cast<float>(cascade % 2) * tile_size;
            viewport.y = static_cast<float>(cascade / 2) * tile_size;
            viewport.w = tile_size * cascades.tile_scale[cascade];
            viewport.h = tile_size * cascades.tile_scale[cascade];
            viewport.min_depth = 0.0f;
            viewport.max_depth = 1.0f;
            SDL_SetGPUViewport(pass, &viewport);

            VertexFrameUniforms frame_uniforms;
            frame_uniforms.view_projection = cascades.light_view_projection[cascade];
            frame_uniforms.wind = impl.WindUniform();
            frame_uniforms.trample = impl.trample_info;
            SDL_PushGPUVertexUniformData(commands, 0, &frame_uniforms, sizeof(frame_uniforms));

            for (const std::uint32_t command_index : impl.cascade_commands[cascade]) {
                const DrawCommand& command = impl.draw_commands[command_index];
                impl.BindMeshPipeline(pass, kShadowPass, command, bound_pipeline);
                const ObjectUniforms object_uniforms = Impl::MakeObjectUniforms(command);
                SDL_PushGPUVertexUniformData(commands, 1, &object_uniforms, sizeof(object_uniforms));
                const MaterialUniforms material_uniforms =
                    Impl::MakeMaterialUniforms(command.material, impl.temporal_noise_offset);
                SDL_PushGPUFragmentUniformData(commands, 0, &material_uniforms, sizeof(material_uniforms));

                SDL_GPUTextureSamplerBinding binding{};
                binding.texture = impl.GetTexture(command.material.texture, impl.white_texture);
                binding.sampler = impl.texture_sampler;
                SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
                impl.DrawCommandGeometry(pass, command);
            }
            impl.DrawInstanceBatches(commands, pass, kShadowPass, 1 + cascade, false, bound_pipeline);
        }
        SDL_EndGPURenderPass(pass);
    }

    // The size the scene is drawn at; from temporal anti-aliasing on, the output size.
    const float width = static_cast<float>(impl.render_width);
    const float height = static_cast<float>(impl.render_height);
    const float output_width = static_cast<float>(impl.config.width);
    const float output_height = static_cast<float>(impl.config.height);
    // What shaders need to turn a depth image value back into a position (see common.hlsli).
    const glm::vec4 projection_info(projection[2][2], projection[3][2], 1.0f / projection[0][0], 1.0f / projection[1][1]);
    const bool ao_on = environment.ambient_occlusion && !impl.draw_commands.empty();
    const bool fog_on = environment.volumetric_fog && environment.fog_density > 0.0f;

    // --- Pass 1b: cloud shadows. For the land around the camera, the share of sunlight the
    // clouds let through; every lit surface (and the fog) reads it. ---
    const bool clouds_on = environment.clouds && environment.draw_sky && impl.cloud_shape != nullptr;
    CloudUniforms cloud_uniforms{};
    const float cloud_shadow_size = 8000.0f;
    const glm::vec2 cloud_shadow_corner =
        glm::floor((glm::vec2(camera.position.x, camera.position.z) - cloud_shadow_size * 0.5f) / 64.0f) * 64.0f;
    if (clouds_on) {
        const glm::vec2 wind = glm::length(environment.wind_direction) > 0.0f ? glm::normalize(environment.wind_direction)
                                                                             : glm::vec2(1.0f, 0.0f);
        const glm::vec2 drift = -wind * environment.cloud_speed * environment.wind_time;
        cloud_uniforms.inverse_view_projection = glm::inverse(view_projection);
        cloud_uniforms.camera_position = glm::vec4(camera.position, impl.temporal_noise_offset * 0.6180339f);
        cloud_uniforms.light_direction = glm::vec4(light_direction, 0.0f);
        cloud_uniforms.light_color = ToVec4(light.color);
        cloud_uniforms.sky_zenith = ToVec4(environment.sky_zenith) * environment.sky_light_intensity;
        cloud_uniforms.sky_horizon = ToVec4(environment.sky_horizon) * environment.sky_light_intensity;
        cloud_uniforms.layer = glm::vec4(environment.cloud_bottom, glm::max(environment.cloud_top, environment.cloud_bottom + 10.0f),
                                         glm::clamp(environment.cloud_coverage, 0.0f, 1.0f), environment.cloud_density);
        cloud_uniforms.shape = glm::vec4(environment.cloud_size, environment.cloud_size * 2.0f, drift.x, drift.y);
        cloud_uniforms.quality = glm::vec4(static_cast<float>(glm::clamp(environment.cloud_steps, 4, 256)),
                                           static_cast<float>(glm::clamp(environment.cloud_light_steps, 1, 16)), 40000.0f, 0.0f);
        cloud_uniforms.shadow_area = glm::vec4(cloud_shadow_corner, cloud_shadow_size, 0.0f);
        if (environment.cloud_shadows) {
            SDL_GPUTextureSamplerBinding bindings[3]{};
            bindings[0].texture = impl.cloud_shape;
            bindings[0].sampler = impl.repeat_sampler;
            bindings[1].texture = impl.cloud_detail;
            bindings[1].sampler = impl.repeat_sampler;
            bindings[2].texture = impl.cloud_weather;
            bindings[2].sampler = impl.repeat_sampler;
            impl.DrawFullscreenPass(commands, impl.cloud_shadow_target, false, impl.cloud_shadow_pipeline, bindings, 3, &cloud_uniforms,
                                    sizeof(cloud_uniforms));
        }
    }
    impl.frame_cloud_shadow = clouds_on && environment.cloud_shadows ? impl.cloud_shadow_target : impl.textures[impl.white_texture.id - 1];
    impl.frame_sky_occlusion = impl.sky_occlusion_map != nullptr && impl.sky_occlusion_texture != nullptr
                                   ? impl.sky_occlusion_texture
                                   : impl.textures[impl.white_texture.id - 1];

    // --- Pass 2: depth of the scene, for ambient occlusion, fog and temporal anti-aliasing,
    // and without multisampling also for the scene pass itself: it then tests this depth
    // before shading, so each pixel is lit once instead of once per leaf behind it. ---
    const bool scene_after_depth = impl.msaa_samples == 1;
    const bool water_on = !impl.water_commands.empty();
    if (ao_on || fog_on || taa_on || scene_after_depth || water_on || (environment.clouds && environment.draw_sky)) {
        SDL_GPUDepthStencilTargetInfo depth_info{};
        depth_info.texture = impl.prepass_depth;
        depth_info.clear_depth = 0.0f;  // reversed depth
        depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
        depth_info.store_op = SDL_GPU_STOREOP_STORE;
        depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &depth_info);
        if (pass == nullptr) {
            FatalSdlError("cannot begin depth pass");
        }
        VertexFrameUniforms frame_uniforms;
        frame_uniforms.view_projection = view_projection;
        frame_uniforms.wind = impl.WindUniform();
        frame_uniforms.trample = impl.trample_info;
        SDL_PushGPUVertexUniformData(commands, 0, &frame_uniforms, sizeof(frame_uniforms));
        // With ambient occlusion on, what does not cast it is left out here and added after it.
        impl.DrawDepthPass(commands, pass, ao_on ? Impl::kAoCasters : Impl::kAllBatches);
        SDL_EndGPURenderPass(pass);
    }

    // --- Pass 3: ambient occlusion from that depth, then a blur across and a blur down. ---
    if (ao_on) {
        AoUniforms ao_uniforms;
        ao_uniforms.projection_info = projection_info;
        ao_uniforms.screen =
            glm::vec4(width, height, static_cast<float>(glm::clamp(environment.ambient_occlusion_samples, 4, 64)), 0.0f);
        ao_uniforms.ao = glm::vec4(environment.ambient_occlusion_radius, environment.ambient_occlusion_strength,
                                   0.5f * height * projection[1][1], impl.temporal_noise_offset);
        SDL_GPUTextureSamplerBinding bindings[2]{};
        bindings[0].texture = impl.prepass_depth;
        bindings[0].sampler = impl.clamp_sampler;
        impl.DrawFullscreenPass(commands, impl.ao_target, false, impl.ao_pipeline, bindings, 1, &ao_uniforms,
                                sizeof(ao_uniforms));

        AoBlurUniforms blur_uniforms;
        blur_uniforms.projection_info = projection_info;
        bindings[1].texture = impl.prepass_depth;
        bindings[1].sampler = impl.clamp_sampler;
        bindings[0].texture = impl.ao_target;
        blur_uniforms.direction = glm::vec4(1.0f, 0.0f, width, height);
        impl.DrawFullscreenPass(commands, impl.ao_scratch, false, impl.ao_blur_pipeline, bindings, 2, &blur_uniforms,
                                sizeof(blur_uniforms));
        bindings[0].texture = impl.ao_scratch;
        blur_uniforms.direction = glm::vec4(0.0f, 1.0f, width, height);
        impl.DrawFullscreenPass(commands, impl.ao_target, false, impl.ao_blur_pipeline, bindings, 2, &blur_uniforms,
                                sizeof(blur_uniforms));

        // What casts no ambient occlusion (grass) joins the depth image now, for the scene
        // pass, water, fog and anti-aliasing.
        if (impl.HasAoNonCasters()) {
            SDL_GPUDepthStencilTargetInfo depth_info{};
            depth_info.texture = impl.prepass_depth;
            depth_info.load_op = SDL_GPU_LOADOP_LOAD;
            depth_info.store_op = SDL_GPU_STOREOP_STORE;
            depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, nullptr, 0, &depth_info);
            if (pass == nullptr) {
                FatalSdlError("cannot begin depth pass");
            }
            VertexFrameUniforms vertex_uniforms;
            vertex_uniforms.view_projection = view_projection;
            vertex_uniforms.wind = impl.WindUniform();
            vertex_uniforms.trample = impl.trample_info;
            SDL_PushGPUVertexUniformData(commands, 0, &vertex_uniforms, sizeof(vertex_uniforms));
            impl.DrawDepthPass(commands, pass, Impl::kAoNonCasters);
            SDL_EndGPURenderPass(pass);
        }
    }

    // --- Pass 4: the scene, in linear wide-range colors. ---
    FragmentFrameUniforms frame_uniforms{};
    {
        SDL_GPUColorTargetInfo color_info{};
        color_info.texture = impl.scene_color;
        color_info.clear_color = impl.linear_clear_color;
        color_info.load_op = SDL_GPU_LOADOP_CLEAR;
        if (impl.msaa_samples > 1) {
            // Average the samples of every pixel into the single-sample image.
            color_info.store_op = SDL_GPU_STOREOP_RESOLVE;
            color_info.resolve_texture = impl.scene_resolved;
        } else {
            color_info.store_op = SDL_GPU_STOREOP_STORE;
        }

        SDL_GPUDepthStencilTargetInfo depth_info{};
        if (scene_after_depth) {
            // The depth pass's image, kept as it is: the scene pass only tests against it.
            depth_info.texture = impl.prepass_depth;
            depth_info.load_op = SDL_GPU_LOADOP_LOAD;
            depth_info.store_op = SDL_GPU_STOREOP_STORE;
        } else {
            depth_info.texture = impl.scene_depth;
            depth_info.clear_depth = 0.0f;  // reversed depth
            depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
            depth_info.store_op = SDL_GPU_STOREOP_DONT_CARE;
        }
        depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color_info, 1, &depth_info);
        if (pass == nullptr) {
            FatalSdlError("cannot begin scene pass");
        }

        if (environment.draw_sky) {
            SkyUniforms sky_uniforms;
            sky_uniforms.inverse_view_projection = glm::inverse(view_projection);
            sky_uniforms.camera_position = glm::vec4(camera.position, 1.0f);
            sky_uniforms.light_direction = glm::vec4(light_direction, 0.0f);
            sky_uniforms.sun_color = ToVec4(light.color);
            sky_uniforms.sky_zenith = ToVec4(environment.sky_zenith);
            sky_uniforms.sky_horizon = ToVec4(environment.sky_horizon);
            sky_uniforms.sky_ground = ToVec4(environment.sky_ground);
            SDL_BindGPUGraphicsPipeline(pass, impl.sky_pipeline);
            SDL_PushGPUFragmentUniformData(commands, 0, &sky_uniforms, sizeof(sky_uniforms));
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        }

        VertexFrameUniforms vertex_frame_uniforms;
        vertex_frame_uniforms.view_projection = view_projection;
        vertex_frame_uniforms.wind = impl.WindUniform();
        vertex_frame_uniforms.trample = impl.trample_info;
        SDL_PushGPUVertexUniformData(commands, 0, &vertex_frame_uniforms, sizeof(vertex_frame_uniforms));

        for (int i = 0; i < kCascadeCount; ++i) {
            frame_uniforms.shadow_matrices[i] = cascades.shadow_matrix[i];
        }
        frame_uniforms.cascade_far = cascades.far_distance;
        frame_uniforms.cascade_texel = cascades.texel_size;
        frame_uniforms.cascade_scale = cascades.tile_scale;
        frame_uniforms.cloud_shadow_area = glm::vec4(cloud_shadow_corner, 1.0f / cloud_shadow_size,
                                                     clouds_on && environment.cloud_shadows ? 1.0f : 0.0f);
        if (impl.sky_occlusion_map != nullptr && impl.sky_occlusion_texture != nullptr) {
            const SkyOcclusionMap& map = *impl.sky_occlusion_map;
            const float extent = static_cast<float>(map.resolution - 1) * map.spacing;
            // Texel centers lie on the map's points: shift by half a texel.
            const float texel = map.spacing;
            frame_uniforms.sky_occlusion_area = glm::vec4(map.origin - glm::vec2(texel * 0.5f), 1.0f / (extent + texel), 1.0f);
        }
        frame_uniforms.aerial = glm::vec4(environment.sky_horizon.r * environment.sky_light_intensity,
                                          environment.sky_horizon.g * environment.sky_light_intensity,
                                          environment.sky_horizon.b * environment.sky_light_intensity, environment.aerial_density);
        frame_uniforms.foliage = glm::vec4(environment.foliage_translucency, 0.0f, 0.0f, 0.0f);
        frame_uniforms.camera_position = glm::vec4(camera.position, 1.0f);
        frame_uniforms.camera_forward = glm::vec4(glm::normalize(camera.target - camera.position), 0.0f);
        frame_uniforms.light_direction = glm::vec4(light_direction, 0.0f);
        frame_uniforms.light_color = ToVec4(light.color);
        if (environment.sky_light) {
            // Upward-facing surfaces see mostly sky between the zenith and the horizon;
            // downward-facing ones see the ground.
            const glm::vec4 sky_average = (ToVec4(environment.sky_zenith) + ToVec4(environment.sky_horizon)) * 0.5f;
            frame_uniforms.ambient_sky = sky_average * environment.sky_light_intensity;
            frame_uniforms.ambient_ground = ToVec4(environment.sky_ground) * environment.sky_light_intensity;
        } else {
            frame_uniforms.ambient_sky = ToVec4(light.ambient);
            frame_uniforms.ambient_ground = ToVec4(light.ambient);
        }
        if (clouds_on) {
            // Under clouds the sky's light is greyer and softer: its color moves towards its own
            // brightness, by up to half at full coverage.
            const float cover = glm::clamp(environment.cloud_coverage, 0.0f, 1.0f) * 0.5f;
            const auto greyer = [cover](glm::vec4 color) {
                const float brightness = color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
                return glm::mix(color, glm::vec4(brightness * 1.1f, brightness * 1.1f, brightness * 1.1f, color.w), cover);
            };
            frame_uniforms.ambient_sky = greyer(frame_uniforms.ambient_sky);
        }
        frame_uniforms.sky_zenith = ToVec4(environment.sky_zenith) * environment.sky_light_intensity;
        frame_uniforms.sky_horizon = ToVec4(environment.sky_horizon) * environment.sky_light_intensity;
        frame_uniforms.sky_ground = ToVec4(environment.sky_ground) * environment.sky_light_intensity;
        // Volumetric fog is added after the scene pass; the simple fog is then left out here.
        frame_uniforms.fog = glm::vec4(environment.fog_color.r, environment.fog_color.g, environment.fog_color.b,
                                       fog_on ? 0.0f : environment.fog_density);
        frame_uniforms.screen = glm::vec4(ao_on ? 1.0f : 0.0f, environment.show_shadow_cascades && shadows_on ? 1.0f : 0.0f,
                                          0.0f, 0.0f);
        frame_uniforms.options = glm::vec4(shadows_on ? 1.0f : 0.0f, impl.msaa_samples > 1 ? 1.0f : 0.0f,
                                           environment.sky_light ? 1.0f : 0.0f,
                                           1.0f / static_cast<float>(impl.config.shadow_map_size * 2));
        SDL_PushGPUFragmentUniformData(commands, 0, &frame_uniforms, sizeof(frame_uniforms));

        SDL_GPUGraphicsPipeline* bound_pipeline = nullptr;
        for (const std::uint32_t command_index : impl.visible_commands) {
            const DrawCommand& command = impl.draw_commands[command_index];
            const Material& material = command.material;
            impl.BindMeshPipeline(pass, scene_after_depth ? kSceneAfterDepthPass : kScenePass, command, bound_pipeline);

            const ObjectUniforms object_uniforms = Impl::MakeObjectUniforms(command);
            SDL_PushGPUVertexUniformData(commands, 1, &object_uniforms, sizeof(object_uniforms));
            const MaterialUniforms material_uniforms = Impl::MakeMaterialUniforms(material, impl.temporal_noise_offset);
            SDL_PushGPUFragmentUniformData(commands, 1, &material_uniforms, sizeof(material_uniforms));

            SDL_GPUTextureSamplerBinding bindings[7]{};
            bindings[0].texture = impl.GetTexture(material.texture, impl.white_texture);
            bindings[0].sampler = impl.texture_sampler;
            bindings[1].texture = impl.GetTexture(material.normal_texture, impl.flat_normal_texture);
            bindings[1].sampler = impl.texture_sampler;
            bindings[2].texture = impl.GetTexture(material.orm_texture, impl.white_texture);
            bindings[2].sampler = impl.texture_sampler;
            bindings[3].texture = impl.shadow_atlas;
            bindings[3].sampler = impl.shadow_sampler;
            bindings[4].texture = ao_on ? impl.ao_target : impl.textures[impl.white_texture.id - 1];
            bindings[4].sampler = impl.clamp_sampler;
            impl.AddLightingBindings(&bindings[5]);
            SDL_BindGPUFragmentSamplers(pass, 0, bindings, 7);
            impl.DrawCommandGeometry(pass, command);
        }
        impl.DrawInstanceBatches(commands, pass, scene_after_depth ? kSceneAfterDepthPass : kScenePass, 0, ao_on,
                                 bound_pipeline);
        SDL_EndGPURenderPass(pass);
    }

    // The finished scene with one sample per pixel; the remaining passes work on it.
    SDL_GPUTexture* scene_image = impl.msaa_samples > 1 ? impl.scene_resolved : impl.scene_color;  // replaced by TAA's result
    SDL_GPUTexture* white = impl.textures[impl.white_texture.id - 1];

    // --- Pass 4b: water. It reads the scene and its depth as they are without it (copies),
    // and writes its own depth, so that fog and anti-aliasing treat it as a surface. ---
    if (water_on) {
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTextureLocation source{};
        source.texture = scene_image;
        SDL_GPUTextureLocation destination{};
        destination.texture = impl.water_scene_copy;
        SDL_CopyGPUTextureToTexture(copy_pass, &source, &destination, static_cast<Uint32>(impl.render_width),
                                    static_cast<Uint32>(impl.render_height), 1, false);
        SDL_EndGPUCopyPass(copy_pass);
        SDL_GPUTextureSamplerBinding depth_binding{};
        depth_binding.texture = impl.prepass_depth;
        depth_binding.sampler = impl.clamp_sampler;
        const glm::vec4 no_uniforms(0.0f);
        impl.DrawFullscreenPass(commands, impl.water_depth_copy, false, impl.depth_copy_pipeline, &depth_binding, 1,
                                &no_uniforms, sizeof(no_uniforms));

        SDL_GPUColorTargetInfo color_info{};
        color_info.texture = scene_image;
        color_info.load_op = SDL_GPU_LOADOP_LOAD;
        color_info.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depth_info{};
        depth_info.texture = impl.prepass_depth;
        depth_info.load_op = SDL_GPU_LOADOP_LOAD;
        depth_info.store_op = SDL_GPU_STOREOP_STORE;
        depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color_info, 1, &depth_info);
        if (pass == nullptr) {
            FatalSdlError("cannot begin water pass");
        }
        SDL_BindGPUGraphicsPipeline(pass, impl.water_pipeline);
        VertexFrameUniforms vertex_frame_uniforms;
        vertex_frame_uniforms.view_projection = view_projection;
        vertex_frame_uniforms.wind = impl.WindUniform();
        vertex_frame_uniforms.trample = impl.trample_info;
        SDL_PushGPUVertexUniformData(commands, 0, &vertex_frame_uniforms, sizeof(vertex_frame_uniforms));
        SDL_PushGPUFragmentUniformData(commands, 0, &frame_uniforms, sizeof(frame_uniforms));
        SDL_GPUTextureSamplerBinding bindings[6]{};
        bindings[0].texture = impl.water_scene_copy;
        bindings[0].sampler = impl.linear_clamp_sampler;
        bindings[1].texture = impl.water_depth_copy;
        bindings[1].sampler = impl.clamp_sampler;
        bindings[2].texture = impl.shadow_atlas;
        bindings[2].sampler = impl.shadow_sampler;
        bindings[3].texture = white;
        bindings[3].sampler = impl.clamp_sampler;
        impl.AddLightingBindings(&bindings[4]);
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, 6);
        const glm::mat4 inverse_view_projection = glm::inverse(view_projection);
        for (const WaterCommand& command : impl.water_commands) {
            const Mesh& mesh = impl.meshes[command.mesh_id - 1];
            if (IsBoxOutside(view_projection * command.transform, mesh.bounds_min, mesh.bounds_max, true)) {
                continue;
            }
            ObjectUniforms object_uniforms;
            object_uniforms.model = command.transform;
            object_uniforms.color = glm::vec4(1.0f);
            object_uniforms.uv_scale = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
            SDL_PushGPUVertexUniformData(commands, 1, &object_uniforms, sizeof(object_uniforms));
            const WaterMaterial& look = command.material;
            WaterUniforms water_uniforms;
            water_uniforms.view_projection = view_projection;
            water_uniforms.inverse_view_projection = inverse_view_projection;
            water_uniforms.absorption = glm::vec4(look.absorption, look.scattering);
            water_uniforms.water_color = glm::vec4(look.color.r, look.color.g, look.color.b, look.roughness);
            water_uniforms.waves = glm::vec4(look.wave_size, look.wave_strength, environment.wind_time, look.refraction);
            const glm::vec2 wind = glm::length(environment.wind_direction) > 0.0f ? glm::normalize(environment.wind_direction)
                                                                                 : glm::vec2(1.0f, 0.0f);
            water_uniforms.wind = glm::vec4(wind, environment.wind_strength, look.shore_fade);
            water_uniforms.screen = glm::vec4(width, height, static_cast<float>(glm::clamp(environment.water_reflection_steps, 0, 128)),
                                              environment.water_reflection_distance);
            SDL_PushGPUFragmentUniformData(commands, 1, &water_uniforms, sizeof(water_uniforms));
            SDL_GPUBufferBinding vertex_binding{};
            vertex_binding.buffer = mesh.vertex_buffer;
            SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
            SDL_GPUBufferBinding index_binding{};
            index_binding.buffer = mesh.index_buffer;
            SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count, 1, 0, 0, 0);
        }
        SDL_EndGPURenderPass(pass);
    }

    // --- Pass 4c: clouds, walked at a smaller size where the sky shows, then laid over it. ---
    if (clouds_on) {
        cloud_uniforms.quality.w = static_cast<float>(width);
        cloud_uniforms.shadow_area.w = static_cast<float>(height);
        SDL_GPUTextureSamplerBinding bindings[4]{};
        bindings[0].texture = impl.prepass_depth;
        bindings[0].sampler = impl.clamp_sampler;
        bindings[1].texture = impl.cloud_shape;
        bindings[1].sampler = impl.repeat_sampler;
        bindings[2].texture = impl.cloud_detail;
        bindings[2].sampler = impl.repeat_sampler;
        bindings[3].texture = impl.cloud_weather;
        bindings[3].sampler = impl.repeat_sampler;
        impl.DrawFullscreenPass(commands, impl.cloud_target, false, impl.clouds_pipeline, bindings, 4, &cloud_uniforms,
                                sizeof(cloud_uniforms));
        SDL_GPUTextureSamplerBinding composite[2]{};
        composite[0].texture = impl.cloud_target;
        composite[0].sampler = impl.linear_clamp_sampler;
        composite[1].texture = impl.prepass_depth;
        composite[1].sampler = impl.clamp_sampler;
        const glm::vec4 no_uniforms(0.0f);
        impl.DrawFullscreenPass(commands, scene_image, true, impl.clouds_composite_pipeline, composite, 2, &no_uniforms,
                                sizeof(no_uniforms));
    }

    // --- Pass 5: volumetric fog at half resolution, then laid over the scene. ---
    if (fog_on) {
        FogUniforms fog_uniforms{};
        fog_uniforms.inverse_view_projection = glm::inverse(view_projection);
        for (int i = 0; i < kCascadeCount; ++i) {
            fog_uniforms.shadow_matrices[i] = cascades.shadow_matrix[i];
        }
        fog_uniforms.cascade_far = cascades.far_distance;
        fog_uniforms.camera_position = glm::vec4(camera.position, 1.0f);
        // w: the frame's noise offset for the step pattern (see temporal_noise_offset).
        fog_uniforms.camera_forward =
            glm::vec4(glm::normalize(camera.target - camera.position), impl.temporal_noise_offset);
        fog_uniforms.light_direction = glm::vec4(light_direction, 0.0f);
        fog_uniforms.light_color = ToVec4(light.color);
        fog_uniforms.fog = glm::vec4(environment.fog_color.r, environment.fog_color.g, environment.fog_color.b,
                                     environment.fog_density);
        fog_uniforms.fog_shape = glm::vec4(environment.fog_height_falloff, environment.fog_base_height,
                                           environment.fog_sun_glow, environment.fog_shadow_brightness);
        fog_uniforms.options = glm::vec4(shadows_on ? 1.0f : 0.0f, glm::min(kFogWalkDistance, camera.far_plane), width,
                                         height);
        fog_uniforms.quality = glm::vec4(static_cast<float>(glm::clamp(environment.fog_steps, 4, 128)), 0.0f, 0.0f, 0.0f);
        fog_uniforms.cloud_shadow_area = glm::vec4(cloud_shadow_corner, 1.0f / cloud_shadow_size,
                                                   clouds_on && environment.cloud_shadows ? 1.0f : 0.0f);
        SDL_GPUTextureSamplerBinding bindings[3]{};
        bindings[0].texture = impl.prepass_depth;
        bindings[0].sampler = impl.clamp_sampler;
        bindings[1].texture = impl.shadow_atlas;
        bindings[1].sampler = impl.shadow_sampler;
        bindings[2].texture = impl.frame_cloud_shadow;
        bindings[2].sampler = impl.linear_clamp_sampler;
        impl.DrawFullscreenPass(commands, impl.fog_target, false, impl.fog_pipeline, bindings, 3, &fog_uniforms,
                                sizeof(fog_uniforms));

        FogCompositeUniforms composite_uniforms;
        composite_uniforms.projection_info = projection_info;
        composite_uniforms.sizes =
            glm::vec4(static_cast<float>(impl.fog_width), static_cast<float>(impl.fog_height), width, height);
        bindings[0].texture = impl.fog_target;
        bindings[0].sampler = impl.clamp_sampler;
        bindings[1].texture = impl.prepass_depth;
        bindings[1].sampler = impl.clamp_sampler;
        impl.DrawFullscreenPass(commands, scene_image, true, impl.fog_composite_pipeline, bindings, 2,
                                &composite_uniforms, sizeof(composite_uniforms));
    }

    // --- Pass 5b: temporal anti-aliasing. The new frame is blended into the previous
    // result; from here on the result stands for the scene. ---
    if (taa_on) {
        const int write = 1 - impl.taa_index;
        TaaUniforms taa_uniforms;
        taa_uniforms.inverse_view_projection = glm::inverse(view_projection);
        taa_uniforms.current_view_projection = unshifted_view_projection;
        taa_uniforms.previous_view_projection = impl.previous_view_projection;
        taa_uniforms.parameters = glm::vec4(glm::clamp(impl.post.temporal_aa_blend, 0.01f, 1.0f),
                                            impl.has_taa_history ? 1.0f : 0.0f, output_width, output_height);
        taa_uniforms.input = glm::vec4(width, height, jitter_pixels.x, jitter_pixels.y);
        SDL_GPUTextureSamplerBinding bindings[3]{};
        bindings[0].texture = scene_image;
        bindings[0].sampler = impl.clamp_sampler;
        bindings[1].texture = impl.taa_targets[impl.taa_index];
        bindings[1].sampler = impl.linear_clamp_sampler;
        bindings[2].texture = impl.prepass_depth;
        bindings[2].sampler = impl.clamp_sampler;
        impl.DrawFullscreenPass(commands, impl.taa_targets[write], false, impl.taa_pipeline, bindings, 3, &taa_uniforms,
                                sizeof(taa_uniforms));
        impl.taa_index = write;
        impl.has_taa_history = true;
        scene_image = impl.taa_targets[write];
    }
    impl.previous_view_projection = unshifted_view_projection;
    impl.previous_camera_position = camera.position;
    ++impl.frame_index;

    // --- Pass 6: bloom. A chain of ever smaller blurred copies of the scene, then mixed
    // back together from the smallest up. Automatic exposure reads the smallest copy. ---
    const bool bloom_on = impl.post.bloom && impl.post.bloom_strength > 0.0f;
    if (bloom_on || impl.post.auto_exposure) {
        SDL_GPUTexture* source = scene_image;
        float source_width = taa_on ? output_width : width;
        float source_height = taa_on ? output_height : height;
        for (size_t i = 0; i < impl.bloom_levels.size(); ++i) {
            const BloomLevel& level = impl.bloom_levels[i];
            ParameterUniforms uniforms;
            uniforms.parameters = glm::vec4(1.0f / source_width, 1.0f / source_height, i == 0 ? 1.0f : 0.0f, 0.0f);
            SDL_GPUTextureSamplerBinding binding{};
            binding.texture = source;
            binding.sampler = impl.linear_clamp_sampler;
            impl.DrawFullscreenPass(commands, level.texture, false, impl.bloom_down_pipeline, &binding, 1, &uniforms,
                                    sizeof(uniforms));
            source = level.texture;
            source_width = static_cast<float>(level.width);
            source_height = static_cast<float>(level.height);
        }
    }
    if (impl.post.auto_exposure) {
        const int previous = impl.luminance_index;
        const int current = 1 - previous;
        ParameterUniforms uniforms;
        uniforms.parameters = glm::vec4(
            impl.has_luminance_history ? glm::clamp(impl.post.auto_exposure_speed, 0.0f, 1.0f) : 1.0f, 0.0f, 0.0f, 0.0f);
        SDL_GPUTextureSamplerBinding bindings[2]{};
        bindings[0].texture = impl.bloom_levels.back().texture;
        bindings[0].sampler = impl.linear_clamp_sampler;
        bindings[1].texture = impl.luminance_targets[previous];
        bindings[1].sampler = impl.clamp_sampler;
        impl.DrawFullscreenPass(commands, impl.luminance_targets[current], false, impl.luminance_pipeline, bindings, 2,
                                &uniforms, sizeof(uniforms));
        impl.luminance_index = current;
        impl.has_luminance_history = true;
    }
    if (bloom_on) {
        for (size_t i = impl.bloom_levels.size() - 1; i > 0; --i) {
            const BloomLevel& smaller = impl.bloom_levels[i];
            ParameterUniforms uniforms;
            uniforms.parameters = glm::vec4(1.0f / static_cast<float>(smaller.width),
                                            1.0f / static_cast<float>(smaller.height), kBloomSpread, 0.0f);
            SDL_GPUTextureSamplerBinding binding{};
            binding.texture = smaller.texture;
            binding.sampler = impl.linear_clamp_sampler;
            impl.DrawFullscreenPass(commands, impl.bloom_levels[i - 1].texture, true, impl.bloom_up_pipeline, &binding, 1,
                                    &uniforms, sizeof(uniforms));
        }
    }

    // --- Pass 7: tone mapping into the output image. ---
    {
        // The debug view shows one stage's image instead of the finished frame; a stage that
        // is turned off shows as white.
        SDL_GPUTexture* debug_image = white;
        float debug_mode = 0.0f;
        switch (impl.post.debug_view) {
        case DebugView::None:
            break;
        case DebugView::AmbientOcclusion:
            debug_image = ao_on ? impl.ao_target : white;
            debug_mode = 1.0f;
            break;
        case DebugView::Fog:
            debug_image = fog_on ? impl.fog_target : white;
            debug_mode = 2.0f;
            break;
        case DebugView::Bloom:
            debug_image = bloom_on ? impl.bloom_levels[0].texture : white;
            debug_mode = 2.0f;
            break;
        }

        SDL_GPUTextureSamplerBinding bindings[4]{};
        bindings[0].texture = scene_image;
        bindings[0].sampler = impl.linear_clamp_sampler;
        bindings[1].texture = bloom_on ? impl.bloom_levels[0].texture : white;
        bindings[1].sampler = impl.linear_clamp_sampler;
        bindings[2].texture = impl.post.auto_exposure ? impl.luminance_targets[impl.luminance_index] : white;
        bindings[2].sampler = impl.clamp_sampler;
        bindings[3].texture = debug_image;
        bindings[3].sampler = impl.linear_clamp_sampler;
        TonemapUniforms uniforms;
        uniforms.parameters = glm::vec4(impl.post.exposure, impl.post.tone_mapping == ToneMapping::Aces ? 1.0f : 0.0f,
                                        bloom_on ? impl.post.bloom_strength : 0.0f,
                                        impl.post.auto_exposure ? 1.0f : 0.0f);
        uniforms.auto_exposure = glm::vec4(impl.post.auto_exposure_target, impl.post.auto_exposure_min,
                                           impl.post.auto_exposure_max, debug_mode);
        // White balance: warm raises red and lowers blue, tint trades green for magenta;
        // divided by its own brightness, so that it changes color and not exposure.
        const float temperature = glm::clamp(impl.post.temperature, -1.0f, 1.0f);
        const float tint = glm::clamp(impl.post.tint, -1.0f, 1.0f);
        glm::vec3 balance(1.0f + 0.2f * temperature, 1.0f - 0.15f * tint, 1.0f - 0.2f * temperature);
        balance /= glm::dot(balance, glm::vec3(0.2126f, 0.7152f, 0.0722f));
        uniforms.white_balance = glm::vec4(balance, 0.0f);
        uniforms.grading = glm::vec4(glm::max(impl.post.saturation, 0.0f), glm::max(impl.post.contrast, 0.0f),
                                     glm::clamp(impl.post.vignette, 0.0f, 1.0f), glm::clamp(impl.post.sharpen, 0.0f, 2.0f));
        // 1: the scene image has the output's size (read pixel by pixel); 0: it is smaller
        // (drawn at render_scale without TAA) and is stretched.
        const bool same_size = taa_on || (impl.render_width == impl.config.width && impl.render_height == impl.config.height);
        uniforms.input = glm::vec4(same_size ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
        impl.DrawFullscreenPass(commands, impl.output_target, false, impl.tonemap_pipeline, bindings, 4, &uniforms,
                                sizeof(uniforms));
    }

    // --- Pass 8: debug windows, drawn over the finished picture. ---
    if (impl.debug_ui_active) {
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, commands);
        SDL_GPUColorTargetInfo color_info{};
        color_info.texture = impl.output_target;
        color_info.load_op = SDL_GPU_LOADOP_LOAD;
        color_info.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color_info, 1, nullptr);
        if (pass == nullptr) {
            FatalSdlError("cannot begin debug window pass");
        }
        ImGui_ImplSDLGPU3_RenderDrawData(draw_data, commands, pass);
        SDL_EndGPURenderPass(pass);
    }

    if (impl.window != nullptr) {
        // The swapchain texture is the image the window shows. It can be unavailable
        // (for example while the window is minimized); the frame is then simply not shown.
        SDL_GPUTexture* swapchain_texture = nullptr;
        Uint32 swapchain_width = 0;
        Uint32 swapchain_height = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, impl.window, &swapchain_texture, &swapchain_width,
                                                   &swapchain_height)) {
            FatalSdlError("cannot acquire window image");
        }
        if (swapchain_texture != nullptr) {
            SDL_GPUBlitInfo blit{};
            blit.source.texture = impl.output_target;
            blit.source.w = static_cast<Uint32>(impl.config.width);
            blit.source.h = static_cast<Uint32>(impl.config.height);
            blit.destination.texture = swapchain_texture;
            blit.destination.w = swapchain_width;
            blit.destination.h = swapchain_height;
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(commands, &blit);
        }
    }

    const auto submit_start = std::chrono::steady_clock::now();
    impl.stats.cpu_milliseconds = std::chrono::duration<float, std::milli>(submit_start - cpu_start).count();
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        FatalSdlError("cannot submit frame");
    }
    impl.stats.wait_milliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - submit_start).count();
    impl.in_frame = false;
    impl.has_finished_frame = true;
}

void Renderer::CaptureFrame(Image& image) {
    if (impl_->in_frame || !impl_->has_finished_frame) {
        FatalError("Renderer::CaptureFrame: call it after EndFrame");
    }

    const Uint32 width = static_cast<Uint32>(impl_->config.width);
    const Uint32 height = static_cast<Uint32>(impl_->config.height);
    const Uint32 byte_count = width * height * 4;

    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transfer_info.size = byte_count;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(impl_->device, &transfer_info);
    if (transfer == nullptr) {
        FatalSdlError("cannot create download transfer buffer");
    }

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
    if (commands == nullptr) {
        FatalSdlError("cannot acquire command buffer");
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureRegion source{};
    source.texture = impl_->output_target;
    source.w = width;
    source.h = height;
    source.d = 1;
    SDL_GPUTextureTransferInfo destination{};
    destination.transfer_buffer = transfer;
    destination.pixels_per_row = width;
    destination.rows_per_layer = height;
    SDL_DownloadFromGPUTexture(copy_pass, &source, &destination);
    SDL_EndGPUCopyPass(copy_pass);

    // A fence is a signal that the GPU has finished these commands. The pixels are only
    // guaranteed to be in the transfer buffer after it is signaled, so wait for it.
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    if (fence == nullptr) {
        FatalSdlError("cannot submit frame download");
    }
    if (!SDL_WaitForGPUFences(impl_->device, true, &fence, 1)) {
        FatalSdlError("cannot wait for frame download");
    }
    SDL_ReleaseGPUFence(impl_->device, fence);

    const std::uint8_t* pixels = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(impl_->device, transfer, false));
    if (pixels == nullptr) {
        FatalSdlError("cannot map download transfer buffer");
    }
    image.width = impl_->config.width;
    image.height = impl_->config.height;
    image.pixels.assign(pixels, pixels + byte_count);
    SDL_UnmapGPUTransferBuffer(impl_->device, transfer);
    SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
}

bool Renderer::SaveFrameToPng(const char* path) {
    Image image;
    CaptureFrame(image);
    return SaveImageToPng(path, image);
}

const PostSettings& Renderer::GetPostSettings() const {
    return impl_->post;
}

void Renderer::SetPostSettings(const PostSettings& settings) {
    impl_->post = settings;
}

void Renderer::ResetTemporalHistory() {
    impl_->has_taa_history = false;
}

bool Renderer::IsDebugUiActive() const {
    return impl_->debug_ui_active;
}

glm::vec4 ComputeShadowCascadeEnds(float near_plane, float shadow_distance) {
    // The splits are computed from 1 m out (or the near plane if it is further): with a
    // near plane of a few centimeters the geometric spacing would spend the sharpest
    // cascade on the first step or two in front of the camera.
    const float start = glm::max(near_plane, glm::min(1.0f, shadow_distance * 0.05f));
    const float far_distance = glm::max(shadow_distance, start * 2.0f);
    glm::vec4 ends;
    for (int i = 0; i < kCascadeCount; ++i) {
        // A mix of even and geometric spacing: ranges grow with distance, but not too fast.
        const float fraction = static_cast<float>(i + 1) / static_cast<float>(kCascadeCount);
        const float geometric = start * std::pow(far_distance / start, fraction);
        const float even = start + (far_distance - start) * fraction;
        ends[i] = glm::mix(even, geometric, 0.75f);
    }
    return ends;
}

FrameStats Renderer::GetFrameStats() const {
    return impl_->stats;
}

const char* Renderer::GetBackendName() const {
    return SDL_GetGPUDeviceDriver(impl_->device);
}

int Renderer::GetWidth() const {
    return impl_->config.width;
}

int Renderer::GetHeight() const {
    return impl_->config.height;
}

int Renderer::GetRenderWidth() const {
    return impl_->render_width;
}

int Renderer::GetRenderHeight() const {
    return impl_->render_height;
}

int Renderer::GetMsaaSamples() const {
    return impl_->msaa_samples;
}

void Renderer::SetWindowTitle(const char* title) {
    if (impl_->window != nullptr) {
        SDL_SetWindowTitle(impl_->window, title);
    }
}

} // namespace poligon
