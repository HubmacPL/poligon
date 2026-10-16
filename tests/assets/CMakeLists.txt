# Assets: compression, the list, cooking, loading by name from a folder and an archive,
# plant variants and the level file; draws a cooked model on both backends.
poligon_add_render_test(assets_test ${CMAKE_BINARY_DIR}/test_output)

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
