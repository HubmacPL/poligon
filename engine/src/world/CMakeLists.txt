# World: terrain and its generator, vegetation and plant rules, water, the level file.
add_library(poligon_world STATIC
  footprints/footprints.cpp
  level/level.cpp
  particles/particles.cpp
  streaming/streaming.cpp
  terrain/height_map.cpp
  terrain/sky_occlusion.cpp
  terrain/terrain.cpp
  terrain/generator/fine_scale.cpp
  terrain/generator/large_scale.cpp
  terrain/generator/maps.cpp
  terrain/generator/terrain_generator.cpp
  vegetation/ground_cover.cpp
  vegetation/plant_rules.cpp
  vegetation/plants.cpp
  vegetation/procedural_grass.cpp
  vegetation/procedural_plants.cpp
  vegetation/trample.cpp
  vegetation/vegetation.cpp
  water/water.cpp
)
target_link_libraries(poligon_world PUBLIC poligon_assets poligon_physics poligon_render)
target_link_libraries(poligon_world PRIVATE poligon_build_options)
