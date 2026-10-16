# Tests, in a folder per engine area (as in engine/src): tests/<area>/<name>.cpp, each area
# with its own CMakeLists.txt. tests/test_support.h is shared by all of them.
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
find_package(Stb REQUIRED)

# A test from <name>.cpp in the calling folder; passes when the program returns 0.
function(poligon_add_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE poligon_engine poligon_build_options)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# A test that renders: run once per graphics backend with the debug layer on, failing on
# any debug layer report. Arguments after the name are passed after the backend.
function(poligon_add_render_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE poligon_engine poligon_build_options)
  foreach(backend d3d12 vulkan)
    add_test(NAME ${name}_${backend} COMMAND ${name} ${backend} ${ARGN})
    set_tests_properties(${name}_${backend} PROPERTIES
      FAIL_REGULAR_EXPRESSION "D3D12 ERROR;D3D12 WARNING;D3D12 CORRUPTION;Validation Error;VUID-")
  endforeach()
endfunction()

add_subdirectory(core)
add_subdirectory(ecs)
add_subdirectory(render)
add_subdirectory(animation)
add_subdirectory(physics)
add_subdirectory(assets)
add_subdirectory(world)
