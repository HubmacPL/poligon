// Example: turn uneven real frame times into fixed simulation ticks.
// GameLoop does this for you; use FixedStepClock directly only when writing your own loop.
#include <cstdio>

#include "poligon/core/fixed_step_clock.h"

int main() {
    // 60 ticks per second, at most 5 ticks per call.
    poligon::FixedStepClock clock(60, 5);

    // Frame times in milliseconds: fast, normal, slow, and a 1 second hitch.
    const int frame_milliseconds[] = {5, 16, 40, 1000};
    for (const int milliseconds : frame_milliseconds) {
        const int ticks = clock.Advance(static_cast<long long>(milliseconds) * 1'000'000);
        std::printf("frame of %4d ms -> %d tick(s), alpha %.3f\n", milliseconds, ticks,
                    clock.GetInterpolationAlpha());
    }
    std::printf("dropped ticks: %lld\n", static_cast<long long>(clock.GetDroppedTickCount()));
    return 0;
}
