// Example: a sunny scene with sky, shadows, a shiny and a matte material, and the effects
// that make it look less computer-made: ambient occlusion, fog with shafts of light,
// bloom and automatic exposure.
// Usage: example_environment OUTPUT.png
#include <cstdio>

#include "poligon/render/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_environment OUTPUT.png\n");
        return 2;
    }

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    // Light values in a sunny scene go well above 1. Automatic exposure scales them to a
    // pleasant brightness before tone mapping; bloom adds a glow around the brightest areas.
    config.post.auto_exposure = true;
    config.post.bloom = true;
    poligon::Renderer renderer(config);

    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    poligon::Camera camera;
    // Looking towards the sun, past a row of posts.
    camera.position = {-5.5f, 1.7f, -4.5f};
    camera.target = {0.0f, 1.6f, 0.0f};

    // The sun: direction it shines in, and a warm color brighter than 1.
    poligon::DirectionalLight sun;
    sun.direction = {-0.6f, -0.45f, -0.5f};
    sun.color = {2.6f, 2.45f, 2.2f, 1.0f};

    poligon::Environment environment;
    environment.draw_sky = true;      // sky gradient and sun disc instead of the clear color
    environment.sky_light = true;     // shadows are lit by the blue sky, not by flat gray
    environment.shadow_distance = 40.0f;
    environment.ambient_occlusion = true;  // darker where the cubes meet the ground
    // Fog computed along every line of sight: darker in the shadow of the posts, which
    // shows as shafts of light between them, and thinning out with height.
    environment.volumetric_fog = true;
    environment.fog_density = 0.04f;
    environment.fog_height_falloff = 0.08f;
    environment.fog_shadow_brightness = 0.2f;

    const glm::mat4 identity(1.0f);
    renderer.BeginFrame(camera, sun, environment);

    poligon::Material ground;
    ground.color = {0.35f, 0.45f, 0.25f, 1.0f};
    ground.roughness = 1.0f;
    renderer.DrawMesh(plane, glm::dvec3(0.0), glm::scale(identity, {60.0f, 1.0f, 60.0f}), ground);

    poligon::Material matte;
    matte.color = {0.8f, 0.3f, 0.2f, 1.0f};
    matte.roughness = 0.9f;
    renderer.DrawMesh(cube, glm::dvec3(0.0), glm::translate(identity, {-1.0f, 0.5f, 0.0f}), matte);

    poligon::Material polished_metal;
    polished_metal.color = {0.9f, 0.8f, 0.5f, 1.0f};
    polished_metal.metallic = 1.0f;
    polished_metal.roughness = 0.25f;
    renderer.DrawMesh(cube, glm::dvec3(0.0), glm::translate(identity, {1.0f, 0.75f, 0.0f}) * glm::scale(identity, {1.0f, 1.5f, 1.0f}),
                      polished_metal);

    // Posts between the camera and the sun; each casts a dark stripe into the fog.
    poligon::Material wood;
    wood.color = {0.35f, 0.25f, 0.18f, 1.0f};
    for (int i = 0; i < 7; ++i) {
        const float along = static_cast<float>(i) - 3.0f;
        const glm::vec3 position{2.0f + along * 0.9f, 4.0f, 2.5f - along * 1.3f};
        renderer.DrawMesh(cube, glm::dvec3(0.0), glm::translate(identity, position) * glm::scale(identity, {0.35f, 8.0f, 0.35f}), wood);
    }

    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s (multisampling: %d samples)\n", argv[1], renderer.GetMsaaSamples());
    return 0;
}
