// Checks temporal anti-aliasing and color grading: TAA smooths a hard edge over a few
// frames, gives the same picture for the same frames every time, follows a moving camera
// without leaving a ghost behind, and starts fresh after a reset; grading changes colors as
// asked. Usage: temporal_aa_test d3d12|vulkan
#include <cmath>
#include <cstdio>
#include <cstring>

#include "poligon/renderer.h"
#include "test_support.h"

namespace {

constexpr int kSize = 128;

poligon::RendererConfig MakeConfig(poligon::GraphicsBackend backend) {
    poligon::RendererConfig config;
    config.width = kSize;
    config.height = kSize;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.msaa_samples = 1;  // edges are hard unless TAA smooths them
    config.post.tone_mapping = poligon::ToneMapping::None;
    return config;
}

int RedAt(const poligon::Image& image, int x, int y) {
    return image.pixels[static_cast<size_t>(y * image.width + x) * 4];
}

// Pixels that are neither (almost) black nor (almost) white: the smoothed edge.
int CountInBetween(const poligon::Image& image) {
    int count = 0;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const int red = RedAt(image, x, y);
            count += red > 25 && red < 230 ? 1 : 0;
        }
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
        std::printf("usage: temporal_aa_test d3d12|vulkan\n");
        return 2;
    }

    // A white square, turned 20 degrees, on black, seen from above: its edges are slanted.
    const glm::mat4 identity(1.0f);
    poligon::Material white;
    white.roughness = 1.0f;
    poligon::DirectionalLight light;
    light.color = {0.0f, 0.0f, 0.0f, 1.0f};
    light.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
    poligon::Environment environment;
    environment.shadows = false;
    poligon::Camera camera;
    camera.position = {0.0f, 10.0f, 0.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, -1.0f};
    const glm::mat4 square = glm::rotate(identity, glm::radians(20.0f), {0.0f, 1.0f, 0.0f}) *
                             glm::scale(identity, {6.0f, 1.0f, 6.0f});

    const auto render = [&](poligon::Renderer& renderer, poligon::MeshHandle plane, const poligon::Camera& view,
                            poligon::Image& frame) {
        renderer.BeginFrame(view, light, environment);
        renderer.DrawMesh(plane, square, white);
        renderer.EndFrame();
        renderer.CaptureFrame(frame);
    };

    poligon::Image plain;
    poligon::Image smoothed;
    {
        poligon::Renderer renderer(MakeConfig(backend));
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        render(renderer, plane, camera, plain);

        poligon::PostSettings post = renderer.GetPostSettings();
        post.temporal_aa = true;
        renderer.SetPostSettings(post);
        for (int i = 0; i < 16; ++i) {
            render(renderer, plane, camera, smoothed);
        }
        const int hard = CountInBetween(plain);
        const int soft = CountInBetween(smoothed);
        if (soft < hard + 100) {
            std::printf("  pixels between black and white: %d without TAA, %d with\n", hard, soft);
        }
        Expect(hard < 20, "without anti-aliasing the edge is hard");
        Expect(soft >= hard + 100, "after 16 frames of TAA the edge is smooth");
        Expect(RedAt(smoothed, kSize / 2, kSize / 2) >= 250 && RedAt(smoothed, 2, 2) <= 5,
               "TAA keeps the inside white and the outside black");

        // A reset drops the history: the next frame is a single, unsmoothed one.
        renderer.ResetTemporalHistory();
        poligon::Image after_reset;
        render(renderer, plane, camera, after_reset);
        Expect(CountInBetween(after_reset) < 20, "after a reset the next frame starts fresh");
    }

    // The same frames give the same picture, to the bit: two renderers from scratch, one
    // after the other (a process holds one graphics device at a time here).
    poligon::Image runs[2];
    for (poligon::Image& run : runs) {
        poligon::Renderer renderer(MakeConfig(backend));
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        poligon::PostSettings post = renderer.GetPostSettings();
        post.temporal_aa = true;
        renderer.SetPostSettings(post);
        for (int i = 0; i < 17; ++i) {
            render(renderer, plane, camera, run);
        }
    }
    Expect(runs[0].pixels == runs[1].pixels, "the same frames give the same picture to the bit");

    // A moving camera: the square slides across the picture. TAA follows it; no copy of
    // the square is left where it was (a "ghost").
    poligon::Camera moving = camera;
    poligon::Image frame;
    {
        poligon::Renderer renderer(MakeConfig(backend));
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        poligon::PostSettings post = renderer.GetPostSettings();
        post.temporal_aa = true;
        renderer.SetPostSettings(post);
        for (int i = 0; i < 24; ++i) {
            moving.position.x = static_cast<float>(i) * 0.2f;
            moving.target.x = moving.position.x;
            render(renderer, plane, moving, frame);
        }
    }
    {
        poligon::Image reference;
        poligon::Renderer still(MakeConfig(backend));
        const poligon::MeshHandle still_plane = still.CreatePlaneMesh();
        render(still, still_plane, moving, reference);
        // A ghost is light where the single frame is black and stays black for two pixels
        // around (so that the smoothed edge itself does not count).
        const auto black_around = [&reference](int x, int y) {
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const int nx = glm::clamp(x + dx, 0, kSize - 1);
                    const int ny = glm::clamp(y + dy, 0, kSize - 1);
                    if (RedAt(reference, nx, ny) >= 5) {
                        return false;
                    }
                }
            }
            return true;
        };
        int ghost_pixels = 0;
        int differing = 0;
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const int difference = std::abs(RedAt(frame, x, y) - RedAt(reference, x, y));
                differing += difference > 64 ? 1 : 0;
                ghost_pixels += black_around(x, y) && RedAt(frame, x, y) > 32 ? 1 : 0;
            }
        }
        if (ghost_pixels > 40 || differing > 200) {
            std::printf("  moving camera: %d ghost pixels, %d pixels differ from a single frame\n", ghost_pixels, differing);
        }
        Expect(ghost_pixels <= 40, "a moving camera leaves no ghost behind");
        Expect(differing <= 200, "with a moving camera the picture matches a single frame apart from its edges");
    }

    // Color grading: no saturation turns color into gray; a vignette darkens the corners.
    {
        poligon::Renderer renderer(MakeConfig(backend));
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        poligon::Material orange;
        orange.color = {0.8f, 0.4f, 0.1f, 1.0f};
        orange.roughness = 1.0f;
        poligon::PostSettings post = renderer.GetPostSettings();
        post.saturation = 0.0f;
        renderer.SetPostSettings(post);
        poligon::Image graded;
        renderer.BeginFrame(camera, light, environment);
        renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), orange);
        renderer.EndFrame();
        renderer.CaptureFrame(graded);
        const std::uint8_t* center = &graded.pixels[static_cast<size_t>((kSize / 2) * kSize + kSize / 2) * 4];
        Expect(std::abs(center[0] - center[1]) <= 1 && std::abs(center[1] - center[2]) <= 1,
               "saturation 0 turns color into gray");

        post.saturation = 1.0f;
        post.vignette = 0.5f;
        renderer.SetPostSettings(post);
        renderer.BeginFrame(camera, light, environment);
        renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), white);
        renderer.EndFrame();
        renderer.CaptureFrame(graded);
        Expect(RedAt(graded, 1, 1) + 20 < RedAt(graded, kSize / 2, kSize / 2), "a vignette darkens the corners");

        post.vignette = 0.0f;
        post.temperature = 1.0f;
        renderer.SetPostSettings(post);
        poligon::Material gray;
        gray.color = {0.4f, 0.4f, 0.4f, 1.0f};
        gray.roughness = 1.0f;
        renderer.BeginFrame(camera, light, environment);
        renderer.DrawMesh(plane, glm::scale(identity, {40.0f, 1.0f, 40.0f}), gray);
        renderer.EndFrame();
        renderer.CaptureFrame(graded);
        const std::uint8_t* warm = &graded.pixels[static_cast<size_t>((kSize / 2) * kSize + kSize / 2) * 4];
        Expect(warm[0] > warm[2] + 20, "a warm temperature makes gray orange");
    }

    // Drawing at half size, brought back to full size by TAA: the picture keeps the output's
    // size and, after a few frames, nearly matches the one drawn at full size.
    {
        poligon::RendererConfig config = MakeConfig(backend);
        config.render_scale = 0.5f;
        poligon::Renderer renderer(config);
        Expect(renderer.GetRenderWidth() == kSize / 2 && renderer.GetRenderHeight() == kSize / 2,
               "the scene is drawn at half size");
        const poligon::MeshHandle plane = renderer.CreatePlaneMesh();
        poligon::PostSettings post = renderer.GetPostSettings();
        post.temporal_aa = true;
        renderer.SetPostSettings(post);
        poligon::Image upscaled;
        for (int i = 0; i < 24; ++i) {
            render(renderer, plane, camera, upscaled);
        }
        Expect(upscaled.width == kSize && upscaled.height == kSize, "the picture has the output's size");
        int differing = 0;
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                differing += std::abs(RedAt(upscaled, x, y) - RedAt(smoothed, x, y)) > 60 ? 1 : 0;
            }
        }
        if (differing > 120) {
            std::printf("  %d pixels differ from the picture drawn at full size\n", differing);
        }
        Expect(differing <= 120, "upscaled by TAA the picture matches the one drawn at full size apart from its edges");
        Expect(RedAt(upscaled, kSize / 2, kSize / 2) >= 250 && RedAt(upscaled, 2, 2) <= 5,
               "upscaling keeps the inside white and the outside black");
    }

    std::printf("temporal_aa_test finished\n");
    return test::ExitCode();
}
