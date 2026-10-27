cmake_minimum_required(VERSION 3.28)

project(poligon VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Poligon supports only MSVC (Visual Studio 2022) targeting x64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++20 modules in this project; skipping the scan keeps builds faster and logs shorter.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# All programs and DLLs land in one directory: build/<preset>/bin.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# What is built besides the engine. On by default when Poligon is the project being built,
# off when a game adds it with add_subdirectory (then only the engine's libraries are made).
option(POLIGON_BUILD_TESTS "Build and register Poligon's tests" ${PROJECT_IS_TOP_LEVEL})
option(POLIGON_BUILD_EXAMPLES "Build and register Poligon's examples" ${PROJECT_IS_TOP_LEVEL})
option(POLIGON_BUILD_TOOLS "Build Poligon's tools (model_viewer, forest_scene, asset_cooker...)" ${PROJECT_IS_TOP_LEVEL})
option(POLIGON_BUILD_TOY "Build the toy game and its tests" ${PROJECT_IS_TOP_LEVEL})
option(POLIGON_ASAN "Build everything with AddressSanitizer" OFF)
option(POLIGON_ANALYZE "Run MSVC static analysis (/analyze) on Poligon targets" OFF)

include(cmake/PoligonBuildOptions.cmake)

if(POLIGON_BUILD_TESTS OR POLIGON_BUILD_EXAMPLES OR POLIGON_BUILD_TOY)
  enable_testing()
endif()

# Dependency direction: game -> engine. The engine must never include game code.
add_subdirectory(engine)
# The image and state comparers are what the tests and the toy's checks run.
if(POLIGON_BUILD_TOOLS OR POLIGON_BUILD_TESTS OR POLIGON_BUILD_TOY)
  add_subdirectory(tools/image_compare)
  add_subdirectory(tools/state_compare)
endif()
if(POLIGON_BUILD_TOY)
  add_subdirectory(game)
endif()
if(POLIGON_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(POLIGON_BUILD_TOOLS)
  add_subdirectory(tools/model_viewer)
  add_subdirectory(tools/image_tool)
  add_subdirectory(tools/forest_scene)
  add_subdirectory(tools/terrain_tool)
  add_subdirectory(tools/impostor_baker)
  add_subdirectory(tools/asset_cooker)
endif()
if(POLIGON_BUILD_TESTS)
  add_subdirectory(tests)
endif()
