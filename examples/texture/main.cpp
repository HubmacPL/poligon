// Example: make an image, save and load it, and draw a textured cube without a window.
// Usage: example_texture OUTPUT_DIRECTORY
#include <cstdio>
#include <string>

#include "poligon/image.h"
#include "poligon/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_texture OUTPUT_DIRECTORY\n");
        return 2;
    }
    const std::string directory = argv[1];
    const std::string texture_path = directory + "/example_texture_checker.png";
    const std::string frame_path = directory + "/example_texture_frame.png";

    // An image is plain RGBA bytes, top row first. Here: an orange and white checker, saved as PNG.
    const std::uint8_t orange[4] = {230, 130, 30, 255};
    const std::uint8_t white[4] = {255, 255, 255, 255};
    if (!poligon::SaveImageToPng(texture_path.c_str(), poligon::MakeCheckerImage(64, 64, 16, orange, white))) {
        std::fprintf(stderr, "cannot write %s\n", texture_path.c_str());
        return 1;
    }

    // Loading works for PNG and JPEG files.
    poligon::Image image;
    std::string error;
    if (!poligon::LoadImageFromFile(texture_path.c_str(), image, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.clear_color = {0.1f, 0.1f, 0.15f, 1.0f};
    poligon::Renderer renderer(config);

    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
    // CreateTexture copies the image to the graphics card and builds mipmaps.
    const poligon::TextureHandle checker = renderer.CreateTexture(image);

    poligon::Camera camera;
    camera.position = {3.0f, 2.5f, 4.0f};
    camera.target = {0.0f, 0.5f, 0.0f};
    const poligon::DirectionalLight light;
    const glm::mat4 identity(1.0f);

    renderer.BeginFrame(camera, light);

    // The ground: the same texture, tinted gray and repeated 5 times in each direction.
    poligon::Material ground_material;
    ground_material.texture = checker;
    ground_material.color = {0.5f, 0.5f, 0.5f, 1.0f};
    ground_material.uv_scale = {5.0f, 5.0f};
    renderer.DrawMesh(plane, glm::scale(identity, {10.0f, 1.0f, 10.0f}), ground_material);

    // The cube: every face shows the whole texture once.
    poligon::Material cube_material;
    cube_material.texture = checker;
    renderer.DrawMesh(cube, glm::translate(identity, {0.0f, 0.5f, 0.0f}), cube_material);

    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(frame_path.c_str())) {
        std::fprintf(stderr, "cannot write %s\n", frame_path.c_str());
        return 1;
    }
    std::printf("loaded a %dx%d texture and saved the frame to %s\n", image.width, image.height, frame_path.c_str());
    return 0;
}
