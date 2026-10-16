# Animation, in two libraries:
#   poligon_animation        skeletons, poses, playing and fading clips, reading and writing
#                            archives (ozz-animation's runtime part); what a game needs
#   poligon_animation_build  building sets from clips (ozz-animation's offline part); for the
#                            asset cooker, tools and examples that read model files
add_library(poligon_animation STATIC
  runtime/animation.cpp
)
target_link_libraries(poligon_animation PUBLIC poligon_core)
# ozz-animation's types stay inside the animation sources.
target_link_libraries(poligon_animation PRIVATE poligon_build_options ozz::animation ozz::base)

add_library(poligon_animation_build STATIC
  build/animation_build.cpp
)
target_link_libraries(poligon_animation_build PUBLIC poligon_animation)
target_link_libraries(poligon_animation_build PRIVATE poligon_build_options ozz::animation_offline ozz::animation ozz::base)
