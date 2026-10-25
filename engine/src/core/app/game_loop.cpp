#include "poligon/core/game_loop.h"

#include <chrono>
#include <thread>

#include "poligon/core/fatal_error.h"
#include "poligon/core/fixed_step_clock.h"
#include "poligon/core/input.h"

namespace poligon {

GameLoop::GameLoop(const GameLoopConfig& config) : config_(config) {
    if (config.ticks_per_second < 1) {
        FatalError("GameLoop: ticks_per_second must be at least 1");
    }
    if (config.max_ticks < 0) {
        FatalError("GameLoop: max_ticks must not be negative");
    }
    if (config.max_ticks_per_frame < 1) {
        FatalError("GameLoop: max_ticks_per_frame must be at least 1");
    }
}

void GameLoop::RequestQuit() {
    quit_requested_ = true;
}

const GameLoopConfig& GameLoop::GetConfig() const {
    return config_;
}

GameLoopResult GameLoop::Run(Game& game) {
    using SteadyClock = std::chrono::steady_clock;

    quit_requested_ = false;
    GameLoopResult result{};
    FixedStepClock clock(config_.ticks_per_second, config_.max_ticks_per_frame);
    const double delta_seconds = 1.0 / static_cast<double>(config_.ticks_per_second);

    const auto tick_limit_reached = [&] {
        return config_.max_ticks > 0 && result.tick_count >= config_.max_ticks;
    };

    game.Start(*this);

    SteadyClock::time_point previous_time = SteadyClock::now();
    while (!quit_requested_ && !tick_limit_reached()) {
        int due_ticks = 1;
        if (!config_.headless) {
            const SteadyClock::time_point now = SteadyClock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - previous_time);
            previous_time = now;
            due_ticks = clock.Advance(elapsed.count());
            if (due_ticks == 0 && !config_.render_every_iteration) {
                // Nothing to simulate yet and Render does not pace the loop, so wait instead of spinning.
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
        }

        game.BeginFrame(*this);

        const std::int64_t ticks_before = result.tick_count;
        for (int i = 0; i < due_ticks && !quit_requested_ && !tick_limit_reached(); ++i) {
            if (config_.input != nullptr) {
                config_.input->AdvanceTick(result.tick_count);
            }
            TickInfo tick{};
            tick.tick_index = result.tick_count;
            tick.delta_seconds = delta_seconds;
            tick.time_seconds = static_cast<double>(result.tick_count) * delta_seconds;
            game.FixedUpdate(*this, tick);
            ++result.tick_count;
        }

        FrameInfo frame{};
        frame.frame_index = result.frame_count;
        frame.tick_count = result.tick_count;
        frame.delta_seconds = static_cast<double>(result.tick_count - ticks_before) * delta_seconds;
        frame.interpolation_alpha = config_.headless ? 0.0 : clock.GetInterpolationAlpha();
        game.Render(*this, frame);
        ++result.frame_count;
    }

    game.Shutdown(*this);

    result.dropped_tick_count = clock.GetDroppedTickCount();
    return result;
}

} // namespace poligon
