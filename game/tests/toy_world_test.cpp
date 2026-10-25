// Tests the toy game's simulation without a renderer.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "poligon/ecs/state_dump.h"
#include "test_support.h"
#include "toy_world.h"

namespace {

void RunTicks(flecs::world& world, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        world.progress(1.0f / 60.0f);
    }
}

struct Placed {
    glm::dvec3 position;
    glm::quat rotation;
    glm::vec3 velocity;
};

// Positions, rotations and speeds of every falling box, in the order of the entities.
std::vector<Placed> FallingBoxes(flecs::world& world) {
    std::vector<Placed> boxes;
    world.query_builder<const toy::Position, const toy::Rotation, const toy::Velocity>().with<toy::Falling>().build().each(
        [&](const toy::Position& position, const toy::Rotation& rotation, const toy::Velocity& velocity) {
            boxes.push_back({position.value, rotation.value, velocity.value});
        });
    return boxes;
}

} // namespace

int main() {
    using test::Expect;

    poligon::PhysicsWorld physics;
    flecs::world world;
    toy::SetUpToyWorld(world, physics);
    RunTicks(world, 600);

    const std::string dump = poligon::DumpWorldStateToJson(world);
    Expect(dump.find("\"unreflected_components\": []") != std::string::npos,
           "every toy component has its members registered");
    Expect(world.get<toy::ToyStats>().expired_count > 10, "spawned boxes expire, so entities are deleted");
    const double box_height = world.lookup("big_box").get<toy::Position>().value.y;
    Expect(box_height > 0.45 && box_height < 0.55, "the big box has fallen and come to rest on the ground");
    Expect(physics.GetBodyCount() < 20, "bodies of expired boxes are removed from the physics world");
    Expect(world.lookup("spawner").get<toy::Spawner>().spawned_count == 30, "one box is spawned every 20 ticks");

    // A second world given the same ticks must end in exactly the same state.
    poligon::PhysicsWorld second_physics;
    flecs::world second_world;
    toy::SetUpToyWorld(second_world, second_physics);
    RunTicks(second_world, 600);
    Expect(dump == poligon::DumpWorldStateToJson(second_world), "two worlds with the same ticks are identical");

    // And one more tick must change it, otherwise the comparison proves nothing.
    RunTicks(second_world, 1);
    Expect(dump != poligon::DumpWorldStateToJson(second_world), "one extra tick changes the state");

    // Input: holding D moves the player's box to the right, Space makes it jump, and the
    // right mouse button with mouse movement turns the camera.
    {
        poligon::PhysicsWorld input_physics;
        flecs::world input_world;
        toy::SetUpToyWorld(input_world, input_physics);
        poligon::Input input;
        const toy::ToyActions actions = toy::SetUpToyInput(input);
        input.UseManualDevices();

        poligon::InputDeviceState devices;
        devices.keys[static_cast<int>(poligon::Key::D)] = true;
        devices.mouse_buttons[static_cast<int>(poligon::MouseButton::Right)] = true;
        devices.mouse_motion_x = 4.0f;
        input.SetDeviceState(devices);
        for (int tick = 0; tick < 60; ++tick) {
            devices.keys[static_cast<int>(poligon::Key::Space)] = tick == 59;
            input.SetDeviceState(devices);
            input.AdvanceTick(tick);
            toy::UpdatePlayerInput(input_world, input, actions);
            input_world.progress(1.0f / 60.0f);
        }

        const flecs::entity player = input_world.lookup("big_box");
        const double x = player.get<toy::Position>().value.x;
        Expect(x > 3.85 && x < 4.1, "one second of holding D moves the box 4 units to the right");
        Expect(player.get<toy::Velocity>().value.y > 5.0f, "pressing Space gives the box upward speed");
        Expect(input_world.get<toy::CameraRig>().yaw_degrees == 60.0f,
               "60 ticks of 4 pixels at 0.25 degrees per pixel turn the camera 60 degrees");
    }

    // The same scene moved far from the origin plays out the same, only moved: after 300 ticks
    // every box is where the unmoved one is plus the shift (to 1e-6 m), turned and moving the same.
    {
        const glm::dvec3 shift(20000.0, 50.0, -16000.0);
        poligon::PhysicsWorld near_physics;
        flecs::world near_world;
        toy::SetUpToyWorld(near_world, near_physics);
        RunTicks(near_world, 300);
        poligon::PhysicsWorld far_physics;
        flecs::world far_world;
        toy::SetUpToyWorld(far_world, far_physics, shift);
        RunTicks(far_world, 300);
        const std::vector<Placed> near_boxes = FallingBoxes(near_world);
        const std::vector<Placed> far_boxes = FallingBoxes(far_world);
        Expect(near_boxes.size() == far_boxes.size() && near_boxes.size() > 5, "both worlds hold the same falling boxes");
        double worst_position = 0.0;
        float worst_rotation = 0.0f;
        float worst_velocity = 0.0f;
        for (size_t i = 0; i < near_boxes.size() && i < far_boxes.size(); ++i) {
            const glm::dvec3 difference = far_boxes[i].position - shift - near_boxes[i].position;
            worst_position = std::fmax(worst_position, std::fmax(std::fabs(difference.x), std::fmax(std::fabs(difference.y), std::fabs(difference.z))));
            worst_rotation = std::fmax(worst_rotation, 1.0f - std::fabs(glm::dot(far_boxes[i].rotation, near_boxes[i].rotation)));
            worst_velocity = std::fmax(worst_velocity, glm::length(far_boxes[i].velocity - near_boxes[i].velocity));
        }
        std::printf("scene moved by (20000, 50, -16000): worst position difference %.3g m, rotation %.3g, speed %.3g m/s\n",
                    worst_position, static_cast<double>(worst_rotation), static_cast<double>(worst_velocity));
        Expect(worst_position <= 1e-6, "a moved scene ends in the same places plus the shift, to 1e-6 m");
        Expect(worst_rotation <= 1e-6f && worst_velocity <= 1e-4f, "a moved scene's boxes are turned and moving the same");
    }

    return test::ExitCode();
}
