// Tests the toy game's simulation without a renderer.
#include <string>

#include "poligon/state_dump.h"
#include "test_support.h"
#include "toy_world.h"

namespace {

void RunTicks(flecs::world& world, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        world.progress(1.0f / 60.0f);
    }
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
    const float box_height = world.lookup("big_box").get<toy::Position>().value.y;
    Expect(box_height > 0.45f && box_height < 0.55f, "the big box has fallen and come to rest on the ground");
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
        const float x = player.get<toy::Position>().value.x;
        Expect(x > 3.85f && x < 4.1f, "one second of holding D moves the box 4 units to the right");
        Expect(player.get<toy::Velocity>().value.y > 5.0f, "pressing Space gives the box upward speed");
        Expect(input_world.get<toy::CameraRig>().yaw_degrees == 60.0f,
               "60 ticks of 4 pixels at 0.25 degrees per pixel turn the camera 60 degrees");
    }

    return test::ExitCode();
}
