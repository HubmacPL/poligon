// Impostors: baking pictures of a model, and their textures on the graphics card.

#include "../renderer/renderer_internal.h"

#include "poligon_shaders/impostor_bake_frag_dxil.h"
#include "poligon_shaders/impostor_bake_frag_spirv.h"
#include "poligon_shaders/mesh_vert_dxil.h"
#include "poligon_shaders/mesh_vert_spirv.h"

namespace poligon {

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

} // namespace poligon
