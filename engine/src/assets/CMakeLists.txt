# Assets, in two libraries:
#   poligon_assets       loading cooked assets by name (from a folder or an archive), model
#                        data on the graphics card; what a game needs
#   poligon_assets_cook  reading model files (Assimp), the asset list, cooking and packing;
#                        for the cooker, the viewer and tools
add_library(poligon_assets STATIC
  runtime/asset_format.cpp
  runtime/asset_library.cpp
  runtime/render_model.cpp
)
target_link_libraries(poligon_assets PUBLIC poligon_render poligon_animation)
target_link_libraries(poligon_assets PRIVATE poligon_build_options)

add_library(poligon_assets_cook STATIC
  cook/cooking.cpp
  cook/model_import.cpp
)
target_link_libraries(poligon_assets_cook PUBLIC poligon_assets poligon_animation_build)
# Assimp reads 3D model files in many formats. Its types stay inside model_import.cpp.
target_link_libraries(poligon_assets_cook PRIVATE poligon_build_options assimp::assimp)
