// Example: a trail of footprints left by a walker. Update it once per tick with where the
// feet are; draw it with FootprintTrail::Draw and a DecalMaterial (see the decals example).
#include <cstdio>

#include "poligon/world/footprints.h"

int main() {
    poligon::FootprintSettings settings;
    settings.max_prints = 8;
    poligon::FootprintTrail trail(settings);

    // Walk 10 m along +x; the ground keeps prints only where x < 7 (mud, then grass).
    const auto takes_prints = [](double x, double) { return x < 7.0; };
    for (int tick = 0; tick <= 100; ++tick) {
        trail.Update(glm::dvec3(tick * 0.1, 0.0, 0.0), takes_prints);
    }

    for (const poligon::Transform& print : trail.GetPrints()) {
        std::printf("print at x %.2f z %+.2f\n", print.position.x, print.position.z);
    }
    std::printf("%zu prints (at most %d kept)\n", trail.GetPrints().size(), settings.max_prints);
    return trail.GetPrints().empty() ? 1 : 0;
}
