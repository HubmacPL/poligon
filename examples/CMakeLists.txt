# One small runnable example per engine feature. Every example is also a test:
# it is built and run on each check, so examples cannot silently rot.
# Arguments after the name are passed to the example when it runs as a test.
function(poligon_add_example name)
  add_executable(example_${name} ${name}/main.cpp)
  target_link_libraries(example_${name} PRIVATE poligon_engine poligon_build_options)
  add_test(NAME example_${name} COMMAND example_${name} ${ARGN})
endfunction()

poligon_add_example(engine_version)
poligon_add_example(command_line --headless --ticks 3)
poligon_add_example(fixed_step_clock)
poligon_add_example(game_loop)
poligon_add_example(crash_handler)
poligon_add_example(state_dump)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test_output)
poligon_add_example(renderer ${CMAKE_BINARY_DIR}/test_output/example_renderer.png)
poligon_add_example(environment ${CMAKE_BINARY_DIR}/test_output/example_environment.png)
poligon_add_example(texture ${CMAKE_BINARY_DIR}/test_output)
poligon_add_example(model ${CMAKE_BINARY_DIR}/test_output)
poligon_add_example(animation ${CMAKE_BINARY_DIR}/test_output/example_animation.png)
poligon_add_example(physics ${CMAKE_BINARY_DIR}/test_output/example_physics.png)
poligon_add_example(debug_ui --headless ${CMAKE_BINARY_DIR}/test_output/example_render_settings.txt
                    ${CMAKE_BINARY_DIR}/test_output/example_debug_ui.png)
poligon_add_example(input ${CMAKE_BINARY_DIR}/test_output/example_input_recording.txt)
