#include "poligon/ecs/ecs.h"

#include "poligon/core/math.h"
#include "poligon/core/color.h"

namespace poligon {

void RegisterEngineTypes(flecs::world& world) {
    // GLM types get short, explicit names; their C++ template names are long and unreadable.
    world.component<glm::vec2>("vec2").member<float>("x").member<float>("y");
    world.component<glm::vec3>("vec3").member<float>("x").member<float>("y").member<float>("z");
    world.component<glm::dvec2>("dvec2").member<double>("x").member<double>("y");
    world.component<glm::dvec3>("dvec3").member<double>("x").member<double>("y").member<double>("z");
    world.component<glm::vec4>("vec4").member<float>("x").member<float>("y").member<float>("z").member<float>("w");
    world.component<glm::quat>("quat").member<float>("x").member<float>("y").member<float>("z").member<float>("w");
    world.component<Color>().member<float>("r").member<float>("g").member<float>("b").member<float>("a");
}

} // namespace poligon
