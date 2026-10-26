poligon_add_test(plant_rules_test)
poligon_add_test(particles_test)
poligon_add_test(footprints_test)
poligon_add_test(streaming_test)

add_executable(terrain_generator_test terrain_generator_test.cpp)
target_link_libraries(terrain_generator_test PRIVATE poligon_engine poligon_build_options)
add_test(NAME terrain_generator_test COMMAND terrain_generator_test ${CMAKE_BINARY_DIR}/test_output)

add_executable(height_map_test height_map_test.cpp)
target_link_libraries(height_map_test PRIVATE poligon_engine poligon_build_options)
add_test(NAME height_map_test COMMAND height_map_test ${CMAKE_BINARY_DIR}/test_output)

# Pixel-level checks on both graphics backends, with the debug layer on.
poligon_add_render_test(vegetation_render_test)
poligon_add_render_test(terrain_render_test)
poligon_add_render_test(ground_cover_test)
poligon_add_render_test(water_render_test)
poligon_add_render_test(terrain_streaming_test)

# The same scene at the origin and 28 km away must look the same (double world positions,
# camera-relative drawing). Small differences come from shadow texels snapped to a world grid;
# the tolerance is the one used between the two graphics backends.
poligon_add_render_test(far_from_origin_test ${CMAKE_BINARY_DIR}/test_output)
foreach(backend d3d12 vulkan)
  set_tests_properties(far_from_origin_test_${backend} PROPERTIES FIXTURES_SETUP far_from_origin_${backend})
  add_test(NAME far_from_origin_looks_the_same_${backend}
    COMMAND image_compare ${CMAKE_BINARY_DIR}/test_output/far_from_origin_${backend}_near.png
            ${CMAKE_BINARY_DIR}/test_output/far_from_origin_${backend}_far.png --channel-tolerance 12)
  set_tests_properties(far_from_origin_looks_the_same_${backend} PROPERTIES FIXTURES_REQUIRED far_from_origin_${backend})
endforeach()

add_executable(land_files_test land_files_test.cpp)
target_link_libraries(land_files_test PRIVATE poligon_engine poligon_build_options)
add_test(NAME land_files_test COMMAND land_files_test ${CMAKE_BINARY_DIR}/test_output)
