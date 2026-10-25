// Drawing recorded meshes, instance batches, terrain and impostors inside a pass; culling instances.

#include "renderer_internal.h"

namespace poligon {

void Renderer::Impl::AddLightingBindings(SDL_GPUTextureSamplerBinding* bindings) const {
    bindings[0].texture = frame_cloud_shadow;
    bindings[0].sampler = linear_clamp_sampler;
    bindings[1].texture = frame_sky_occlusion;
    bindings[1].sampler = linear_clamp_sampler;
    bindings[2].texture = sky_radiance;
    bindings[2].sampler = cube_sampler;
}

void Renderer::Impl::DrawCommandGeometry(SDL_GPURenderPass* pass, const DrawCommand& command) {
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

ObjectUniforms Renderer::Impl::MakeObjectUniforms(const DrawCommand& command) {
    ObjectUniforms uniforms;
    uniforms.model = command.transform;
    uniforms.color = ToVec4(command.material.color);
    uniforms.uv_scale = glm::vec4(command.material.uv_scale.x, command.material.uv_scale.y,
                                  static_cast<float>(command.first_joint), static_cast<float>(command.previous_first_joint));
    uniforms.wind_response = glm::vec4(command.material.wind_bend, command.material.wind_flutter, command.fade, 0.0f);
    uniforms.previous_model = command.previous_transform;
    uniforms.normal_matrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(command.transform))));
    return uniforms;
}

glm::vec4 Renderer::Impl::WindUniform() const {
    const float length = glm::length(environment.wind_direction);
    const glm::vec2 direction = length > 0.0001f ? environment.wind_direction / length : glm::vec2(1.0f, 0.0f);
    return glm::vec4(direction.x, direction.y, glm::max(environment.wind_strength, 0.0f), environment.wind_time);
}

void Renderer::Impl::BindMeshPipeline(SDL_GPURenderPass* pass, MeshPass mesh_pass, const DrawCommand& command,
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

void Renderer::Impl::DrawDepthGeometry(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, AoFilter filter) {
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

bool Renderer::Impl::HasAoNonCasters() const {
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

void Renderer::Impl::DrawInstanceBatches(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, MeshPass mesh_pass, int list,
                         bool ao_on, SDL_GPUGraphicsPipeline*& bound_pipeline, AoFilter filter) {
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
            SDL_GPUTextureSamplerBinding bindings[9]{};
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
                bindings[8].texture = GetTexture(material.emissive_texture, white_texture);
                bindings[8].sampler = texture_sampler;
                SDL_BindGPUFragmentSamplers(pass, 0, bindings, 9);
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

void Renderer::Impl::DrawTerrainBatch(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, MeshPass mesh_pass, int list, bool ao_on,
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

        SDL_GPUTextureSamplerBinding bindings[10]{};
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
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, 10);
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

void Renderer::Impl::DrawImpostorBatch(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, MeshPass mesh_pass, int list, bool ao_on,
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

    SDL_GPUTextureSamplerBinding bindings[7]{};
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
        SDL_BindGPUFragmentSamplers(pass, 0, bindings, 7);
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

void Renderer::Impl::CullInstances(const glm::mat4& view_projection, const Cascades* cascades) {
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

MaterialUniforms Renderer::Impl::MakeMaterialUniforms(const Material& material, float noise_offset) {
    MaterialUniforms uniforms;
    uniforms.factors = glm::vec4(material.metallic, material.roughness, material.alpha_cutoff, noise_offset);
    uniforms.flags = glm::vec4(material.normal_texture.id != 0 ? 1.0f : 0.0f, material.orm_texture.id != 0 ? 1.0f : 0.0f,
                               material.occlusion_in_orm ? 1.0f : 0.0f,
                               material.double_sided ? (material.back_faces_keep_normal ? 2.0f : 1.0f) : 0.0f);
    uniforms.shading = glm::vec4(glm::clamp(material.normal_up, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f);
    const float strength = glm::max(material.emissive_strength, 0.0f);
    uniforms.emissive = glm::vec4(glm::max(glm::vec3(material.emissive.r, material.emissive.g, material.emissive.b), 0.0f) * strength,
                                  material.emissive_texture.id != 0 ? 1.0f : 0.0f);
    return uniforms;
}

} // namespace poligon
