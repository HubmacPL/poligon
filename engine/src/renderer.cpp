#include "poligon/renderer.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "poligon/fatal_error.h"
#include "poligon_shaders/mesh_frag_dxil.h"
#include "poligon_shaders/mesh_frag_spirv.h"
#include "poligon_shaders/mesh_vert_dxil.h"
#include "poligon_shaders/mesh_vert_spirv.h"

namespace poligon {

namespace {

// Layouts must match the cbuffers in engine/shaders/mesh.*.hlsl.
struct FrameUniforms {
    glm::mat4 view_projection;
};

struct ObjectUniforms {
    glm::mat4 model;
    float color[4];
    float uv_scale[4];
};

struct LightUniforms {
    float direction[4];
    float color[4];
    float ambient[4];
};

// The GPU reads these as tightly packed floats.
static_assert(sizeof(glm::vec2) == 2 * sizeof(float), "glm::vec2 must be two floats");
static_assert(sizeof(glm::vec3) == 3 * sizeof(float), "glm::vec3 must be three floats");
static_assert(sizeof(glm::mat4) == 16 * sizeof(float), "glm::mat4 must be sixteen floats");
static_assert(sizeof(MeshVertex) == 8 * sizeof(float), "MeshVertex must have no padding");

// The off-screen image stores sRGB-encoded bytes, like image files and displays do. The GPU
// converts automatically: shaders write linear values, the stored bytes are sRGB.
constexpr SDL_GPUTextureFormat kColorTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;

struct Mesh {
    SDL_GPUBuffer* vertex_buffer = nullptr;
    SDL_GPUBuffer* index_buffer = nullptr;
    Uint32 index_count = 0;
};

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
    // without installing anything. See docs/rendering.md.
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

} // namespace

struct Renderer::Impl {
    RendererConfig config;
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUTexture* color_target = nullptr;
    SDL_GPUTexture* depth_target = nullptr;
    SDL_GPUTextureFormat depth_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    SDL_GPUGraphicsPipeline* mesh_pipeline = nullptr;
    SDL_GPUSampler* texture_sampler = nullptr;
    std::vector<Mesh> meshes;
    std::vector<SDL_GPUTexture*> textures;
    // Used for materials without a texture, so that one shader serves both cases.
    TextureHandle white_texture;
    // Clear color as linear values, the form the sRGB render target expects.
    SDL_FColor linear_clear_color{};

    // Valid only between BeginFrame and EndFrame.
    SDL_GPUCommandBuffer* frame_commands = nullptr;
    SDL_GPURenderPass* frame_pass = nullptr;
    bool has_finished_frame = false;

    SDL_GPUShader* CreateShader(SDL_GPUShaderStage stage, Uint32 sampler_count, Uint32 uniform_buffer_count,
                                const unsigned char* dxil, size_t dxil_size, const unsigned char* spirv,
                                size_t spirv_size) {
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

        SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
        if (shader == nullptr) {
            FatalSdlError("cannot create shader");
        }
        return shader;
    }

    void CreateTargets() {
        SDL_GPUTextureCreateInfo color_info{};
        color_info.type = SDL_GPU_TEXTURETYPE_2D;
        color_info.format = kColorTargetFormat;
        // SAMPLER is needed because the image is copied (blitted) to the window.
        color_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        color_info.width = static_cast<Uint32>(config.width);
        color_info.height = static_cast<Uint32>(config.height);
        color_info.layer_count_or_depth = 1;
        color_info.num_levels = 1;
        color_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        // Direct3D 12 clears fastest (and without debug layer warnings) when the clear values
        // are declared at creation. Other backends ignore these properties.
        const SDL_PropertiesID clear_properties = SDL_CreateProperties();
        SDL_SetFloatProperty(clear_properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_R_FLOAT, linear_clear_color.r);
        SDL_SetFloatProperty(clear_properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_G_FLOAT, linear_clear_color.g);
        SDL_SetFloatProperty(clear_properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_B_FLOAT, linear_clear_color.b);
        SDL_SetFloatProperty(clear_properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_A_FLOAT, linear_clear_color.a);
        SDL_SetFloatProperty(clear_properties, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_DEPTH_FLOAT, 1.0f);
        color_info.props = clear_properties;
        color_target = SDL_CreateGPUTexture(device, &color_info);
        if (color_target == nullptr) {
            FatalSdlError("cannot create color target");
        }

        if (!SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                          SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
            depth_format = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
        }
        SDL_GPUTextureCreateInfo depth_info = color_info;
        depth_info.format = depth_format;
        depth_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        depth_target = SDL_CreateGPUTexture(device, &depth_info);
        if (depth_target == nullptr) {
            FatalSdlError("cannot create depth target");
        }
        SDL_DestroyProperties(clear_properties);
    }

    void CreateTextureSampler() {
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
        if (texture_sampler == nullptr) {
            FatalSdlError("cannot create texture sampler");
        }
    }

