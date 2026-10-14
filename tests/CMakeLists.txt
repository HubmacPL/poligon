function(poligon_add_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE poligon_engine poligon_build_options)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

poligon_add_test(engine_version_test)
poligon_add_test(command_line_test)
poligon_add_test(fixed_step_clock_test)
poligon_add_test(game_loop_test)
poligon_add_test(math_test)
poligon_add_test(physics_test)

find_package(Stb REQUIRED)
add_executable(image_test image_test.cpp)
target_include_directories(image_test SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(image_test PRIVATE poligon_engine poligon_build_options)
add_test(NAME image_test COMMAND image_test ${CMAKE_BINARY_DIR}/test_output)

add_executable(render_settings_test render_settings_test.cpp)
target_link_libraries(render_settings_test PRIVATE poligon_engine poligon_build_options)
add_test(NAME render_settings_test COMMAND render_settings_test ${CMAKE_BINARY_DIR}/test_output)

# Pixel-level checks of textured rendering, on both graphics backends, with the debug layer on.
foreach(render_test texture_render_test scene_render_test effects_render_test skinning_render_test)
  add_executable(${render_test} ${render_test}.cpp)
  target_link_libraries(${render_test} PRIVATE poligon_engine poligon_build_options)
  foreach(backend d3d12 vulkan)
    add_test(NAME ${render_test}_${backend} COMMAND ${render_test} ${backend})
    set_tests_properties(${render_test}_${backend} PROPERTIES
      FAIL_REGULAR_EXPRESSION "D3D12 ERROR;D3D12 WARNING;D3D12 CORRUPTION;Validation Error;VUID-")
  endforeach()
endforeach()

# Model loading: the test writes its own small OBJ and glTF files, then the viewer draws one
# of them on both backends with the debug layer on.
add_executable(model_test model_test.cpp)
target_link_libraries(model_test PRIVATE poligon_engine poligon_build_options)
add_test(NAME model_test COMMAND model_test ${CMAKE_BINARY_DIR}/test_output)
set_tests_properties(model_test PROPERTIES FIXTURES_SETUP test_models)
foreach(backend d3d12 vulkan)
  add_test(NAME model_viewer_draws_${backend}
    COMMAND model_viewer ${CMAKE_BINARY_DIR}/test_output/model_test_quad.obj --headless --backend ${backend}
            --gpu-validation --dump-frame ${CMAKE_BINARY_DIR}/test_output/model_viewer_${backend}.png)
  set_tests_properties(model_viewer_draws_${backend} PROPERTIES
    FIXTURES_REQUIRED test_models
    PASS_REGULAR_EXPRESSION "triangles=2.*frame saved to"
    FAIL_REGULAR_EXPRESSION "D3D12 ERROR;D3D12 WARNING;D3D12 CORRUPTION;Validation Error;VUID-")
endforeach()

add_executable(input_test input_test.cpp)
target_link_libraries(input_test PRIVATE poligon_engine poligon_build_options)
add_test(NAME input_test COMMAND input_test ${CMAKE_BINARY_DIR}/test_output)

add_executable(state_dump_test state_dump_test.cpp)
target_link_libraries(state_dump_test PRIVATE poligon_engine poligon_build_options)
add_test(NAME state_dump_test COMMAND state_dump_test ${CMAKE_BINARY_DIR}/test_output/state_dump_test.json)

# flecs repeatability: once inside one process (the program's own exit code)
# and once between two processes (identical printed state and visiting order).
poligon_add_test(ecs_order_probe)
add_test(NAME ecs_order_probe_same_between_processes
  COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:ecs_order_probe>
    -DARGUMENTS=
    -P ${PROJECT_SOURCE_DIR}/cmake/CompareTwoRuns.cmake)

# Crash reports: each test crashes on purpose and passes only when the report names what
# happened and the function it happened in. The timeout catches a dialog box waiting for a click.
add_executable(crash_test crash_test.cpp)
target_link_libraries(crash_test PRIVATE poligon_engine poligon_build_options)
function(poligon_add_crash_test mode expected)
  add_test(NAME crash_report_${mode}
    COMMAND crash_test ${mode} ${CMAKE_BINARY_DIR}/test_output/crash_${mode}.txt)
  set_tests_properties(crash_report_${mode} PROPERTIES PASS_REGULAR_EXPRESSION "${expected}" TIMEOUT 30)
endfunction()

poligon_add_crash_test(abort "CRASH: abort\\(\\) was called.*CrashWithAbort")
poligon_add_crash_test(fatal_error "FATAL: deliberate fatal error.*CrashWithFatalError")
poligon_add_crash_test(exception "CRASH: uncaught C\\+\\+ exception: deliberate exception")
poligon_add_crash_test(pure_call "CRASH: pure virtual function call.*CrashWithPureCall")
if(POLIGON_ASAN)
  # The sanitizer reports memory and CPU faults itself, in its own format.
  poligon_add_crash_test(null_write "AddressSanitizer: access-violation.*CrashWithNullWrite")
else()
  poligon_add_crash_test(null_write "CRASH: access violation writing address 0x0000000000000000.*CrashWithNullWrite")
  poligon_add_crash_test(division "CRASH: integer division by zero.*CrashWithDivision")
  poligon_add_crash_test(stack_overflow "CRASH: stack overflow.*CrashWithStackOverflow")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # Only the debug C++ library checks vector indices.
  poligon_add_crash_test(vector_index
    "CRASH: debug runtime check failed.*vector subscript out of range.*CrashWithVectorIndex")
endif()

poligon_add_test(fatal_error_test)
set_tests_properties(fatal_error_test PROPERTIES
  PASS_REGULAR_EXPRESSION "FATAL: GameLoop: ticks_per_second must be at least 1")

# Canary: proves the sanitizer build really detects memory errors.
# It passes only when AddressSanitizer prints its heap-buffer-overflow report.
if(POLIGON_ASAN)
  add_executable(asan_canary asan_canary.cpp)
  target_link_libraries(asan_canary PRIVATE poligon_build_options)
  add_test(NAME asan_canary_detects_overflow COMMAND asan_canary)
  set_tests_properties(asan_canary_detects_overflow PROPERTIES
    PASS_REGULAR_EXPRESSION "AddressSanitizer: heap-buffer-overflow")
endif()
