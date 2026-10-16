# Assets: model files (Assimp), the asset list, cooking, archives, loading by name.
add_library(poligon_assets STATIC
  assets.cpp
  model.cpp
)
target_link_libraries(poligon_assets PUBLIC poligon_render poligon_animation)
# Assimp reads 3D model files in many formats. Its types stay inside model.cpp.
target_link_libraries(poligon_assets PRIVATE poligon_build_options assimp::assimp)
