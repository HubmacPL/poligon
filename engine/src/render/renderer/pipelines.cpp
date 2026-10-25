// Shaders and graphics pipelines, made once when the renderer starts.

#include "renderer_internal.h"

#include "poligon_shaders/ao_blur_frag_dxil.h"
#include "poligon_shaders/ao_blur_frag_spirv.h"
#include "poligon_shaders/ao_frag_dxil.h"
#include "poligon_shaders/ao_frag_spirv.h"
#include "poligon_shaders/bloom_down_frag_dxil.h"
#include "poligon_shaders/bloom_down_frag_spirv.h"
#include "poligon_shaders/bloom_up_frag_dxil.h"
#include "poligon_shaders/bloom_up_frag_spirv.h"
#include "poligon_shaders/cloud_shadow_frag_dxil.h"
#include "poligon_shaders/cloud_shadow_frag_spirv.h"
#include "poligon_shaders/clouds_composite_frag_dxil.h"
#include "poligon_shaders/clouds_composite_frag_spirv.h"
#include "poligon_shaders/clouds_resolve_frag_dxil.h"
#include "poligon_shaders/clouds_resolve_frag_spirv.h"
#include "poligon_shaders/sky_capture_frag_dxil.h"
#include "poligon_shaders/sky_capture_frag_spirv.h"
#include "poligon_shaders/sky_prefilter_frag_dxil.h"
#include "poligon_shaders/sky_prefilter_frag_spirv.h"
#include "poligon_shaders/clouds_frag_dxil.h"
#include "poligon_shaders/clouds_frag_spirv.h"
#include "poligon_shaders/depth_copy_frag_dxil.h"
#include "poligon_shaders/depth_copy_frag_spirv.h"
#include "poligon_shaders/fog_composite_frag_dxil.h"
#include "poligon_shaders/fog_composite_frag_spirv.h"
#include "poligon_shaders/fog_frag_dxil.h"
#include "poligon_shaders/fog_frag_spirv.h"
#include "poligon_shaders/fullscreen_vert_dxil.h"
#include "poligon_shaders/fullscreen_vert_spirv.h"
#include "poligon_shaders/impostor_after_depth_frag_dxil.h"
#include "poligon_shaders/impostor_after_depth_frag_spirv.h"
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
#include "poligon_shaders/depth_motion_frag_dxil.h"
#include "poligon_shaders/depth_motion_frag_spirv.h"
#include "poligon_shaders/sky_frag_dxil.h"
#include "poligon_shaders/sky_frag_spirv.h"
#include "poligon_shaders/taa_frag_dxil.h"
#include "poligon_shaders/taa_frag_spirv.h"
#include "poligon_shaders/terrain_frag_dxil.h"
#include "poligon_shaders/terrain_frag_spirv.h"
#include "poligon_shaders/terrain_vert_dxil.h"
#include "poligon_shaders/terrain_vert_spirv.h"
#include "poligon_shaders/tonemap_frag_dxil.h"
#include "poligon_shaders/tonemap_frag_spirv.h"
#include "poligon_shaders/water_frag_dxil.h"
#include "poligon_shaders/water_frag_spirv.h"

