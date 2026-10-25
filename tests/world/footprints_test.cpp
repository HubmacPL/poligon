// FootprintTrail (poligon/world/footprints.h): prints every stride, left and right in turn,
// facing the way walked, only where the ground takes them, at most max_prints, none across a
// teleport.
#include <cmath>
#include <cstdio>

#include "poligon/world/footprints.h"
#include "test_support.h"

int main() {
    using test::Expect;

    poligon::FootprintSettings settings;
    settings.stride = 0.5f;
    settings.max_prints = 50;
    poligon::FootprintTrail trail(settings);
    // Walking 10 m along -z in steps of 5 cm (1.5 m/s at 30 ticks a second).
    for (int step = 0; step <= 200; ++step) {
        trail.Update({0.0f, 0.0f, -0.05f * static_cast<float>(step)});
    }
    const std::vector<poligon::Transform>& prints = trail.GetPrints();
    Expect(prints.size() == 20, "a print every stride");
    bool alternate = true;
    bool facing = true;
    for (size_t i = 0; i < prints.size(); ++i) {
        const float x = static_cast<float>(prints[i].position.x);
        alternate = alternate && (i % 2 == 0 ? x < 0.0f : x > 0.0f) && std::abs(std::abs(x) - settings.step_width) < 1e-4f;
        // The box's -z (its picture's top) points the way walked (-z here).
        const glm::vec3 up_in_picture = glm::normalize(prints[i].rotation * glm::vec3(0.0f, 0.0f, -1.0f));
        facing = facing && up_in_picture.z < -0.999f;
    }
    Expect(alternate, "left and right of the path in turn");
    Expect(facing, "turned the way walked");

    for (int step = 201; step <= 1400; ++step) {
        trail.Update({0.0f, 0.0f, -0.05f * static_cast<float>(step)});
    }
    Expect(trail.GetPrints().size() == 50, "at most max_prints");

    trail.Update({100.0f, 0.0f, 0.0f});  // teleport
    trail.Update({100.0f, 0.0f, -0.3f});
    trail.Update({100.0f, 0.0f, -0.6f});
    Expect(trail.GetPrints().back().position.x > 99.0 && glm::distance(trail.GetPrints()[48].position, trail.GetPrints()[49].position) > 50.0,
           "after a teleport the trail goes on from the new place");

    poligon::FootprintTrail picky(settings);
    for (int step = 0; step <= 200; ++step) {
        picky.Update({0.0f, 0.0f, -0.05f * static_cast<float>(step)}, [](double, double z) { return z > -4.75; });
    }
    Expect(picky.GetPrints().size() == 9, "only where the ground takes prints");

    std::printf("footprints_test finished\n");
    return test::ExitCode();
}
