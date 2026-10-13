#include <cstdint>

#include "poligon/fixed_step_clock.h"
#include "test_support.h"

namespace {
constexpr std::int64_t kMillisecond = 1'000'000;
constexpr std::int64_t kSecond = 1'000'000'000;
} // namespace

int main() {
    using poligon::FixedStepClock;
    using test::Expect;

    {
        FixedStepClock clock(60, 5);
        Expect(clock.Advance(0) == 0, "zero elapsed time gives no ticks");
        Expect(clock.Advance(-5 * kMillisecond) == 0, "negative elapsed time gives no ticks");
        Expect(clock.Advance(10 * kMillisecond) == 0, "10 ms is less than one 60 Hz tick");
        Expect(clock.Advance(10 * kMillisecond) == 1, "20 ms in total gives one tick");
        Expect(clock.GetInterpolationAlpha() > 0.19 && clock.GetInterpolationAlpha() < 0.21,
               "20 ms is 1.2 ticks, so alpha is about 0.2");
    }

    {
        // One second fed in 1 ms pieces must give exactly 60 ticks: no time lost to rounding.
        FixedStepClock clock(60, 5);
        int total_ticks = 0;
        for (int i = 0; i < 1000; ++i) {
            total_ticks += clock.Advance(kMillisecond);
        }
        Expect(total_ticks == 60, "1000 steps of 1 ms give exactly 60 ticks");
        Expect(clock.GetInterpolationAlpha() == 0.0, "nothing is left over after a whole second");
        Expect(clock.GetDroppedTickCount() == 0, "no ticks dropped in normal operation");
    }

    {
        // A one second hitch at 60 Hz is 60 ticks; the limit of 5 drops the other 55.
        FixedStepClock clock(60, 5);
        Expect(clock.Advance(kSecond) == 5, "ticks per advance are limited");
        Expect(clock.GetDroppedTickCount() == 55, "ticks beyond the limit are counted as dropped");
        Expect(clock.Advance(0) == 0, "dropped time does not come back later");
    }

    return test::ExitCode();
}
