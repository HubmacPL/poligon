// Checks the sky from the atmosphere model (SkyModel::Atmosphere, poligon/render/sky.h): the
// sun's place for a time, day and latitude; a sun 5 degrees above the horizon gives a redder
// sky towards it than one 60 degrees up (red over blue, in the model and on screen); the
// zenith gets brighter as the sun climbs, at five heights; the sun's light is redder and
// weaker low down and gone below the horizon.
// Usage: sky_render_test d3d12|vulkan
#include <cmath>
#include <cstdio>
#include <cstring>

#include "poligon/render/renderer.h"
#include "poligon/render/sky.h"
#include "test_support.h"

namespace {

// The way to the sun at `elevation` degrees, towards the south (+z).
glm::vec3 ToSun(float elevation) {
    const float angle = glm::radians(elevation);
    return glm::vec3(0.0f, std::sin(angle), std::cos(angle));
}

// Red over blue, and the brightness, averaged over the middle of the picture.
void MeasureMiddle(const poligon::Image& image, float& red_over_blue, float& brightness) {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    int count = 0;
    for (int y = image.height / 4; y < image.height * 3 / 4; ++y) {
        for (int x = image.width / 4; x < image.width * 3 / 4; ++x) {
            const std::uint8_t* pixel = &image.pixels[static_cast<size_t>(y * image.width + x) * 4];
            // Back to linear light.
            const auto linear = [](std::uint8_t value) { return std::pow(value / 255.0, 2.2); };
            red += linear(pixel[0]);
            green += linear(pixel[1]);
            blue += linear(pixel[2]);
            ++count;
        }
    }
    red_over_blue = static_cast<float>(red / std::max(blue, 1e-6));
    brightness = static_cast<float>((0.2126 * red + 0.7152 * green + 0.0722 * blue) / count);
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
        std::printf("usage: sky_render_test d3d12|vulkan\n");
        return 2;
    }

    // The sun's place: at noon on the summer solstice at 50.86 N it stands 62.6 degrees up in
    // the south; at 6:00 in the east, at 21:00 below the horizon.
    const glm::vec3 noon = -poligon::ComputeSunDirection(12.0f, 172.0f, 50.86f);
    const float noon_elevation = glm::degrees(std::asin(noon.y));
    std::printf("  noon: elevation %.2f, to the sun %.3f %.3f %.3f\n", noon_elevation, noon.x, noon.y, noon.z);
    Expect(std::abs(noon_elevation - 62.6f) < 0.6f && noon.z > 0.4f && std::abs(noon.x) < 0.01f, "noon sun in the south, 62.6 degrees up");
    const glm::vec3 morning = -poligon::ComputeSunDirection(6.0f, 172.0f, 50.86f);
    Expect(morning.x > 0.5f && morning.y > 0.0f, "the sun rises in the east (+x)");
    Expect(-poligon::ComputeSunDirection(21.0f, 172.0f, 50.86f).y < 0.0f, "at 21:00 solar time the sun has set");

    // The model on the processor.
    const poligon::SkyLight low = poligon::ComputeSkyLight(-ToSun(5.0f));
    const poligon::SkyLight high = poligon::ComputeSkyLight(-ToSun(60.0f));
    std::printf("  sun at ground: 5 deg %.3f %.3f %.3f, 60 deg %.3f %.3f %.3f\n", low.sun_at_ground.x, low.sun_at_ground.y,
                low.sun_at_ground.z, high.sun_at_ground.x, high.sun_at_ground.y, high.sun_at_ground.z);
    Expect(low.sun_at_ground.x / low.sun_at_ground.z > 1.5f * high.sun_at_ground.x / high.sun_at_ground.z,
           "a low sun's light is redder");
    Expect(low.sun_at_ground.y < high.sun_at_ground.y, "and weaker");
    Expect(low.horizon.x / low.horizon.z > 1.2f * high.horizon.x / high.horizon.z, "the horizon is redder with a low sun");
    Expect(glm::length(poligon::ComputeSkyLight(-ToSun(-3.0f)).sun_at_ground) == 0.0f, "no sunlight below the horizon");

    poligon::RendererConfig config;
    config.width = 128;
    config.height = 128;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.post.tone_mapping = poligon::ToneMapping::None;
    config.post.exposure = 2.0f;
    poligon::Renderer renderer(config);
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_model = poligon::SkyModel::Atmosphere;
    environment.shadows = false;
    poligon::DirectionalLight light;

    const auto render = [&](const poligon::Camera& camera, float elevation, float& red_over_blue, float& brightness) {
        light.direction = -ToSun(elevation);
        renderer.BeginFrame(camera, light, environment);
        renderer.EndFrame();
        poligon::Image picture;
        renderer.CaptureFrame(picture);
        MeasureMiddle(picture, red_over_blue, brightness);
    };

    // Towards the sun, 10 degrees up, a 30 degree view: redder with the sun low.
    poligon::Camera towards;
    towards.position = {0.0, 0.0, 0.0};
    towards.target = {0.0, std::tan(glm::radians(10.0)), 1.0};
    towards.vertical_fov_degrees = 30.0f;
    float low_ratio = 0.0f;
    float high_ratio = 0.0f;
    float ignored = 0.0f;
    render(towards, 5.0f, low_ratio, ignored);
    render(towards, 60.0f, high_ratio, ignored);
    std::printf("  towards the sun, red / blue: sun at 5 deg %.3f, at 60 deg %.3f\n", low_ratio, high_ratio);
    Expect(low_ratio > 1.3f * high_ratio, "the sky towards a sun 5 degrees up is redder than towards one 60 degrees up");

    // Straight up: brighter as the sun climbs.
    poligon::Camera up;
    up.position = {0.0, 0.0, 0.0};
    up.target = {0.0, 1.0, 0.0};
    up.up = {0.0f, 0.0f, -1.0f};
    up.vertical_fov_degrees = 30.0f;
    float previous = -1.0f;
    bool rising = true;
    for (const float elevation : {2.0f, 10.0f, 25.0f, 45.0f, 70.0f}) {
        float ratio = 0.0f;
        float zenith = 0.0f;
        render(up, elevation, ratio, zenith);
        std::printf("  zenith brightness with the sun at %.0f deg: %.4f\n", elevation, zenith);
        rising = rising && zenith > previous;
        previous = zenith;
    }
    Expect(rising, "the zenith gets brighter at each of five sun heights");
    return test::ExitCode();
}
