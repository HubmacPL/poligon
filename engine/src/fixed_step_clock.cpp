#include "poligon/fixed_step_clock.h"

#include "poligon/fatal_error.h"

namespace poligon {

namespace {
constexpr std::int64_t kUnitsPerTick = 1'000'000'000;
} // namespace

FixedStepClock::FixedStepClock(int ticks_per_second, int max_ticks_per_advance)
    : ticks_per_second_(ticks_per_second), max_ticks_per_advance_(max_ticks_per_advance) {
    if (ticks_per_second < 1) {
        FatalError("FixedStepClock: ticks_per_second must be at least 1");
    }
    if (max_ticks_per_advance < 1) {
        FatalError("FixedStepClock: max_ticks_per_advance must be at least 1");
    }
}

int FixedStepClock::Advance(std::int64_t elapsed_nanoseconds) {
    if (elapsed_nanoseconds <= 0) {
        return 0;
    }

    accumulator_ += elapsed_nanoseconds * ticks_per_second_;
    std::int64_t due_ticks = accumulator_ / kUnitsPerTick;
    accumulator_ %= kUnitsPerTick;

    if (due_ticks > max_ticks_per_advance_) {
        dropped_tick_count_ += due_ticks - max_ticks_per_advance_;
        due_ticks = max_ticks_per_advance_;
    }
    return static_cast<int>(due_ticks);
}

double FixedStepClock::GetInterpolationAlpha() const {
    return static_cast<double>(accumulator_) / static_cast<double>(kUnitsPerTick);
}

std::int64_t FixedStepClock::GetDroppedTickCount() const {
    return dropped_tick_count_;
}

} // namespace poligon
