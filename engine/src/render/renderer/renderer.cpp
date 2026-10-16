// The renderer's public API (poligon/render/renderer.h): creating it, recording a frame's draws, running the
// frame's passes in EndFrame and showing the result, reading frames back, settings and the window.

#include "renderer_internal.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

namespace poligon {

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
    impl_->sky_map = impl_->sky_occlusion_source != nullptr ? impl_->sky_occlusion_source->GetGroundMap() : GroundMap{};
    if (impl_->sky_map.resolution >= 2 && impl_->sky_map.values != nullptr) {
        const GroundMap& map = impl_->sky_map;
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
            impl_->UploadR8(impl_->sky_occlusion_texture, map.values, map.resolution);
            impl_->sky_occlusion_version = map.version;
        }
    }
    if (impl_->environment.clouds && impl_->environment.draw_sky) {
        impl_->CreateCloudTextures();
    }
    // The trample map, copied to the card when it changed.
    const GroundMap trample = impl_->trample_source != nullptr ? impl_->trample_source->GetGroundMap() : GroundMap{};
    if (trample.resolution < 2 || trample.values == nullptr) {
        impl_->trample_info = glm::vec4(0.0f);
    } else {
        const GroundMap& map = trample;
        if (impl_->trample_texture == nullptr || impl_->trample_resolution != map.resolution) {
            if (impl_->trample_texture != nullptr) {
                SDL_ReleaseGPUTexture(impl_->device, impl_->trample_texture);
            }
            SDL_GPUTextureCreateInfo info{};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
            info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            info.width = static_cast<Uint32>(map.resolution);
            info.height = static_cast<Uint32>(map.resolution);
            info.layer_count_or_depth = 1;
            info.num_levels = 1;
            impl_->trample_texture = SDL_CreateGPUTexture(impl_->device, &info);
            if (impl_->trample_texture == nullptr) {
                FatalSdlError("cannot create trample texture");
            }
            impl_->trample_resolution = map.resolution;
            impl_->trample_version = 0xFFFFFFFFu;
        }
        if (impl_->trample_version != map.version) {
            impl_->UploadR8(impl_->trample_texture, map.values, map.resolution);
            impl_->trample_version = map.version;
        }
        impl_->trample_info = glm::vec4(map.corner.x, map.corner.y, 1.0f / map.size, 1.0f);
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

void Renderer::SetSkyOcclusion(const GroundMapSource* map) {
    impl_->sky_occlusion_source = map;
    impl_->sky_occlusion_version = 0xFFFFFFFFu;
}

void Renderer::SetTrampleMap(const GroundMapSource* map) {
    impl_->trample_source = map;
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

    // What the passes share (FrameContext); each pass is a function in engine/src/render/passes.
    Impl::FrameContext frame;
    frame.commands = SDL_AcquireGPUCommandBuffer(impl.device);
    if (frame.commands == nullptr) {
        FatalSdlError("cannot acquire command buffer");
    }
    SDL_GPUCommandBuffer* commands = frame.commands;

    const float aspect_ratio = static_cast<float>(impl.config.width) / static_cast<float>(impl.config.height);
    const glm::mat4 view = glm::lookAt(camera.position, camera.target, camera.up);
    // Near and far swapped: depth 1 at the near plane, 0 at the far one (reversed depth, see
    // CreateTargets). Everything that reads depth (common.hlsli, ao, fog) follows this.
    const glm::mat4 unshifted_projection = glm::perspective(glm::radians(camera.vertical_fov_degrees), aspect_ratio,
                                                            camera.far_plane, camera.near_plane);
    frame.unshifted_view_projection = unshifted_projection * view;
    const glm::mat4& unshifted_view_projection = frame.unshifted_view_projection;
    // Temporal anti-aliasing: shift the whole picture by a fraction of a pixel, a different
    // one every frame (see TaaUniforms). Everything that renders the scene uses the shifted
    // camera; reprojection uses the unshifted one.
    frame.taa_on = impl.post.temporal_aa;
    if (frame.taa_on && impl.has_taa_history && glm::distance(camera.position, impl.previous_camera_position) > kTaaCutDistance) {
        impl.has_taa_history = false;
    }
    if (!frame.taa_on) {
        impl.has_taa_history = false;
    }
    glm::mat4 projection = unshifted_projection;
    if (frame.taa_on) {
        const int step = static_cast<int>(impl.frame_index % kTaaSequenceLength);
        const float shift_x = Halton(step, 2) - 0.5f;
        const float shift_y = Halton(step, 3) - 0.5f;
        projection[2][0] += shift_x * 2.0f / static_cast<float>(impl.render_width);
        projection[2][1] += shift_y * 2.0f / static_cast<float>(impl.render_height);
        // projection[2][x] multiplies view depth z, and clip w is -z: the picture moves by
        // minus the added amount in normalized coordinates, i.e. -shift_x pixels to the right
        // and +shift_y pixels down. This is the shift in drawn pixels as TAA needs it.
        frame.jitter_pixels = glm::vec2(-shift_x, shift_y);
        impl.temporal_noise_offset = static_cast<float>(impl.frame_index % 16) * 5.588238f;
    } else {
        impl.temporal_noise_offset = 0.0f;
    }
    frame.projection = projection;
    frame.view_projection = projection * view;
    const glm::mat4& view_projection = frame.view_projection;
    frame.light_direction = glm::normalize(light.direction);
    const bool sun_is_on = light.color.r > 0.0f || light.color.g > 0.0f || light.color.b > 0.0f;
    frame.shadows_on = environment.shadows && sun_is_on && (!impl.draw_commands.empty() || !impl.instance_batches.empty());
    const bool shadows_on = frame.shadows_on;
    // The size the scene is drawn at; from temporal anti-aliasing on, the output size.
    frame.width = static_cast<float>(impl.render_width);
    frame.height = static_cast<float>(impl.render_height);
    frame.output_width = static_cast<float>(impl.config.width);
    frame.output_height = static_cast<float>(impl.config.height);
    // What shaders need to turn a depth image value back into a position (see common.hlsli).
    frame.projection_info = glm::vec4(projection[2][2], projection[3][2], 1.0f / projection[0][0], 1.0f / projection[1][1]);
    frame.ao_on = environment.ambient_occlusion && !impl.draw_commands.empty();
    frame.fog_on = environment.volumetric_fog && environment.fog_density > 0.0f;
    frame.clouds_on = environment.clouds && environment.draw_sky && impl.cloud_shape != nullptr;
    frame.cloud_shadow_corner =
        glm::floor((glm::vec2(camera.position.x, camera.position.z) - frame.cloud_shadow_size * 0.5f) / 64.0f) * 64.0f;
    frame.scene_after_depth = impl.msaa_samples == 1;
    frame.water_on = !impl.water_commands.empty();
    frame.bloom_on = impl.post.bloom && impl.post.bloom_strength > 0.0f;
    // The finished scene with one sample per pixel; the passes after the scene work on it.
    frame.scene_image = impl.msaa_samples > 1 ? impl.scene_resolved : impl.scene_color;  // replaced by TAA's result
    frame.white = impl.textures[impl.white_texture.id - 1];

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

    impl.RenderShadowPass(frame);
    impl.RenderCloudShadowPass(frame);
    impl.RenderDepthPass(frame);
    impl.RenderScenePass(frame);
    impl.RenderWaterPass(frame);
    impl.RenderCloudPass(frame);
    impl.RenderFogPass(frame);
    impl.RenderTemporalAaPass(frame);
    impl.previous_view_projection = unshifted_view_projection;
    impl.previous_camera_position = camera.position;
    ++impl.frame_index;
    impl.RenderBloomPass(frame);
    impl.RenderOutputPass(frame);

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

bool Renderer::SetWindowMode(WindowMode mode, int width, int height) {
    if (impl_->in_frame) {
        FatalError("Renderer::SetWindowMode: called between BeginFrame and EndFrame");
    }
    if (width < 16 || height < 16 || width > 8192 || height > 8192) {
        return false;
    }
    if (impl_->window != nullptr) {
        if (mode == WindowMode::Fullscreen) {
            // An exact mode of the display the window is on, at its highest refresh rate.
            SDL_DisplayMode display_mode{};
            const SDL_DisplayID display = SDL_GetDisplayForWindow(impl_->window);
            if (!SDL_GetClosestFullscreenDisplayMode(display, width, height, 0.0f, false, &display_mode) ||
                display_mode.w != width || display_mode.h != height) {
                return false;
            }
            if (!SDL_SetWindowFullscreenMode(impl_->window, &display_mode) || !SDL_SetWindowFullscreen(impl_->window, true)) {
                return false;
            }
        } else {
            SDL_SetWindowFullscreen(impl_->window, false);
            SDL_SyncWindow(impl_->window);
            SDL_SetWindowSize(impl_->window, width, height);
            SDL_SetWindowPosition(impl_->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
        SDL_SyncWindow(impl_->window);
    }
    impl_->window_mode = mode;
    if (width != impl_->config.width || height != impl_->config.height) {
        // The images of the old size may still be in use by the frame the card is drawing.
        SDL_WaitForGPUIdle(impl_->device);
        impl_->ReleaseSizedTargets();
        impl_->config.width = width;
        impl_->config.height = height;
        impl_->CreateSizedTargets();
        impl_->has_taa_history = false;
    }
    return true;
}

WindowMode Renderer::GetWindowMode() const {
    return impl_->window_mode;
}

} // namespace poligon