namespace poligon {

SDL_GPUShader* Renderer::Impl::CreateShader(SDL_GPUShaderStage stage, Uint32 sampler_count, Uint32 uniform_buffer_count,
                            const unsigned char* dxil, size_t dxil_size, const unsigned char* spirv,
                            size_t spirv_size, Uint32 storage_buffer_count) {
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

SDL_GPUGraphicsPipeline* Renderer::Impl::CreateFullscreenPipeline(SDL_GPUShader* vertex_shader, SDL_GPUShader* fragment_shader,
                                                  SDL_GPUTextureFormat format, FullscreenBlend blend, SDL_GPUCullMode cull) {
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
    info.rasterizer_state.cull_mode = cull;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &color_description;
    info.target_info.num_color_targets = 1;
    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    if (pipeline == nullptr) {
        FatalSdlError("cannot create full-screen pipeline");
    }
    return pipeline;
}

void Renderer::Impl::DrawFullscreenPass(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target, bool keep_target,
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

void Renderer::Impl::DrawFullscreenToCube(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target, int face, int level,
                                          SDL_GPUGraphicsPipeline* pipeline, const SDL_GPUTextureSamplerBinding* bindings,
                                          Uint32 binding_count, const void* uniforms0, Uint32 size0, const void* uniforms1,
                                          Uint32 size1) {
    SDL_GPUColorTargetInfo color_info{};
    color_info.texture = target;
    color_info.mip_level = static_cast<Uint32>(level);
    color_info.layer_or_depth_plane = static_cast<Uint32>(face);
    color_info.load_op = SDL_GPU_LOADOP_DONT_CARE;
    color_info.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color_info, 1, nullptr);
    if (pass == nullptr) {
        FatalSdlError("cannot begin a cube map pass");
    }
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_BindGPUFragmentSamplers(pass, 0, bindings, binding_count);
    SDL_PushGPUFragmentUniformData(commands, 0, uniforms0, size0);
    if (uniforms1 != nullptr) {
        SDL_PushGPUFragmentUniformData(commands, 1, uniforms1, size1);
    }
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

void Renderer::Impl::CreatePipelines() {
    SDL_GPUShader* mesh_vertex = CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0, 2, kMeshVertDxil, sizeof(kMeshVertDxil),
                                              kMeshVertSpirv, sizeof(kMeshVertSpirv));
    SDL_GPUShader* mesh_fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 9, 2, kMeshFragDxil,
                                                sizeof(kMeshFragDxil), kMeshFragSpirv, sizeof(kMeshFragSpirv));
    SDL_GPUShader* shadow_fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, kShadowFragDxil,
                                                  sizeof(kShadowFragDxil), kShadowFragSpirv, sizeof(kShadowFragSpirv));
    SDL_GPUShader* depth_motion_fragment =
        CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, kDepthMotionFragDxil, sizeof(kDepthMotionFragDxil), kDepthMotionFragSpirv,
                     sizeof(kDepthMotionFragSpirv));
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

    // --- Meshes in the depth pass: depth and motion vectors, seen from the camera, one sample per pixel ---
    SDL_GPUGraphicsPipelineCreateInfo after_depth_info{};  // filled after the skinned pipelines
    SDL_GPUGraphicsPipelineCreateInfo prepass_info = mesh_info;
    SDL_GPUColorTargetDescription motion_description{};
    motion_description.format = kMotionFormat;
    prepass_info.fragment_shader = depth_motion_fragment;
    prepass_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    prepass_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    prepass_info.multisample_state.enable_alpha_to_coverage = false;
    prepass_info.target_info.color_target_descriptions = &motion_description;
    prepass_info.target_info.num_color_targets = 1;
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
        CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 9, 2, kMeshAfterDepthFragDxil, sizeof(kMeshAfterDepthFragDxil),
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
    SDL_GPUShader* terrain_fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 10, 2, kTerrainFragDxil,
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
    terrain_pipelines[kDepthPass] = create_terrain(prepass_info, depth_motion_fragment, SDL_GPU_CULLMODE_BACK);
    // The terrain shader discards nothing, so the depth test runs before it anyway.
    terrain_pipelines[kSceneAfterDepthPass] = create_terrain(after_depth_info, terrain_fragment, SDL_GPU_CULLMODE_BACK);
    SDL_ReleaseGPUShader(device, terrain_vertex);
    SDL_ReleaseGPUShader(device, terrain_fragment);

    // --- Impostors: cards facing the camera, in the scene and depth passes ---
    SDL_GPUShader* impostor_vertex =
        CreateShader(SDL_GPU_SHADERSTAGE_VERTEX, 0, 2, kImpostorVertDxil, sizeof(kImpostorVertDxil), kImpostorVertSpirv,
                     sizeof(kImpostorVertSpirv), 1);
    SDL_GPUShader* impostor_fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 7, 2, kImpostorFragDxil,
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
        CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 7, 2, kImpostorAfterDepthFragDxil, sizeof(kImpostorAfterDepthFragDxil),
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
        {&fog_pipeline, 4, kFogFragDxil, sizeof(kFogFragDxil), kFogFragSpirv, sizeof(kFogFragSpirv), kSceneFormat,
         FullscreenBlend::Replace},
        {&fog_composite_pipeline, 2, kFogCompositeFragDxil, sizeof(kFogCompositeFragDxil), kFogCompositeFragSpirv,
         sizeof(kFogCompositeFragSpirv), kSceneFormat, FullscreenBlend::AddAndDim},
        {&bloom_down_pipeline, 1, kBloomDownFragDxil, sizeof(kBloomDownFragDxil), kBloomDownFragSpirv,
         sizeof(kBloomDownFragSpirv), kSceneFormat, FullscreenBlend::Replace},
        {&bloom_up_pipeline, 1, kBloomUpFragDxil, sizeof(kBloomUpFragDxil), kBloomUpFragSpirv,
         sizeof(kBloomUpFragSpirv), kSceneFormat, FullscreenBlend::Mix},
        {&luminance_pipeline, 2, kLuminanceFragDxil, sizeof(kLuminanceFragDxil), kLuminanceFragSpirv,
         sizeof(kLuminanceFragSpirv), kLuminanceFormat, FullscreenBlend::Replace},
        {&taa_pipeline, 4, kTaaFragDxil, sizeof(kTaaFragDxil), kTaaFragSpirv, sizeof(kTaaFragSpirv), kSceneFormat,
         FullscreenBlend::Replace},
        {&depth_copy_pipeline, 1, kDepthCopyFragDxil, sizeof(kDepthCopyFragDxil), kDepthCopyFragSpirv,
         sizeof(kDepthCopyFragSpirv), SDL_GPU_TEXTUREFORMAT_R32_FLOAT, FullscreenBlend::Replace},
        {&clouds_pipeline, 4, kCloudsFragDxil, sizeof(kCloudsFragDxil), kCloudsFragSpirv, sizeof(kCloudsFragSpirv), kSceneFormat,
         FullscreenBlend::Replace},
        {&cloud_shadow_pipeline, 3, kCloudShadowFragDxil, sizeof(kCloudShadowFragDxil), kCloudShadowFragSpirv,
         sizeof(kCloudShadowFragSpirv), SDL_GPU_TEXTUREFORMAT_R8_UNORM, FullscreenBlend::Replace},
        {&clouds_composite_pipeline, 2, kCloudsCompositeFragDxil, sizeof(kCloudsCompositeFragDxil), kCloudsCompositeFragSpirv,
         sizeof(kCloudsCompositeFragSpirv), kSceneFormat, FullscreenBlend::AddAndDim},
        {&clouds_resolve_pipeline, 2, kCloudsResolveFragDxil, sizeof(kCloudsResolveFragDxil), kCloudsResolveFragSpirv,
         sizeof(kCloudsResolveFragSpirv), kSceneFormat, FullscreenBlend::Replace},
    };
    for (const FullscreenPass& pass : passes) {
        SDL_GPUShader* fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, pass.sampler_count, 1, pass.dxil,
                                               pass.dxil_size, pass.spirv, pass.spirv_size);
        *pass.pipeline = CreateFullscreenPipeline(fullscreen_vertex, fragment, pass.format, pass.blend);
        SDL_ReleaseGPUShader(device, fragment);
    }

    // --- Water: a mesh drawn over the single-sample scene, testing and writing depth. ---
    {
        SDL_GPUShader* water_fragment = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 7, 2, kWaterFragDxil,
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

    // --- The sky light map: capturing the sky into cube faces and blurring it level by level. ---
    {
        SDL_GPUShader* capture = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 3, 2, kSkyCaptureFragDxil, sizeof(kSkyCaptureFragDxil),
                                              kSkyCaptureFragSpirv, sizeof(kSkyCaptureFragSpirv));
        sky_capture_pipeline = CreateFullscreenPipeline(fullscreen_vertex, capture, kSceneFormat, FullscreenBlend::Replace);
        SDL_ReleaseGPUShader(device, capture);
        SDL_GPUShader* prefilter = CreateShader(SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1, kSkyPrefilterFragDxil,
                                                sizeof(kSkyPrefilterFragDxil), kSkyPrefilterFragSpirv, sizeof(kSkyPrefilterFragSpirv));
        sky_prefilter_pipeline = CreateFullscreenPipeline(fullscreen_vertex, prefilter, kSceneFormat, FullscreenBlend::Replace);
        SDL_ReleaseGPUShader(device, prefilter);
    }

    // Pipelines keep what they need; the shader objects can go.
    SDL_ReleaseGPUShader(device, mesh_vertex);
    SDL_ReleaseGPUShader(device, mesh_fragment);
    SDL_ReleaseGPUShader(device, shadow_fragment);
    SDL_ReleaseGPUShader(device, depth_motion_fragment);
    SDL_ReleaseGPUShader(device, fullscreen_vertex);
    SDL_ReleaseGPUShader(device, sky_fragment);
    SDL_ReleaseGPUShader(device, tonemap_fragment);
}

} // namespace poligon
