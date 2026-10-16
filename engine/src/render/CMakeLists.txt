# Render: the renderer on SDL3 GPU, its HLSL shaders, graphics presets and render settings.
# renderer/: the public API, the frame (EndFrame runs the passes), render targets, pipelines
# and drawing inside a pass, around Renderer::Impl in renderer/renderer_internal.h.
# resources/: what lives on the graphics card. passes/: one file per pass of a frame.
add_library(poligon_render STATIC
  passes/bloom_pass.cpp
  passes/cloud_pass.cpp
  passes/depth_pass.cpp
  passes/fog_pass.cpp
  passes/output_pass.cpp
  passes/scene_pass.cpp
  passes/shadow_pass.cpp
  passes/temporal_aa_pass.cpp
  passes/water_pass.cpp
  renderer/drawing.cpp
  renderer/pipelines.cpp
  renderer/renderer.cpp
  renderer/targets.cpp
  resources/cloud_noise.cpp
  resources/cloud_textures.cpp
  resources/impostor_file.cpp
  resources/impostors.cpp
  resources/meshes.cpp
  resources/terrain.cpp
  resources/textures.cpp
  resources/uploads.cpp
  settings/graphics_quality.cpp
  settings/render_settings.cpp
)
target_link_libraries(poligon_render PUBLIC poligon_core)
target_link_libraries(poligon_render PRIVATE poligon_build_options SDL3::SDL3)
# Dear ImGui draws the debug windows. PUBLIC: games and tools call ImGui:: directly.
target_link_libraries(poligon_render PUBLIC imgui::imgui)
target_include_directories(poligon_render PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

# Shaders are grouped by what they draw: mesh, terrain, vegetation (impostors), sky (sky and
# clouds), water, post (full-screen passes); shaders/common holds what several include.
set(POLIGON_SHADER_SHARED
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/common/common.hlsli
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/common/lighting.hlsli
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/common/impostor.hlsli
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/common/clouds.hlsli
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh/mesh.frag.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vegetation/impostor.frag.hlsl)

# Compiles one HLSL file to DXIL (Direct3D 12) and SPIR-V (Vulkan) and embeds both in the
# engine as byte arrays, so there are no shader files to find at run time.
#   source: path relative to this directory, for example shaders/mesh/mesh.vert.hlsl
#   stage:  vertex or fragment
#   symbol: array name prefix, for example kMeshVert -> kMeshVertDxil, kMeshVertSpirv
#   header: header name prefix, for example mesh_vert -> mesh_vert_dxil.h, mesh_vert_spirv.h
function(poligon_embed_shader source stage symbol header)
  if(stage STREQUAL "vertex")
    set(profile vs_6_0)
  elseif(stage STREQUAL "fragment")
    set(profile ps_6_0)
  else()
    message(FATAL_ERROR "Unknown shader stage: ${stage}")
  endif()

  set(input ${CMAKE_CURRENT_SOURCE_DIR}/${source})
  set(output_dir ${CMAKE_CURRENT_BINARY_DIR}/generated/poligon_shaders)
  set(dxil ${output_dir}/${header}.dxil)
  set(spirv ${output_dir}/${header}.spv)
  set(embed ${PROJECT_SOURCE_DIR}/cmake/EmbedBinary.cmake)

  add_custom_command(
    OUTPUT ${output_dir}/${header}_dxil.h ${output_dir}/${header}_spirv.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
    # Shaders carry [[vk::...]] attributes that only matter for SPIR-V; the DXIL compile would
    # report them as ignored, so that one warning is off here.
    COMMAND ${DIRECTX_DXC_TOOL} -nologo -WX -Wno-ignored-attributes -I ${CMAKE_CURRENT_SOURCE_DIR}/shaders/common -T ${profile} -E main -Fo ${dxil} ${input}
    COMMAND ${DIRECTX_DXC_TOOL} -nologo -WX -spirv -I ${CMAKE_CURRENT_SOURCE_DIR}/shaders/common -T ${profile} -E main -Fo ${spirv} ${input}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${dxil} -DOUTPUT=${output_dir}/${header}_dxil.h -DSYMBOL=${symbol}Dxil -P ${embed}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${spirv} -DOUTPUT=${output_dir}/${header}_spirv.h -DSYMBOL=${symbol}Spirv -P ${embed}
    # Every shader may include the shared files (shaders/common, and the two fragment shaders
    # others build on), so a change there rebuilds all of them.
    DEPENDS ${input} ${embed} ${POLIGON_SHADER_SHARED}
    COMMENT "Compiling shader ${source}"
    VERBATIM)
  target_sources(poligon_render PRIVATE ${output_dir}/${header}_dxil.h ${output_dir}/${header}_spirv.h)
endfunction()

# mesh
poligon_embed_shader(shaders/mesh/mesh.vert.hlsl vertex kMeshVert mesh_vert)
poligon_embed_shader(shaders/mesh/mesh_skinned.vert.hlsl vertex kMeshSkinnedVert mesh_skinned_vert)
poligon_embed_shader(shaders/mesh/mesh_instanced.vert.hlsl vertex kMeshInstancedVert mesh_instanced_vert)
poligon_embed_shader(shaders/mesh/mesh.frag.hlsl fragment kMeshFrag mesh_frag)
poligon_embed_shader(shaders/mesh/shadow.frag.hlsl fragment kShadowFrag shadow_frag)
poligon_embed_shader(shaders/mesh/mesh_after_depth.frag.hlsl fragment kMeshAfterDepthFrag mesh_after_depth_frag)
# terrain
poligon_embed_shader(shaders/terrain/terrain.vert.hlsl vertex kTerrainVert terrain_vert)
poligon_embed_shader(shaders/terrain/terrain.frag.hlsl fragment kTerrainFrag terrain_frag)
# vegetation
poligon_embed_shader(shaders/vegetation/impostor.vert.hlsl vertex kImpostorVert impostor_vert)
poligon_embed_shader(shaders/vegetation/impostor.frag.hlsl fragment kImpostorFrag impostor_frag)
poligon_embed_shader(shaders/vegetation/impostor_depth.frag.hlsl fragment kImpostorDepthFrag impostor_depth_frag)
poligon_embed_shader(shaders/vegetation/impostor_bake.frag.hlsl fragment kImpostorBakeFrag impostor_bake_frag)
poligon_embed_shader(shaders/vegetation/impostor_after_depth.frag.hlsl fragment kImpostorAfterDepthFrag impostor_after_depth_frag)
# sky
poligon_embed_shader(shaders/sky/clouds.frag.hlsl fragment kCloudsFrag clouds_frag)
poligon_embed_shader(shaders/sky/cloud_shadow.frag.hlsl fragment kCloudShadowFrag cloud_shadow_frag)
poligon_embed_shader(shaders/sky/clouds_composite.frag.hlsl fragment kCloudsCompositeFrag clouds_composite_frag)
poligon_embed_shader(shaders/sky/sky.frag.hlsl fragment kSkyFrag sky_frag)
# water
poligon_embed_shader(shaders/water/water.frag.hlsl fragment kWaterFrag water_frag)
poligon_embed_shader(shaders/water/depth_copy.frag.hlsl fragment kDepthCopyFrag depth_copy_frag)
# post
poligon_embed_shader(shaders/post/fullscreen.vert.hlsl vertex kFullscreenVert fullscreen_vert)
poligon_embed_shader(shaders/post/tonemap.frag.hlsl fragment kTonemapFrag tonemap_frag)
poligon_embed_shader(shaders/post/ao.frag.hlsl fragment kAoFrag ao_frag)
poligon_embed_shader(shaders/post/ao_blur.frag.hlsl fragment kAoBlurFrag ao_blur_frag)
poligon_embed_shader(shaders/post/fog.frag.hlsl fragment kFogFrag fog_frag)
poligon_embed_shader(shaders/post/fog_composite.frag.hlsl fragment kFogCompositeFrag fog_composite_frag)
poligon_embed_shader(shaders/post/bloom_down.frag.hlsl fragment kBloomDownFrag bloom_down_frag)
poligon_embed_shader(shaders/post/bloom_up.frag.hlsl fragment kBloomUpFrag bloom_up_frag)
poligon_embed_shader(shaders/post/luminance.frag.hlsl fragment kLuminanceFrag luminance_frag)
poligon_embed_shader(shaders/post/taa.frag.hlsl fragment kTaaFrag taa_frag)
