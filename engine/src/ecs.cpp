#include "poligon/ecs.h"

#include "poligon/math.h"
#include "poligon/renderer.h"

namespace poligon {

void RegisterEngineTypes(flecs::world& world) {
    world.component<Vec3>().member<float>("x").member<float>("y").member<float>("z");
    world.component<Color>().member<float>("r").member<float>("g").member<float>("b").member<float>("a");
}

} // namespace poligon
