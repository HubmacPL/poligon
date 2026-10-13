#include <chrono>
#include <cstdint>
#include <string>

#include "poligon/game_loop.h"
#include "test_support.h"

namespace {

// Records the order of callbacks as a string: S = Start, T = tick, R = render, E = Shutdown.
class RecordingGame : public poligon::Game {
public:
    std::string calls;
    std::int64_t quit_at_tick = -1;
    bool tick_indices_in_order = true;
    bool tick_times_correct = true;
    std::int64_t expected_tick_index = 0;

    void Start(poligon::GameLoop& /*loop*/) override { calls += 'S'; }

    void FixedUpdate(poligon::GameLoop& loop, const poligon::TickInfo& tick) override {
        calls += 'T';
        if (tick.tick_index != expected_tick_index) {
            tick_indices_in_order = false;
        }
        const double ticks_per_second = static_cast<double>(loop.GetConfig().ticks_per_second);
        if (tick.delta_seconds != 1.0 / ticks_per_second ||
            tick.time_seconds != static_cast<double>(tick.tick_index) * tick.delta_seconds) {
            tick_times_correct = false;
        }
        ++expected_tick_index;
        if (tick.tick_index == quit_at_tick) {
            loop.RequestQuit();
        }
    }

    void Render(poligon::GameLoop& /*loop*/, const poligon::FrameInfo& /*frame*/) override { calls += 'R'; }

    void Shutdown(poligon::GameLoop& /*loop*/) override { calls += 'E'; }
};

} // namespace

int main() {
    using test::Expect;

    {
        poligon::GameLoopConfig config;
        config.headless = true;
        config.max_ticks = 3;
        RecordingGame game;
        poligon::GameLoop loop(config);
        const poligon::GameLoopResult result = loop.Run(game);

        Expect(game.calls == "STRTRTRE", "headless: start, then tick and render alternate, then shutdown");
        Expect(result.tick_count == 3 && result.frame_count == 3, "headless: one frame per tick");
        Expect(result.dropped_tick_count == 0, "headless: never drops ticks");
        Expect(game.tick_indices_in_order, "tick_index counts 0, 1, 2");
        Expect(game.tick_times_correct, "delta and time follow ticks_per_second exactly");
    }

    {
        poligon::GameLoopConfig config;
        config.headless = true;
        config.max_ticks = 100;
        RecordingGame game;
        game.quit_at_tick = 1;
        poligon::GameLoop loop(config);
        const poligon::GameLoopResult result = loop.Run(game);

        Expect(result.tick_count == 2, "RequestQuit stops before the next tick");
        Expect(game.calls == "STRTRE", "the frame in which quit was requested is still rendered");
    }

    {
        // Real-time mode: 30 ticks at 60 Hz cannot finish in less than half a second.
        // Only the lower bound is checked; an upper bound would fail on a busy machine.
        poligon::GameLoopConfig config;
        config.headless = false;
        config.max_ticks = 30;
        RecordingGame game;
        poligon::GameLoop loop(config);

        const auto start = std::chrono::steady_clock::now();
        const poligon::GameLoopResult result = loop.Run(game);
        const auto elapsed = std::chrono::steady_clock::now() - start;

        Expect(result.tick_count == 30, "real time: stops at max_ticks");
        Expect(result.frame_count >= 1 && result.frame_count <= 30, "real time: at most one frame per tick");
        Expect(elapsed >= std::chrono::milliseconds(500), "real time: ticks follow the wall clock");
        Expect(game.tick_indices_in_order, "real time: tick_index still counts without gaps");
    }

    return test::ExitCode();
}
