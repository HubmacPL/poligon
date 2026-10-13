#pragma once

#include <cstdint>

namespace poligon {

// Converts real elapsed time into a whole number of fixed-length simulation ticks.
// Uses integer arithmetic only, so no time is lost or invented by rounding.
class FixedStepClock {
public:
    // ticks_per_second: simulation rate, for example 60.
    // max_ticks_per_advance: upper limit of ticks returned by one Advance call. Time beyond the
    // limit is dropped, so one very long frame cannot trigger an ever-growing backlog of ticks.
    FixedStepClock(int ticks_per_second, int max_ticks_per_advance);

    // Adds real elapsed time and returns how many ticks should run now.
    // Negative elapsed time is treated as zero.
    int Advance(std::int64_t elapsed_nanoseconds);

    // Fraction of the next tick that has already elapsed, in [0, 1).
    // Rendering can use it to interpolate between the last two simulation states.
    double GetInterpolationAlpha() const;

    // Total number of ticks dropped because of max_ticks_per_advance.
    std::int64_t GetDroppedTickCount() const;

private:
    std::int64_t ticks_per_second_;
    std::int64_t max_ticks_per_advance_;
    // Unit: nanoseconds multiplied by ticks_per_second. One tick equals 1'000'000'000 units.
    std::int64_t accumulator_ = 0;
    std::int64_t dropped_tick_count_ = 0;
};

} // namespace poligon
