# ECS: the flecs world's setup, the state dump and the state hash.
add_library(poligon_ecs STATIC
  ecs.cpp
  state_dump.cpp
)
target_link_libraries(poligon_ecs PUBLIC poligon_core)
target_link_libraries(poligon_ecs PRIVATE poligon_build_options)
# flecs is PUBLIC: game code uses the flecs API directly through poligon/ecs/ecs.h.
target_link_libraries(poligon_ecs PUBLIC $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>)
