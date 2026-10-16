// Example: the smallest game. Runs headless for at most 100 ticks and quits itself after 5.
#include <cstdio>

#include "poligon/core/game_loop.h"

class CounterGame : public poligon::Game {
public:
    void Start(poligon::GameLoop& /*loop*/) override { std::printf("start\n"); }

    void FixedUpdate(poligon::GameLoop& loop, const poligon::TickInfo& tick) override {
        std::printf("tick %lld at %.4f s\n", static_cast<long long>(tick.tick_index), tick.time_seconds);
        if (tick.tick_index == 4) {
            loop.RequestQuit();
        }
    }

    void Render(poligon::GameLoop& /*loop*/, const poligon::FrameInfo& frame) override {
        std::printf("frame %lld\n", static_cast<long long>(frame.frame_index));
    }

    void Shutdown(poligon::GameLoop& /*loop*/) override { std::printf("shutdown\n"); }
};

int main() {
    poligon::GameLoopConfig config;
    config.headless = true;  // no wall clock: one tick per frame, as fast as possible
    config.max_ticks = 100;

    CounterGame game;
    poligon::GameLoop loop(config);
    const poligon::GameLoopResult result = loop.Run(game);

    std::printf("ran %lld ticks\n", static_cast<long long>(result.tick_count));
    return result.tick_count == 5 ? 0 : 1;
}
