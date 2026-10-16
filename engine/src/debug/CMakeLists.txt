# Debug: the debug windows (Dear ImGui) and the free camera.
add_library(poligon_debug STATIC
  debug_ui.cpp
  free_camera.cpp
)
target_link_libraries(poligon_debug PUBLIC poligon_render poligon_ecs)
target_link_libraries(poligon_debug PRIVATE poligon_build_options)
