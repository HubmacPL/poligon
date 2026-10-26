// Example: point and spot lights at night. No sun, a faint sky; three colored lamps on posts
// light the ground and the blocks around them, and a flashlight (a spot light with shadows)
// throws the shadow of a crate onto the ground. A little volumetric fog shows the lamps' glow
// and the flashlight's beam (temporal anti-aliasing smooths the fog's noise over the frames).
// Usage: example_lights OUTPUT.png
#include <cstdio>

#include "poligon/render/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_lights OUTPUT.png\n");
        return 2;
    }
    poligon::RendererConfig config;
    config.width = 960;
    config.height = 540;
    config.headless = true;
    config.post.temporal_aa = true;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    // Night: the sun off, a very faint ambient light.
    poligon::DirectionalLight night;
    night.color = {0.0f, 0.0f, 0.0f, 1.0f};
    night.ambient = {0.01f, 0.012f, 0.02f, 1.0f};
    poligon::Environment environment;
    environment.volumetric_fog = true;
    environment.fog_density = 0.02f;
    environment.fog_color = {0.02f, 0.02f, 0.03f, 1.0f};

    poligon::Camera camera;
    camera.position = {0.0, 2.0, 9.0};
    camera.target = {0.0, 0.8, 0.0};
    const glm::quat level(1.0f, 0.0f, 0.0f, 0.0f);
    poligon::Material ground;
    ground.color = {0.5f, 0.5f, 0.45f, 1.0f};
    ground.roughness = 0.9f;
    poligon::Material stone = ground;
    stone.color = {0.7f, 0.65f, 0.6f, 1.0f};
    poligon::Material glow;  // the lamps' bulbs give off light themselves
    glow.emissive = {1.0f, 0.9f, 0.7f, 1.0f};
    glow.emissive_strength = 6.0f;

    for (int frame = 0; frame < 16; ++frame) {
        renderer.BeginFrame(camera, night, environment);
        renderer.DrawMesh(plane, poligon::Transform(glm::dvec3(0.0), level, glm::vec3(40.0f)), ground);
        for (int i = 0; i < 6; ++i) {
            renderer.DrawMesh(cube, poligon::Transform({-5.0 + 2.0 * i, 0.5, -1.5 + (i % 2) * 1.0}, level, glm::vec3(1.0f)), stone);
        }
        // Three lamps on posts.
        const glm::dvec3 lamps[3] = {{-4.0, 2.2, 1.0}, {0.5, 2.2, -0.5}, {4.0, 2.2, 1.5}};
        const poligon::Color colors[3] = {{1.0f, 0.55f, 0.2f, 1.0f}, {0.4f, 0.7f, 1.0f, 1.0f}, {1.0f, 0.3f, 0.5f, 1.0f}};
        for (int i = 0; i < 3; ++i) {
            renderer.DrawMesh(cube, poligon::Transform(lamps[i] - glm::dvec3(0.0, 1.1, 0.0), level, {0.08f, 2.2f, 0.08f}), stone);
            renderer.DrawMesh(cube, poligon::Transform(lamps[i] + glm::dvec3(0.0, 0.1, 0.0), level, glm::vec3(0.2f)), glow);
            poligon::PointLight lamp;
            lamp.position = lamps[i];
            lamp.color = colors[i];
            lamp.intensity = 6.0f;
            lamp.range = 9.0f;
            renderer.DrawLight(lamp);
        }
        // A flashlight held at the camera's right, pointing at a crate, which throws a shadow.
        renderer.DrawMesh(cube, poligon::Transform({1.5, 0.4, 3.5}, level, glm::vec3(0.8f)), stone);
        poligon::SpotLight flashlight;
        flashlight.position = {2.5, 1.6, 6.5};
        flashlight.direction = glm::normalize(glm::vec3(1.5f - 2.5f, 0.2f - 1.6f, 3.0f - 6.5f));
        flashlight.color = {1.0f, 0.95f, 0.85f, 1.0f};
        flashlight.intensity = 25.0f;
        flashlight.range = 20.0f;
        flashlight.inner_angle_degrees = 12.0f;
        flashlight.outer_angle_degrees = 20.0f;
        flashlight.shadows = true;
        renderer.DrawLight(flashlight);
        renderer.EndFrame();
    }
    std::printf("lights drawn: %d\n", renderer.GetFrameStats().lights_drawn);
    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}
