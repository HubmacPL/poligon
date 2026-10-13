find_package(Stb REQUIRED)

add_executable(image_compare main.cpp)
target_include_directories(image_compare SYSTEM PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(image_compare PRIVATE poligon_build_options)
