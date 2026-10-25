# Core: what every program needs, below everything else (see engine/CMakeLists.txt).
add_library(poligon_core STATIC
  app/command_line.cpp
  app/fixed_step_clock.cpp
  app/game_loop.cpp
  app/version.cpp
  app/worker.cpp
  image/image.cpp
  image/stb_dxt_impl.cpp
  image/stb_image_impl.cpp
  image/stb_image_write_impl.cpp
  image/texture_compression.cpp
  input/input.cpp
  platform/crash_handler.cpp
  platform/fatal_error.cpp
  platform/platform.cpp
)
target_include_directories(poligon_core PUBLIC ${POLIGON_INCLUDE_DIR})
target_include_directories(poligon_core SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
# dbghelp is a Windows system library: turns addresses into function names for crash reports.
# ImGui: platform.cpp hands window events to the debug windows when they exist.
target_link_libraries(poligon_core PRIVATE poligon_build_options SDL3::SDL3 dbghelp imgui::imgui)
# GLM is PUBLIC: game code uses it directly (poligon/core/math.h). Its configuration must be
# identical in every file of a program, so it is set here and nowhere else:
#   DEPTH_ZERO_TO_ONE  projection matrices produce depth 0..1, as Direct3D 12 and Vulkan expect
#   CTOR_INIT          default-constructed vectors are zero and matrices identity, never garbage
#   XYZW_ONLY          vectors have plain x, y, z, w members (no aliases such as .r or .s)
target_link_libraries(poligon_core PUBLIC glm::glm)
target_compile_definitions(poligon_core PUBLIC
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  GLM_FORCE_CTOR_INIT
  GLM_FORCE_XYZW_ONLY)
