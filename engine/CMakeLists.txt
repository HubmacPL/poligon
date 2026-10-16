# The engine is a set of libraries, one per area, each in its own folder under src/ with its
# public headers under include/poligon/<area>/. A library may use only those below it:
#
#   core       game loop, clock, command line, platform, errors and crash reports, input, images
#   ecs        flecs world setup, state dump and hash                            (core)
#   render     the renderer, its shaders, graphics presets, render settings      (core)
#   animation  skeletons and clips on ozz-animation                              (core)
#              + animation_build: building sets from clips (tool side)
#   physics    bodies, characters and rays on Jolt Physics                       (core)
#   assets     loading cooked assets by name, models on the card                  (render, animation)
#              + assets_cook: model files, the asset list, cooking (tool side)
#   world      terrain, vegetation, water, the level file                         (assets, physics, render)
#   debug      debug windows and the free camera                                  (render, ecs)
#
# The tool-side libraries (animation_build, assets_cook) are what a released game can leave
# out: it loads cooked assets only. poligon_runtime links the game-side libraries (the toy
# game uses it, which proves they link without the tool side); poligon_engine links them
# all, for tools, tests and examples.

find_package(SDL3 CONFIG REQUIRED)
find_package(Stb REQUIRED)
# Provides DIRECTX_DXC_TOOL, the HLSL compiler. Build-time tool only; nothing from it is linked or shipped.
find_package(directx-dxc CONFIG REQUIRED)
find_package(flecs CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(ozz-animation CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)

set(POLIGON_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(src/core)
add_subdirectory(src/ecs)
add_subdirectory(src/render)
add_subdirectory(src/animation)
add_subdirectory(src/physics)
add_subdirectory(src/assets)
add_subdirectory(src/world)
add_subdirectory(src/debug)

add_library(poligon_runtime INTERFACE)
target_link_libraries(poligon_runtime INTERFACE poligon_core poligon_ecs poligon_render poligon_animation poligon_physics
                      poligon_assets poligon_world poligon_debug)
add_library(poligon_engine INTERFACE)
target_link_libraries(poligon_engine INTERFACE poligon_runtime poligon_animation_build poligon_assets_cook)
