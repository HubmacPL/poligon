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