    void CreateMeshPipeline() {
        SDL_GPUShader* vertex_shader =
            CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0, 2, kMeshVertDxil, sizeof(kMeshVertDxil), kMeshVertSpirv,
                         sizeof(kMeshVertSpirv));
        SDL_GPUShader* fragment_shader =
            CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, kMeshFragDxil, sizeof(kMeshFragDxil), kMeshFragSpirv,
                         sizeof(kMeshFragSpirv));

        SDL_GPUVertexBufferDescription vertex_buffer{};
        vertex_buffer.slot = 0;
        vertex_buffer.pitch = sizeof(MeshVertex);
        vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attributes[3]{};
        attributes[0].location = 0;
        attributes[0].buffer_slot = 0;
        attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[0].offset = offsetof(MeshVertex, position);
        attributes[1].location = 1;
        attributes[1].buffer_slot = 0;
        attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attributes[1].offset = offsetof(MeshVertex, normal);
        attributes[2].location = 2;
        attributes[2].buffer_slot = 0;
        attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attributes[2].offset = offsetof(MeshVertex, uv);

        SDL_GPUColorTargetDescription color_description{};
        color_description.format = kColorTargetFormat;

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
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = true;
        info.target_info.color_target_descriptions = &color_description;
        info.target_info.num_color_targets = 1;
        info.target_info.depth_stencil_format = depth_format;
        info.target_info.has_depth_stencil_target = true;

        mesh_pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
        if (mesh_pipeline == nullptr) {
            FatalSdlError("cannot create mesh pipeline");
        }

        // The pipeline keeps what it needs; the shader objects can go.
        SDL_ReleaseGPUShader(device, vertex_shader);
        SDL_ReleaseGPUShader(device, fragment_shader);
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
};

Renderer::Renderer(const RendererConfig& config) : impl_(std::make_unique<Impl>()) {
    if (config.width < 1 || config.height < 1) {
        FatalError("Renderer: width and height must be at least 1");
    }
    impl_->config = config;
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
        // SDR_LINEAR: the window image is sRGB-encoded too, so copying the off-screen image to it
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
    impl_->CreateTextureSampler();
    impl_->CreateMeshPipeline();

    TextureOptions white_options;
    white_options.generate_mipmaps = false;
    impl_->white_texture = CreateTexture(MakeSolidImage(1, 1, 255, 255, 255), white_options);
}

