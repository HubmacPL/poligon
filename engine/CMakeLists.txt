find_package(SDL3 CONFIG REQUIRED)
find_package(Stb REQUIRED)
# Provides DIRECTX_DXC_TOOL, the HLSL compiler. Build-time tool only; nothing from it is linked or shipped.
find_package(directx-dxc CONFIG REQUIRED)

find_package(flecs CONFIG REQUIRED)

add_library(poligon_engine STATIC
  src/command_line.cpp
  src/crash_handler.cpp
  src/ecs.cpp
  src/state_dump.cpp
  src/fatal_error.cpp
  src/fixed_step_clock.cpp
  src/game_loop.cpp
  src/input.cpp
  src/math.cpp
  src/platform.cpp
  src/renderer.cpp
  src/stb_image_write_impl.cpp
  src/version.cpp
)
target_include_directories(poligon_engine PUBLIC include)
target_include_directories(poligon_engine SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
# dbghelp is a Windows system library: turns addresses into function names for crash reports.
target_link_libraries(poligon_engine PRIVATE poligon_build_options SDL3::SDL3 dbghelp)
# flecs is PUBLIC: game code uses the flecs API directly through poligon/ecs.h.
target_link_libraries(poligon_engine PUBLIC $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>)

# Compiles one HLSL file to DXIL (Direct3D 12) and SPIR-V (Vulkan) and embeds both in the
# engine as byte arrays, so there are no shader files to find at run time.
#   source: path relative to this directory, for example shaders/mesh.vert.hlsl
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
    COMMAND ${DIRECTX_DXC_TOOL} -nologo -WX -T ${profile} -E main -Fo ${dxil} ${input}
    COMMAND ${DIRECTX_DXC_TOOL} -nologo -WX -spirv -T ${profile} -E main -Fo ${spirv} ${input}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${dxil} -DOUTPUT=${output_dir}/${header}_dxil.h -DSYMBOL=${symbol}Dxil -P ${embed}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${spirv} -DOUTPUT=${output_dir}/${header}_spirv.h -DSYMBOL=${symbol}Spirv -P ${embed}
    DEPENDS ${input} ${embed}
    COMMENT "Compiling shader ${source}"
    VERBATIM)
  target_sources(poligon_engine PRIVATE ${output_dir}/${header}_dxil.h ${output_dir}/${header}_spirv.h)
endfunction()

target_include_directories(poligon_engine PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
poligon_embed_shader(shaders/mesh.vert.hlsl vertex kMeshVert mesh_vert)
poligon_embed_shader(shaders/mesh.frag.hlsl fragment kMeshFrag mesh_frag)
