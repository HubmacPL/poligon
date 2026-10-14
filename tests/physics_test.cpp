// Checks the physics world: bodies fall and come to rest, bounce, stack, are hit by rays;
// a character walks, is stopped by obstacles, steps onto ledges and jumps; and a run is
// repeatable bit for bit.
#include <cmath>
#include <cstdio>
#include <vector>

#include "poligon/physics.h"
#include "test_support.h"

namespace {

constexpr float kTick = 1.0f / 60.0f;

void StepFor(poligon::PhysicsWorld& world, float seconds) {
    const int ticks = static_cast<int>(std::lround(seconds / kTick));
    for (int i = 0; i < ticks; ++i) {
        world.Step(kTick);
    }
}

bool Near(float value, float expected, float tolerance) {
    const bool near = std::fabs(value - expected) <= tolerance;
    if (!near) {
        std::printf("  got %.4f, expected %.4f +- %.4f\n", static_cast<double>(value), static_cast<double>(expected),
                    static_cast<double>(tolerance));
    }
    return near;
}

// A floor whose top is at y = 0.
poligon::BodyHandle AddFloor(poligon::PhysicsWorld& world) {
    poligon::BodySettings floor;
    floor.position = {0.0f, -0.5f, 0.0f};
    return world.AddBox({50.0f, 0.5f, 50.0f}, floor);
}

poligon::BodySettings DynamicAt(glm::vec3 position) {
    poligon::BodySettings settings;
    settings.position = position;
    settings.dynamic = true;
    return settings;
}

// Builds a small scene and pushes things around in it; used twice to check repeatability.
std::vector<std::uint64_t> RunScript(bool extra_push) {
    poligon::PhysicsWorld world;
    AddFloor(world);
    std::vector<poligon::BodyHandle> boxes;
    for (int i = 0; i < 6; ++i) {
        boxes.push_back(world.AddBox({0.5f, 0.5f, 0.5f}, DynamicAt({0.1f * static_cast<float>(i), 0.5f + 1.05f * static_cast<float>(i), 0.0f})));
    }
    const poligon::BodyHandle ball = world.AddSphere(0.4f, DynamicAt({-6.0f, 0.4f, 0.0f}));
    poligon::CharacterSettings character_settings;
    character_settings.position = {4.0f, 0.0f, 3.0f};
    const poligon::CharacterHandle walker = world.AddCharacter(character_settings);

    std::vector<std::uint64_t> hashes;
    for (int tick = 0; tick < 300; ++tick) {
        if (tick == 30) {
            world.SetVelocity(ball, {9.0f, 0.0f, 0.0f});  // roll the ball into the tower
        }
        if (tick == 90 && extra_push) {
            world.AddImpulse(boxes[2], {0.0f, 0.0f, 0.001f});
        }
        world.SetCharacterInput(walker, {-1.5f, 0.0f, -1.0f}, tick == 120);
        world.Step(kTick);
        hashes.push_back(world.HashState());
    }
    return hashes;
}

} // namespace

