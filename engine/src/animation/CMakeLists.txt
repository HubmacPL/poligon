# Animation: skeletons, clips and players on ozz-animation.
add_library(poligon_animation STATIC
  animation.cpp
)
target_link_libraries(poligon_animation PUBLIC poligon_core)
# ozz-animation samples and blends clips. Its types stay inside animation.cpp.
target_link_libraries(poligon_animation PRIVATE poligon_build_options ozz::animation_offline ozz::animation ozz::base)