Renderer::~Renderer() {
    if (impl_->device != nullptr) {
        SDL_WaitForGPUIdle(impl_->device);
        for (const Mesh& mesh : impl_->meshes) {
            SDL_ReleaseGPUBuffer(impl_->device, mesh.vertex_buffer);
            SDL_ReleaseGPUBuffer(impl_->device, mesh.index_buffer);
        }
        for (SDL_GPUTexture* texture : impl_->textures) {
            SDL_ReleaseGPUTexture(impl_->device, texture);
        }
        SDL_ReleaseGPUSampler(impl_->device, impl_->texture_sampler);
        SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->mesh_pipeline);
        SDL_ReleaseGPUTexture(impl_->device, impl_->depth_target);
        SDL_ReleaseGPUTexture(impl_->device, impl_->color_target);
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
    impl_->meshes.push_back(mesh);

    MeshHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->meshes.size());
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
    if (impl_->frame_commands != nullptr) {
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
    Image smaller_level;
    const Image* level_image = &image;
    for (Uint32 level = 0; level < level_count; ++level) {
        if (level > 0) {
            smaller_level = MakeHalfSizeImage(*level_image, options.is_color);
            level_image = &smaller_level;
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
    TextureHandle handle;
    handle.id = static_cast<std::uint32_t>(impl_->textures.size());
    return handle;
}

void Renderer::BeginFrame(const Camera& camera, const DirectionalLight& light) {
    if (impl_->frame_commands != nullptr) {
        FatalError("Renderer::BeginFrame: the previous frame was not finished with EndFrame");
    }

    impl_->frame_commands = SDL_AcquireGPUCommandBuffer(impl_->device);
    if (impl_->frame_commands == nullptr) {
        FatalSdlError("cannot acquire command buffer");
    }

    SDL_GPUColorTargetInfo color_info{};
    color_info.texture = impl_->color_target;
    color_info.clear_color = impl_->linear_clear_color;
    color_info.load_op = SDL_GPU_LOADOP_CLEAR;
    color_info.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depth_info{};
    depth_info.texture = impl_->depth_target;
    depth_info.clear_depth = 1.0f;
    depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_info.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_info.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_info.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    impl_->frame_pass = SDL_BeginGPURenderPass(impl_->frame_commands, &color_info, 1, &depth_info);
    if (impl_->frame_pass == nullptr) {
        FatalSdlError("cannot begin render pass");
    }
    SDL_BindGPUGraphicsPipeline(impl_->frame_pass, impl_->mesh_pipeline);

    const float aspect_ratio = static_cast<float>(impl_->config.width) / static_cast<float>(impl_->config.height);
    const glm::mat4 view = glm::lookAt(camera.position, camera.target, camera.up);
    const glm::mat4 projection = glm::perspective(glm::radians(camera.vertical_fov_degrees), aspect_ratio,
                                                  camera.near_plane, camera.far_plane);
    FrameUniforms frame_uniforms;
    frame_uniforms.view_projection = projection * view;
    SDL_PushGPUVertexUniformData(impl_->frame_commands, 0, &frame_uniforms, sizeof(frame_uniforms));

    const glm::vec3 direction = glm::normalize(light.direction);
    const LightUniforms light_uniforms = {
        {direction.x, direction.y, direction.z, 0.0f},
        {light.color.r, light.color.g, light.color.b, light.color.a},
        {light.ambient.r, light.ambient.g, light.ambient.b, light.ambient.a},
    };
    SDL_PushGPUFragmentUniformData(impl_->frame_commands, 0, &light_uniforms, sizeof(light_uniforms));
}

void Renderer::DrawMesh(MeshHandle mesh_handle, const glm::mat4& transform, const Material& material) {
    if (impl_->frame_pass == nullptr) {
        FatalError("Renderer::DrawMesh: call BeginFrame first");
    }
    if (mesh_handle.id == 0 || mesh_handle.id > impl_->meshes.size()) {
        FatalError("Renderer::DrawMesh: invalid mesh handle");
    }
    if (material.texture.id > impl_->textures.size()) {
        FatalError("Renderer::DrawMesh: invalid texture handle");
    }
    const Mesh& mesh = impl_->meshes[mesh_handle.id - 1];
    const TextureHandle texture = material.texture.id != 0 ? material.texture : impl_->white_texture;

    ObjectUniforms object_uniforms;
    object_uniforms.model = transform;
    object_uniforms.color[0] = material.color.r;
    object_uniforms.color[1] = material.color.g;
    object_uniforms.color[2] = material.color.b;
    object_uniforms.color[3] = material.color.a;
    object_uniforms.uv_scale[0] = material.uv_scale.x;
    object_uniforms.uv_scale[1] = material.uv_scale.y;
    object_uniforms.uv_scale[2] = 0.0f;
    object_uniforms.uv_scale[3] = 0.0f;
    SDL_PushGPUVertexUniformData(impl_->frame_commands, 1, &object_uniforms, sizeof(object_uniforms));

    SDL_GPUTextureSamplerBinding texture_binding{};
    texture_binding.texture = impl_->textures[texture.id - 1];
    texture_binding.sampler = impl_->texture_sampler;
    SDL_BindGPUFragmentSamplers(impl_->frame_pass, 0, &texture_binding, 1);

    SDL_GPUBufferBinding vertex_binding{};
    vertex_binding.buffer = mesh.vertex_buffer;
    SDL_BindGPUVertexBuffers(impl_->frame_pass, 0, &vertex_binding, 1);
    SDL_GPUBufferBinding index_binding{};
    index_binding.buffer = mesh.index_buffer;
    SDL_BindGPUIndexBuffer(impl_->frame_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(impl_->frame_pass, mesh.index_count, 1, 0, 0, 0);
}

void Renderer::DrawMesh(MeshHandle mesh, const glm::mat4& transform, Color color) {
    Material material;
    material.color = color;
    DrawMesh(mesh, transform, material);
}

void Renderer::EndFrame() {
    if (impl_->frame_pass == nullptr) {
        FatalError("Renderer::EndFrame: call BeginFrame first");
    }
    SDL_EndGPURenderPass(impl_->frame_pass);
    impl_->frame_pass = nullptr;

    if (impl_->window != nullptr) {
        // The swapchain texture is the image the window shows. It can be unavailable
        // (for example while the window is minimized); the frame is then simply not shown.
        SDL_GPUTexture* swapchain_texture = nullptr;
        Uint32 swapchain_width = 0;
        Uint32 swapchain_height = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(impl_->frame_commands, impl_->window, &swapchain_texture,
                                                   &swapchain_width, &swapchain_height)) {
            FatalSdlError("cannot acquire window image");
        }
        if (swapchain_texture != nullptr) {
            SDL_GPUBlitInfo blit{};
            blit.source.texture = impl_->color_target;
            blit.source.w = static_cast<Uint32>(impl_->config.width);
            blit.source.h = static_cast<Uint32>(impl_->config.height);
            blit.destination.texture = swapchain_texture;
            blit.destination.w = swapchain_width;
            blit.destination.h = swapchain_height;
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(impl_->frame_commands, &blit);
        }
    }

    if (!SDL_SubmitGPUCommandBuffer(impl_->frame_commands)) {
        FatalSdlError("cannot submit frame");
    }
    impl_->frame_commands = nullptr;
    impl_->has_finished_frame = true;
}

void Renderer::CaptureFrame(Image& image) {
    if (impl_->frame_commands != nullptr || !impl_->has_finished_frame) {
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
    source.texture = impl_->color_target;
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

const char* Renderer::GetBackendName() const {
    return SDL_GetGPUDeviceDriver(impl_->device);
}

int Renderer::GetWidth() const {
    return impl_->config.width;
}

int Renderer::GetHeight() const {
    return impl_->config.height;
}

} // namespace poligon
