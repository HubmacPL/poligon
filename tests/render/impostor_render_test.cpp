// Checks impostors: baking (which direction each frame shows, what the color and normal
// pictures hold), saving and loading, drawing (size and color match the model itself), and
// the fade between levels of detail.
// Usage: impostor_render_test d3d12|vulkan
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "poligon/render/impostor.h"
#include "poligon/render/renderer.h"
#include "test_support.h"

namespace {

constexpr int kSize = 256;
constexpr int kFrames = 8;
constexpr int kFrameSize = 64;

// Pixel at the middle of frame (column, row) of an impostor picture.
const std::uint8_t* FrameMiddle(const poligon::Image& image, int column, int row) {
    const int x = column * kFrameSize + kFrameSize / 2;
    const int y = row * kFrameSize + kFrameSize / 2;
    return &image.pixels[static_cast<size_t>(y * image.width + x) * 4];
}

// How many pixels of the row through the middle of the picture are mostly red.
int RedWidth(const poligon::Image& image) {
    int count = 0;
    const int y = image.height / 2;
    for (int x = 0; x < image.width; ++x) {
        const std::uint8_t* pixel = &image.pixels[static_cast<size_t>(y * image.width + x) * 4];
        count += pixel[0] > 100 && pixel[1] < 60 ? 1 : 0;
    }
    return count;
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;

    poligon::GraphicsBackend backend = poligon::GraphicsBackend::Default;
    if (argc == 2 && std::strcmp(argv[1], "d3d12") == 0) {
        backend = poligon::GraphicsBackend::Direct3D12;
    } else if (argc == 2 && std::strcmp(argv[1], "vulkan") == 0) {
        backend = poligon::GraphicsBackend::Vulkan;
    } else {
        std::printf("usage: impostor_render_test d3d12|vulkan\n");
        return 2;
    }

    poligon::RendererConfig config;
    config.width = kSize;
    config.height = kSize;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.msaa_samples = 1;
    config.post.tone_mapping = poligon::ToneMapping::None;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle cube = renderer.CreateCubeMesh();
    const glm::mat4 identity(1.0f);

    // A "model": a red block 2 x 4 x 2 m standing on the origin, with a green lid.
    poligon::Material red;
    red.color = {1.0f, 0.0f, 0.0f, 1.0f};
    red.roughness = 1.0f;
    poligon::Material green = red;
    green.color = {0.0f, 1.0f, 0.0f, 1.0f};
    const poligon::ImpostorPart parts[2] = {
        {cube, red, glm::translate(identity, {0.0f, 2.0f, 0.0f}) * glm::scale(identity, {2.0f, 4.0f, 2.0f})},
        {cube, green, glm::translate(identity, {0.0f, 4.05f, 0.0f}) * glm::scale(identity, {2.0f, 0.1f, 2.0f})},
    };

    poligon::Impostor impostor;
    renderer.BakeImpostor(parts, 2, kFrames, kFrameSize, impostor);
    Expect(impostor.frames_per_side == kFrames && impostor.color.width == kFrames * kFrameSize &&
               impostor.normal.height == kFrames * kFrameSize,
           "the pictures hold frames x frames frames of the asked size");
    const float expected_radius = std::sqrt(2.0f * 2.0f + 4.1f * 4.1f + 2.0f * 2.0f) * 0.5f * 1.02f;
    Expect(glm::length(impostor.center - glm::vec3(0.0f, 2.05f, 0.0f)) < 0.01f &&
               std::fabs(impostor.radius - expected_radius) < 0.01f,
           "the frames show the sphere around the model's box");

    int empty_frames = 0;
    for (int row = 0; row < kFrames; ++row) {
        for (int column = 0; column < kFrames; ++column) {
            empty_frames += FrameMiddle(impostor.color, column, row)[3] > 127 ? 0 : 1;
        }
    }
    Expect(empty_frames == 0, "the model is in the middle of every frame");

    // Frame (0, 0) looks from the horizon: its grid point (-1, -1) is the direction
    // (0, 0, -1), so the camera stands at -z and sees the red side facing it.
    const std::uint8_t* side = FrameMiddle(impostor.color, 0, 0);
    const std::uint8_t* side_normal = FrameMiddle(impostor.normal, 0, 0);
    if (side[0] < 250 || side[1] > 5 || side_normal[2] > 10) {
        std::printf("  side frame: color %d %d %d, normal %d %d %d\n", side[0], side[1], side[2], side_normal[0], side_normal[1],
                    side_normal[2]);
    }
    Expect(side[0] >= 250 && side[1] <= 5, "a frame from the horizon shows the red side");
    Expect(side_normal[2] <= 10 && std::abs(side_normal[1] - 128) <= 3,
           "its normal picture says the surface faces -z (blue 0, green middle)");

    // Frames near the middle of the grid look almost straight down: the green lid.
    const std::uint8_t* top = FrameMiddle(impostor.color, 3, 3);
    const std::uint8_t* top_normal = FrameMiddle(impostor.normal, 3, 3);
    if (top[1] < 250 || top_normal[1] < 250) {
        std::printf("  top frame: color %d %d %d, normal %d %d %d\n", top[0], top[1], top[2], top_normal[0], top_normal[1],
                    top_normal[2]);
    }
    Expect(top[1] >= 250 && top[0] <= 5, "a frame from above shows the green lid");
    Expect(top_normal[1] >= 250, "its normal picture says the surface faces up");

    // Saving and loading gives the same impostor.
    const std::string path = (std::filesystem::temp_directory_path() / "poligon_impostor_test.impostor").string();
    poligon::Impostor loaded;
    std::string error;
    Expect(poligon::SaveImpostor(path.c_str(), impostor), "the impostor can be saved");
    Expect(poligon::LoadImpostor(path.c_str(), loaded, error), "the impostor can be loaded");
    Expect(loaded.frames_per_side == impostor.frames_per_side && loaded.center == impostor.center &&
               loaded.radius == impostor.radius && loaded.color.pixels == impostor.color.pixels &&
               loaded.normal.pixels == impostor.normal.pixels,
           "the loaded impostor is identical to the saved one");

    // Drawn from the side, the card matches the model: same red, about the same width.
    poligon::Camera camera;
    camera.position = {0.0f, 2.0f, 30.0f};
    camera.target = {0.0f, 2.0f, 0.0f};
    camera.vertical_fov_degrees = 20.0f;
    poligon::DirectionalLight light;
    light.color = {0.0f, 0.0f, 0.0f, 1.0f};
    light.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
    poligon::Environment environment;
    environment.shadows = false;
    poligon::Image model_frame;
    poligon::Image impostor_frame;

    renderer.BeginFrame(camera, light, environment);
    renderer.DrawMesh(parts[0].mesh, glm::dvec3(0.0), parts[0].transform, parts[0].material);
    renderer.DrawMesh(parts[1].mesh, glm::dvec3(0.0), parts[1].transform, parts[1].material);
    renderer.EndFrame();
    renderer.CaptureFrame(model_frame);

    const poligon::ImpostorHandle handle = renderer.CreateImpostor(loaded);
    renderer.BeginFrame(camera, light, environment);
    renderer.DrawImpostorInstances(handle, glm::dvec3(0.0), &identity, 1);
    renderer.EndFrame();
    renderer.CaptureFrame(impostor_frame);
    const int model_width = RedWidth(model_frame);
    const int impostor_width = RedWidth(impostor_frame);
    const std::uint8_t* middle = &impostor_frame.pixels[static_cast<size_t>((kSize / 2) * kSize + kSize / 2) * 4];
    if (std::abs(model_width - impostor_width) > 3 || middle[0] < 200) {
        std::printf("  red width: model %d, impostor %d; impostor middle %d %d %d\n", model_width, impostor_width, middle[0],
                    middle[1], middle[2]);
    }
    Expect(model_width > 20, "the model itself is drawn");
    Expect(std::abs(model_width - impostor_width) <= 3, "the impostor is as wide as the model");
    Expect(middle[0] >= 200 && middle[1] <= 30, "the impostor shows the model's color");
    Expect(renderer.GetFrameStats().instances_drawn == 1, "the impostor counts as one drawn instance");

    // Fading: with 0.5 in the last value of the matrix about half of the pixels are drawn.
    glm::mat4 half_faded = identity;
    half_faded[3][3] = 0.5f;
    renderer.BeginFrame(camera, light, environment);
    renderer.DrawImpostorInstances(handle, glm::dvec3(0.0), &half_faded, 1);
    renderer.EndFrame();
    renderer.CaptureFrame(impostor_frame);
    const int faded_width = RedWidth(impostor_frame);
    if (faded_width < impostor_width / 4 || faded_width > impostor_width * 3 / 4) {
        std::printf("  faded red width %d of %d\n", faded_width, impostor_width);
    }
    Expect(faded_width >= impostor_width / 4 && faded_width <= impostor_width * 3 / 4,
           "an impostor fading in at one half shows about half of its pixels");

    renderer.DestroyImpostor(handle);
    const std::string base = path.substr(0, path.size() - std::strlen(".impostor"));
    for (const std::string& file : {path, base + "_color.png", base + "_normal.png"}) {
        std::filesystem::remove(file);
    }
    std::printf("impostor_render_test finished\n");
    return test::ExitCode();
}
