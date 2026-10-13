# The simulation is a library so that tests can run it without a renderer.
add_library(toy_world STATIC
  src/toy_world.cpp
)
target_include_directories(toy_world PUBLIC src)
target_link_libraries(toy_world PUBLIC poligon_engine PRIVATE poligon_build_options)

add_executable(toy
  src/main.cpp
)
target_link_libraries(toy PRIVATE toy_world poligon_build_options)

add_executable(toy_world_test tests/toy_world_test.cpp)
target_include_directories(toy_world_test PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(toy_world_test PRIVATE toy_world poligon_build_options)
add_test(NAME toy_world_test COMMAND toy_world_test)

# Image comparisons on one backend use the strict defaults of image_compare. Between Direct3D 12
# and Vulkan a wider channel tolerance is needed: the two drivers implement anisotropic texture
# filtering slightly differently (measured: up to 32 of 255 on a few hundred edge pixels).
# A real error, such as a moved object or a wrong texture, still changes far more pixels.
set(toy_cross_backend_tolerance --channel-tolerance 12)
set(toy_tolerance_d3d12 "")  # the reference image was rendered with Direct3D 12
set(toy_tolerance_vulkan ${toy_cross_backend_tolerance})

set(toy_output_dir ${CMAKE_BINARY_DIR}/test_output)
file(MAKE_DIRECTORY ${toy_output_dir})

add_test(NAME toy_headless_runs
  COMMAND toy --headless --ticks 600 --dump-state ${toy_output_dir}/toy_state_600.json)
set_tests_properties(toy_headless_runs PROPERTIES PASS_REGULAR_EXPRESSION "ticks=600 frames=600")

# Two headless runs with the same arguments must print exactly the same final state hash.
add_test(NAME toy_headless_is_repeatable
  COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:toy>
    "-DARGUMENTS=--headless --ticks 600"
    -P ${PROJECT_SOURCE_DIR}/cmake/CompareTwoRuns.cmake)

# --- Determinism test -------------------------------------------------------------------
# The same recording is replayed in separate processes. Runs "a" and "b" differ only in the
# graphics backend, which must not influence the simulation. Requirements:
#   - hard: identical state dump (JSON) and identical state hash on every tick;
#   - with tolerance: matching final frame (PNG).
# Run "late" replays a recording whose second jump is one tick later; the comparison with it
# must FAIL and name tick 151, which proves the test can see a difference and locate it.
set(toy_recording ${CMAKE_CURRENT_SOURCE_DIR}/tests/recordings/walk_jump_look.txt)
set(toy_recording_late ${CMAKE_CURRENT_SOURCE_DIR}/tests/recordings/walk_jump_look_late_jump.txt)

function(toy_add_replay_run name recording backend)
  add_test(NAME toy_replay_run_${name}
    COMMAND toy --headless --replay ${recording} --backend ${backend} --gpu-validation
            --dump-state ${toy_output_dir}/replay_${name}.json
            --hash-log ${toy_output_dir}/replay_${name}.hashes --hash-interval 1
            --dump-frame ${toy_output_dir}/replay_${name}.png)
  set_tests_properties(toy_replay_run_${name} PROPERTIES
    PASS_REGULAR_EXPRESSION "ticks=480 frames=480"
    FAIL_REGULAR_EXPRESSION "D3D12 ERROR;D3D12 WARNING;D3D12 CORRUPTION;Validation Error;VUID-"
    FIXTURES_SETUP toy_replay_${name})
endfunction()

toy_add_replay_run(a ${toy_recording} d3d12)
toy_add_replay_run(b ${toy_recording} vulkan)
toy_add_replay_run(late ${toy_recording_late} d3d12)

add_test(NAME toy_replay_state_is_identical
  COMMAND state_compare ${toy_output_dir}/replay_a.json ${toy_output_dir}/replay_b.json
          --hash-logs ${toy_output_dir}/replay_a.hashes ${toy_output_dir}/replay_b.hashes)
set_tests_properties(toy_replay_state_is_identical PROPERTIES FIXTURES_REQUIRED "toy_replay_a;toy_replay_b")

add_test(NAME toy_replay_frame_matches
  COMMAND image_compare ${toy_output_dir}/replay_a.png ${toy_output_dir}/replay_b.png ${toy_cross_backend_tolerance})
set_tests_properties(toy_replay_frame_matches PROPERTIES FIXTURES_REQUIRED "toy_replay_a;toy_replay_b")

add_test(NAME toy_replay_difference_is_detected
  COMMAND state_compare ${toy_output_dir}/replay_a.json ${toy_output_dir}/replay_late.json
          --hash-logs ${toy_output_dir}/replay_a.hashes ${toy_output_dir}/replay_late.hashes)
set_tests_properties(toy_replay_difference_is_detected PROPERTIES
  FIXTURES_REQUIRED "toy_replay_a;toy_replay_late"
  # Passes only when the tool reports the divergence at exactly the right place.
  PASS_REGULAR_EXPRESSION "Last identical state: after tick 150\\.[^A-Z]*First different state: after tick 151\\.")

add_test(NAME toy_rejects_missing_recording COMMAND toy --headless --replay ${toy_output_dir}/no_such_file.txt)
set_tests_properties(toy_rejects_missing_recording PROPERTIES PASS_REGULAR_EXPRESSION "cannot open")

add_test(NAME toy_rejects_unknown_option COMMAND toy --headles --ticks 5)
set_tests_properties(toy_rejects_unknown_option PROPERTIES PASS_REGULAR_EXPRESSION "unknown option")

# Frame dumps: render tick 45 on each backend with the debug layer on, then compare the PNG
# with the reference image. To update the reference after an intended visual change, copy the
# new dump over tests/reference/toy_tick_45.png and look at it first.
foreach(backend d3d12 vulkan)
  add_test(NAME toy_dumps_frame_${backend}
    COMMAND toy --headless --ticks 45 --backend ${backend} --gpu-validation
            --dump-frame ${toy_output_dir}/toy_tick_45_${backend}.png)
  set_tests_properties(toy_dumps_frame_${backend} PROPERTIES
    PASS_REGULAR_EXPRESSION "frame saved to"
    # Any message from the graphics debug layer fails the test.
    FAIL_REGULAR_EXPRESSION "D3D12 ERROR;D3D12 WARNING;D3D12 CORRUPTION;Validation Error;VUID-"
    FIXTURES_SETUP toy_frame_${backend})

  add_test(NAME toy_frame_matches_reference_${backend}
    COMMAND image_compare ${toy_output_dir}/toy_tick_45_${backend}.png
            ${PROJECT_SOURCE_DIR}/tests/reference/toy_tick_45.png ${toy_tolerance_${backend}})
  set_tests_properties(toy_frame_matches_reference_${backend} PROPERTIES
    FIXTURES_REQUIRED toy_frame_${backend})
endforeach()

# Both backends are expected to agree. Small differences are allowed; a wrong shader on one side is not.
add_test(NAME toy_frame_same_on_both_backends
  COMMAND image_compare ${toy_output_dir}/toy_tick_45_d3d12.png ${toy_output_dir}/toy_tick_45_vulkan.png
          ${toy_cross_backend_tolerance})
set_tests_properties(toy_frame_same_on_both_backends PROPERTIES
  FIXTURES_REQUIRED "toy_frame_d3d12;toy_frame_vulkan")
