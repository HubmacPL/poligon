#pragma once

#include <cstdint>

namespace poligon {

class Input;

// Terms:
//   tick  - one simulation step of fixed length (1 / ticks_per_second seconds).
//   frame - one call to Game::Render.
// In headless mode every frame runs exactly one tick and the wall clock is never read,
// so a run is repeatable and finishes as fast as the machine allows.
// In real-time mode ticks follow the wall clock.

struct GameLoopConfig {
    int ticks_per_second = 60;
    // true: no wall clock, one tick per frame. false: ticks follow real time.
    bool headless = false;
    // Stop after this many ticks. 0 means run until RequestQuit is called.
    std::int64_t max_ticks = 0;
    // Real-time mode only: most ticks run in one frame; time beyond that is dropped.
    int max_ticks_per_frame = 5;
    // Real-time mode only. Set to true when Render blocks until the display refresh (a window
    // with vsync): the loop then renders every iteration, even when no tick was due.
    // When false, the loop renders only after ticks and sleeps in between.
    bool render_every_iteration = false;
    // Optional. When set, the loop calls input->AdvanceTick before every Game::FixedUpdate,
    // so the game always sees the action values that belong to the current tick.
    Input* input = nullptr;
};

struct TickInfo {
    std::int64_t tick_index;  // 0 for the first tick
    double delta_seconds;     // always 1.0 / ticks_per_second
    double time_seconds;      // simulation time at the start of this tick: tick_index / ticks_per_second
};

struct FrameInfo {
    std::int64_t frame_index;    // 0 for the first frame
    std::int64_t tick_count;     // ticks completed so far
    double interpolation_alpha;  // fraction of the next tick already elapsed, in [0, 1); 0 in headless mode
    double delta_seconds;        // game time this frame's ticks covered (0 when none ran); for Environment::frame_seconds
};

struct GameLoopResult {
    std::int64_t tick_count;
    std::int64_t frame_count;
    std::int64_t dropped_tick_count;  // real-time mode only, see max_ticks_per_frame
};

class GameLoop;

// Derive from Game and pass the object to GameLoop::Run.
class Game {
public:
    virtual ~Game() = default;

    // Called once, before the first tick.
    virtual void Start(GameLoop& /*loop*/) {}
    // Called once per frame, before that frame's ticks. The place to process window events
    // (poligon::ProcessPlatformEvents), so that ticks see the newest keyboard and mouse state.
    virtual void BeginFrame(GameLoop& /*loop*/) {}
    // Called once per tick. All simulation state changes belong here.
    virtual void FixedUpdate(GameLoop& loop, const TickInfo& tick) = 0;
    // Called once per frame, after that frame's ticks. Must not change simulation state.
    virtual void Render(GameLoop& /*loop*/, const FrameInfo& /*frame*/) {}
    // Called once, after the last frame.
    virtual void Shutdown(GameLoop& /*loop*/) {}
};

class GameLoop {
public:
    explicit GameLoop(const GameLoopConfig& config);

    // Runs the game until max_ticks is reached or RequestQuit is called.
    GameLoopResult Run(Game& game);

    // Ends the run. No further ticks are executed; the current frame is still rendered.
    void RequestQuit();

    const GameLoopConfig& GetConfig() const;

private:
    GameLoopConfig config_;
    bool quit_requested_ = false;
};

} // namespace poligon
