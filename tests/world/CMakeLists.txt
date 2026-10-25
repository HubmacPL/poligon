poligon_add_test(plant_rules_test)
poligon_add_test(particles_test)

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