int main() {
    using test::Expect;

    // --- Falling and resting ---
    {
        poligon::PhysicsWorld world;
        const poligon::BodyHandle floor = AddFloor(world);
        const poligon::BodyHandle box = world.AddBox({0.5f, 0.25f, 0.5f}, DynamicAt({0.0f, 5.0f, 0.0f}));
        Expect(world.GetBodyCount() == 2, "two bodies were added");

        StepFor(world, 0.5f);
        // Free fall: 0.5 * 9.81 * 0.5^2 = 1.23 m, at 4.9 m/s.
        Expect(Near(world.GetPosition(box).y, 5.0f - 1.23f, 0.06f), "after half a second a body has fallen 1.23 m");
        Expect(Near(world.GetVelocity(box).y, -4.9f, 0.1f), "and moves at 4.9 m/s");

        StepFor(world, 3.0f);
        Expect(Near(world.GetPosition(box).y, 0.25f, 0.02f), "the box comes to rest on the floor");
        Expect(world.IsAsleep(box), "a body at rest falls asleep");
        Expect(Near(world.GetPosition(floor).y, -0.5f, 0.0f), "the floor has not moved");

        const glm::mat4 transform = world.GetTransform(box);
        Expect(Near(transform[3].y, world.GetPosition(box).y, 0.0f), "GetTransform carries the position");

        glm::vec3 bounds_min;
        glm::vec3 bounds_max;
        world.GetBounds(box, bounds_min, bounds_max);
        Expect(Near(bounds_min.y, 0.0f, 0.02f) && Near(bounds_max.y, 0.5f, 0.02f) && Near(bounds_max.x, 0.5f, 0.02f),
               "the bounds enclose the box where it lies");
        Expect(world.IsDynamic(box) && !world.IsDynamic(floor), "bodies know whether they move");

        world.RemoveBody(box);
        Expect(world.GetBodyCount() == 1, "a removed body is gone");
        Expect(world.GetLastBodyId() == 2 && !world.IsBody(box) && world.IsBody(floor), "its handle number is not reused");
    }

    // --- Bouncing ---
    {
        poligon::PhysicsWorld world;
        // Of two bodies that meet, the bouncier one decides; the floor has no bounce.
        AddFloor(world);
        poligon::BodySettings bouncy = DynamicAt({0.0f, 3.0f, 0.0f});
        bouncy.bounciness = 0.8f;
        const poligon::BodyHandle ball = world.AddSphere(0.25f, bouncy);
        const poligon::BodyHandle lump = world.AddSphere(0.25f, DynamicAt({3.0f, 3.0f, 0.0f}));

        float ball_top = 0.0f;
        float lump_top = 0.0f;
        bool landed = false;
        for (int tick = 0; tick < 150; ++tick) {
            world.Step(kTick);
            landed = landed || world.GetPosition(lump).y < 0.3f;
            if (landed) {
                ball_top = std::fmax(ball_top, world.GetPosition(ball).y);
                lump_top = std::fmax(lump_top, world.GetPosition(lump).y);
            }
        }
        Expect(ball_top > 1.0f, "a bouncy ball jumps back up");
        Expect(lump_top < 0.35f, "a ball without bounce stays down");
    }

    // --- Pushing ---
    {
        poligon::PhysicsConfig no_gravity;
        no_gravity.gravity = {0.0f, 0.0f, 0.0f};
        poligon::PhysicsWorld world(no_gravity);
        poligon::BodySettings settings = DynamicAt({0.0f, 0.0f, 0.0f});
        settings.mass = 10.0f;
        const poligon::BodyHandle body = world.AddSphere(0.5f, settings);
        world.AddImpulse(body, {20.0f, 0.0f, 0.0f});
        world.Step(kTick);
        Expect(Near(world.GetVelocity(body).x, 2.0f, 0.01f), "an impulse of 20 on 10 kg gives 2 m/s");
        world.SetVelocity(body, {0.0f, 3.0f, 0.0f});
        StepFor(world, 1.0f);
        Expect(Near(world.GetPosition(body).y, 3.0f, 0.2f), "without gravity a body keeps moving as pushed");
        world.SetPosition(body, {7.0f, 8.0f, 9.0f}, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        Expect(Near(world.GetPosition(body).x, 7.0f, 0.0f) && Near(world.GetPosition(body).z, 9.0f, 0.0f),
               "SetPosition moves a body at once");
    }

    // --- A stack stays standing ---
    {
        poligon::PhysicsWorld world;
        AddFloor(world);
        std::vector<poligon::BodyHandle> boxes;
        for (int i = 0; i < 5; ++i) {
            boxes.push_back(world.AddBox({0.5f, 0.5f, 0.5f}, DynamicAt({0.0f, 0.5f + static_cast<float>(i), 0.0f})));
        }
        StepFor(world, 5.0f);
        const glm::vec3 top = world.GetPosition(boxes[4]);
        Expect(Near(top.y, 4.5f, 0.05f) && Near(top.x, 0.0f, 0.05f) && Near(top.z, 0.0f, 0.05f),
               "five stacked boxes stay stacked");
    }

    // --- Rays ---
    {
        poligon::PhysicsWorld world;
        const poligon::BodyHandle floor = AddFloor(world);
        poligon::BodySettings crate_settings;
        crate_settings.position = {3.0f, 1.0f, 0.0f};
        const poligon::BodyHandle crate = world.AddBox({1.0f, 1.0f, 1.0f}, crate_settings);

        poligon::RayHit hit = world.CastRay({0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f);
        Expect(hit.hit && hit.body.id == floor.id && Near(hit.distance, 5.0f, 0.01f), "a ray straight down hits the floor 5 m below");
        Expect(Near(hit.normal.y, 1.0f, 0.01f) && Near(hit.position.y, 0.0f, 0.01f), "the hit reports where and which way the surface faces");

        hit = world.CastRay({0.0f, 1.0f, 0.0f}, {5.0f, 0.0f, 0.0f}, 100.0f);
        Expect(hit.hit && hit.body.id == crate.id && Near(hit.distance, 2.0f, 0.01f) && Near(hit.normal.x, -1.0f, 0.01f),
               "a level ray hits the side of the crate; the direction need not be normalized");

        Expect(!world.CastRay({0.0f, 5.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 100.0f).hit, "a ray into the sky hits nothing");
        Expect(!world.CastRay({0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 2.0f).hit, "a ray that is too short hits nothing");
    }

    // --- Ground made of triangles ---
    {
        poligon::PhysicsWorld world;
        // A square sloping up towards +x: y = 0.2 * x.
        const glm::vec3 vertices[4] = {{-10.0f, -2.0f, 10.0f}, {10.0f, 2.0f, 10.0f}, {10.0f, 2.0f, -10.0f}, {-10.0f, -2.0f, -10.0f}};
        const std::uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
        world.AddMesh(vertices, 4, indices, 6, poligon::BodySettings{});
        const poligon::RayHit hit = world.CastRay({5.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 100.0f);
        Expect(hit.hit && Near(hit.position.y, 1.0f, 0.01f), "a ray finds the height of sloping ground");

        poligon::BodySettings settings = DynamicAt({5.0f, 3.0f, 0.0f});
        settings.friction = 1.0f;
        const poligon::BodyHandle box = world.AddBox({0.3f, 0.3f, 0.3f}, settings);
        StepFor(world, 3.0f);
        const glm::vec3 position = world.GetPosition(box);
        Expect(position.y > 0.2f * position.x && position.y < 0.2f * position.x + 0.5f, "a box lands on the slope and stays on it");
    }

    // --- Land from a height map ---
    {
        poligon::PhysicsWorld world;
        // A slope rising 0.25 m per meter along x, 33 x 33 points, 2 m apart, placed so that
        // its first point is at (-10, 5, -10).
        const int size = 33;
        std::vector<float> heights;
        for (int iz = 0; iz < size; ++iz) {
            for (int ix = 0; ix < size; ++ix) {
                heights.push_back(static_cast<float>(ix) * 0.5f);
            }
        }
        const poligon::BodyHandle land = world.AddHeightField(heights.data(), size, 2.0f, {-10.0f, 5.0f, -10.0f});
        poligon::RayHit hit = world.CastRay({10.0f, 100.0f, 3.0f}, {0.0f, -1.0f, 0.0f}, 200.0f);
        // 20 m from the first point: 10 points along, height 5 m, plus the origin's 5 m.
        Expect(hit.hit && hit.body.id == land.id && Near(hit.position.y, 10.0f, 0.02f), "a ray finds the height of land from a height map");
        Expect(hit.normal.x < -0.2f && hit.normal.y > 0.9f, "and which way it slopes");

        poligon::CharacterSettings settings;
        settings.position = {10.0f, 10.5f, 3.0f};
        const poligon::CharacterHandle character = world.AddCharacter(settings);
        for (int tick = 0; tick < 180; ++tick) {
            world.SetCharacterInput(character, {2.0f, 0.0f, 0.0f}, false);
            world.Step(kTick);
        }
        const glm::vec3 position = world.GetCharacterPosition(character);
        // Walked about 6 m uphill: the land there is 1.5 m higher.
        Expect(position.x > 15.0f && Near(position.y, 5.0f + (position.x + 10.0f) * 0.25f, 0.1f),
               "a character walks up the land and stays on it");
    }

    // --- Character ---
    {
        poligon::PhysicsWorld world;
        AddFloor(world);
        // A tree trunk in the way, and a low ledge to the side.
        poligon::BodySettings trunk;
        trunk.position = {5.0f, 2.0f, 0.0f};
        world.AddCylinder(2.0f, 0.4f, trunk);
        poligon::BodySettings ledge;
        ledge.position = {0.0f, 0.15f, 6.0f};
        world.AddBox({3.0f, 0.15f, 1.0f}, ledge);
        poligon::BodySettings wall;
        wall.position = {0.0f, 1.0f, -6.0f};
        world.AddBox({3.0f, 1.0f, 1.0f}, wall);

        poligon::CharacterSettings settings;
        settings.position = {0.0f, 0.5f, 0.0f};
        const poligon::CharacterHandle character = world.AddCharacter(settings);
        StepFor(world, 1.0f);
        Expect(world.IsCharacterOnGround(character) && Near(world.GetCharacterPosition(character).y, 0.0f, 0.02f),
               "a character dropped from half a meter lands on its feet");

        world.SetCharacterInput(character, {2.0f, 0.0f, 0.0f}, false);
        world.Step(kTick);
        const float start_x = world.GetCharacterPosition(character).x;
        for (int tick = 0; tick < 60; ++tick) {
            world.SetCharacterInput(character, {2.0f, 0.0f, 0.0f}, false);
            world.Step(kTick);
        }
        Expect(Near(world.GetCharacterPosition(character).x - start_x, 2.0f, 0.05f), "at 2 m/s a character covers 2 m in a second");

        for (int tick = 0; tick < 240; ++tick) {
            world.SetCharacterInput(character, {2.0f, 0.0f, 0.0f}, false);
            world.Step(kTick);
        }
        // Trunk surface at x = 4.6, character radius 0.3.
        Expect(world.GetCharacterPosition(character).x < 4.4f && world.GetCharacterPosition(character).x > 4.0f,
               "a tree trunk stops the character");

        world.SetCharacterPosition(character, {0.0f, 0.0f, 3.0f});
        // The ledge spans z = 5 to 7; a second and a half at 2 m/s ends in its middle.
        for (int tick = 0; tick < 90; ++tick) {
            world.SetCharacterInput(character, {0.0f, 0.0f, 2.0f}, false);
            world.Step(kTick);
        }
        const glm::vec3 on_ledge = world.GetCharacterPosition(character);
        Expect(on_ledge.z > 5.3f && on_ledge.z < 6.7f && Near(on_ledge.y, 0.3f, 0.03f),
               "a ledge of 30 cm is stepped onto without a jump");

        world.SetCharacterPosition(character, {0.0f, 0.0f, -3.0f});
        for (int tick = 0; tick < 180; ++tick) {
            world.SetCharacterInput(character, {0.0f, 0.0f, -2.0f}, false);
            world.Step(kTick);
        }
        Expect(world.GetCharacterPosition(character).z > -4.8f && world.GetCharacterPosition(character).y < 0.1f,
               "a wall of 2 m is not");

        world.SetCharacterPosition(character, {-8.0f, 0.0f, 0.0f});
        StepFor(world, 0.2f);
        world.SetCharacterInput(character, {0.0f, 0.0f, 0.0f}, true);
        float highest = 0.0f;
        for (int tick = 0; tick < 90; ++tick) {
            world.Step(kTick);
            highest = std::fmax(highest, world.GetCharacterPosition(character).y);
        }
        // Jump speed 4.5 m/s: 4.5^2 / (2 * 9.81) = 1.03 m.
        Expect(Near(highest, 1.03f, 0.1f), "a jump reaches about a meter");
        Expect(world.IsCharacterOnGround(character), "and ends back on the ground");
    }

    // --- Repeatability ---
    {
        const std::vector<std::uint64_t> first = RunScript(false);
        const std::vector<std::uint64_t> second = RunScript(false);
        Expect(first == second, "the same script gives the same state on every tick, bit for bit");
        const std::vector<std::uint64_t> pushed = RunScript(true);
        Expect(first[89] == pushed[89] && first[299] != pushed[299], "a push of one gram-meter per second changes the outcome");
        Expect(first[0] != first[299], "the state hash follows the simulation");
    }

    std::printf("physics_test finished\n");
    return test::ExitCode();
}
