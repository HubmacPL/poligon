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

option(POLIGON_ASAN "Build everything with AddressSanitizer" OFF)
option(POLIGON_ANALYZE "Run MSVC static analysis (/analyze) on Poligon targets" OFF)

include(cmake/PoligonBuildOptions.cmake)

enable_testing()

# Dependency direction: game -> engine. The engine must never include game code.
add_subdirectory(engine)
add_subdirectory(game)
add_subdirectory(examples)
add_subdirectory(tools/image_compare)
add_subdirectory(tools/state_compare)
add_subdirectory(tools/model_viewer)
add_subdirectory(tools/image_tool)
add_subdirectory(tools/forest_scene)
add_subdirectory(tests)
