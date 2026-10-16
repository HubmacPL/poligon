# Physics: bodies, characters and rays on Jolt Physics.
add_library(poligon_physics STATIC
  physics.cpp
)
target_link_libraries(poligon_physics PUBLIC poligon_core)
# Jolt Physics. Its types stay inside physics.cpp.
target_link_libraries(poligon_physics PRIVATE poligon_build_options Jolt::Jolt)
