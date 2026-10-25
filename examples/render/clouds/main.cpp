// Example: the sky and the light it gives. Volumetric clouds drift over a plain, their
// shadows darken parts of it, far things fade into the haze of distance, and a pit in the
// middle sees less of the sky (a sky occlusion map), so its floor gets less sky light.
// Usage: example_clouds OUTPUT.png
#include <cstdio>

#include "poligon/render/renderer.h"
#include "poligon/world/sky_occlusion.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_clouds OUTPUT.png\n");
        return 2;
    }
    const glm::mat4 identity(1.0f);
    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.post.exposure = 0.55f;
    poligon::Renderer renderer(config);
    renderer.PrepareClouds();  // makes the cloud noise now rather than in the first frame
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    poligon::Camera camera;
    camera.position = {0.0f, 40.0f, 60.0f};
    camera.target = {0.0f, 300.0f, -2000.0f};
    camera.far_plane = 20000.0f;
    poligon::DirectionalLight sun;
    sun.direction = {-0.3f, -0.7f, -0.6f};
    sun.color = {2.8f, 2.6f, 2.3f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;
    environment.shadow_distance = 300.0f;
    environment.clouds = true;            // a layer of clouds from 1500 to 3500 m
    environment.cloud_coverage = 0.5f;
    environment.cloud_shadows = true;     // their shadows on the land
    environment.wind_time = 120.0f;       // the game's time: clouds drift with it
    environment.aerial_density = 0.0003f; // far land fades into a bluish haze

    // A sky occlusion map: within 30 m of the middle the ground sees only a third of the sky.
    poligon::SkyOcclusionMap occlusion;
    occlusion.resolution = 64;
    occlusion.spacing = 2.0f;
    occlusion.origin = {-64.0f, -64.0f};
    occlusion.values.assign(64u * 64u, 255);
    poligon::AddCanopy(occlusion, {0.0f, -20.0f}, 30.0f, 0.66f);
    renderer.SetSkyOcclusion(&occlusion);

    poligon::Material grass;
    grass.color = {0.35f, 0.45f, 0.22f, 1.0f};
    grass.roughness = 1.0f;
    renderer.BeginFrame(camera, sun, environment);
    renderer.DrawMesh(plane, glm::dvec3(0.0), glm::scale(identity, {40000.0f, 1.0f, 40000.0f}), grass);
    renderer.EndFrame();
    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}
