// Example: a physics world with a floor, a wall of crates, a ball thrown at it, a character
// that walks, and a ray that asks what lies ahead.
// Usage: example_physics OUTPUT.png
#include <cstdio>
#include <vector>

#include "poligon/physics/physics.h"
#include "poligon/render/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_physics OUTPUT.png\n");
        return 2;
    }
    const glm::mat4 identity(1.0f);

    poligon::PhysicsWorld physics;

    // A body that never moves: the floor, a slab whose top is at height 0.
    // Sizes are half extents; the position is the center.
    poligon::BodySettings floor;
    floor.position = {0.0f, -0.5f, 0.0f};
    physics.AddBox({20.0f, 0.5f, 20.0f}, floor);

    // Bodies that move: a wall of crates, three wide and four high.
    const float crate_size = 0.8f;
    std::vector<poligon::BodyHandle> crates;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 3; ++column) {
            poligon::BodySettings crate;
            crate.dynamic = true;
            crate.mass = 5.0f;
            crate.position = {(static_cast<float>(column) - 1.0f) * crate_size, crate_size * (static_cast<float>(row) + 0.5f), 0.0f};
            crates.push_back(physics.AddBox(glm::vec3(crate_size * 0.5f), crate));
        }
    }

    // A heavy ball thrown at the wall.
    poligon::BodySettings ball_settings;
    ball_settings.dynamic = true;
    ball_settings.mass = 40.0f;
    ball_settings.position = {0.0f, 1.2f, 8.0f};
    ball_settings.velocity = {0.0f, 2.0f, -14.0f};
    const poligon::BodyHandle ball = physics.AddSphere(0.4f, ball_settings);

    // A character: told where it wants to go, kept on the ground and out of things by the world.
    poligon::CharacterSettings character_settings;
    character_settings.position = {6.0f, 0.0f, 4.0f};
    const poligon::CharacterHandle character = physics.AddCharacter(character_settings);

    // One Step per tick, always with the same length. 90 ticks are a second and a half.
    for (int tick = 0; tick < 90; ++tick) {
        physics.SetCharacterInput(character, {-2.0f, 0.0f, 0.0f}, false);  // walk towards -x at 2 m/s
        physics.Step(1.0f / 60.0f);
    }

    const glm::vec3 ball_position = physics.GetPosition(ball);
    const glm::dvec3 walker = physics.GetCharacterPosition(character);
    std::printf("ball at %.2f %.2f %.2f\n", static_cast<double>(ball_position.x), static_cast<double>(ball_position.y),
                static_cast<double>(ball_position.z));
    std::printf("character at %.2f %.2f %.2f, on ground: %s\n", static_cast<double>(walker.x), static_cast<double>(walker.y),
                walker.z, physics.IsCharacterOnGround(character) ? "yes" : "no");

    // A ray: what is 50 m straight down from a point above the scene?
    const poligon::RayHit hit = physics.CastRay({0.0f, 10.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 50.0f);
    if (hit.hit) {
        std::printf("the ray hit body %u after %.2f m\n", hit.body.id, static_cast<double>(hit.distance));
    }
    // The same calls in the same order always end in the same state; this number shows it.
    std::printf("state hash %016llx\n", static_cast<unsigned long long>(physics.HashState()));

    // --- Drawing: the renderer knows nothing of physics; bodies are drawn where they are. ---
    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.post.exposure = 0.5f;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    poligon::Camera camera;
    camera.position = {7.0f, 4.0f, 7.0f};
    camera.target = {0.0f, 1.0f, -1.0f};
    poligon::DirectionalLight sun;
    sun.color = {2.6f, 2.45f, 2.2f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;

    renderer.BeginFrame(camera, sun, environment);
    renderer.DrawMesh(plane, glm::dvec3(0.0), glm::scale(identity, {40.0f, 1.0f, 40.0f}), poligon::Color{0.4f, 0.5f, 0.3f, 1.0f});
    for (const poligon::BodyHandle crate : crates) {
        // GetTransform is position (double) and rotation; the unit cube still has to be given its size.
        poligon::Transform placed = physics.GetTransform(crate);
        placed.scale = glm::vec3(crate_size);
        renderer.DrawMesh(cube, placed, poligon::Color{0.7f, 0.5f, 0.3f, 1.0f});
    }
    // There is no sphere mesh yet; the ball is drawn as a small cube.
    poligon::Transform ball_placed = physics.GetTransform(ball);
    ball_placed.scale = glm::vec3(0.65f);
    renderer.DrawMesh(cube, ball_placed, poligon::Color{0.8f, 0.2f, 0.2f, 1.0f});
    // The character as a tall box standing on its position.
    renderer.DrawMesh(cube, poligon::Transform(walker + glm::dvec3(0.0, 0.9, 0.0), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), {0.6f, 1.8f, 0.6f}),
                      poligon::Color{0.2f, 0.4f, 0.8f, 1.0f});
    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}
