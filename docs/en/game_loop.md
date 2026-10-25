# Game loop and headless mode

Headers: `poligon/core/game_loop.h`, `poligon/core/fixed_step_clock.h`, `poligon/core/command_line.h`, `poligon/core/fatal_error.h`.
Examples: `examples/core/game_loop`, `examples/core/fixed_step_clock`, `examples/core/command_line`.

## Terms

- **Tick**: one simulation step of fixed length, `1 / ticks_per_second` seconds (60 per second by default).
- **Frame**: one call to `Game::Render`.

## Two modes

| | Headless (`headless = true`) | Real time (`headless = false`) |
|---|---|---|
| Wall clock | never read | sets the number of ticks |
| Ticks per frame | exactly 1 | 0 to `max_ticks_per_frame` |
| Speed | as fast as the machine allows | follows real time |
| Repeatability | full | the frame count depends on the machine |

Changes are verified in headless mode.

## Rules

- All simulation state changes happen in `Game::FixedUpdate`. `Render` changes no state.
- Time in the simulation comes only from `TickInfo` (`delta_seconds`, `time_seconds`). Game code must not read the system clock.
- `time_seconds` is computed as `tick_index / ticks_per_second`, not summed, so no rounding error builds up.
- `FrameInfo::delta_seconds` (in `Render`) is the game time the frame's ticks covered: 1/60 s in headless mode, 0 to several ticks in real time. Effects that change over time in the renderer (automatic exposure, `Environment::frame_seconds`) take it from there, so replays match.
- A programmer error (for example an invalid configuration) ends the process through `FatalError`: a `FATAL: ...` line with the call stack on stderr and exit code 3, without a dialog box. Details: [crashes.md](crashes.md).

## Command line

- `--headless`: headless mode.
- `--ticks N`: stop after N ticks (N >= 1). Without it a game with a window runs until the window is closed or Escape is pressed.
- `--backend d3d12|vulkan`, `--gpu-validation`, `--dump-frame FILE.png`: see [rendering.md](rendering.md).
- `--dump-state FILE.json`: see [ecs.md](ecs.md). `--record`, `--replay`: see [input.md](input.md). `--hash-log`, `--hash-interval`: see [determinism.md](determinism.md). `--render-settings`: see [debug_tools.md](debug_tools.md).
- `--quality low|medium|high|ultra` (graphics preset) and `--resolution WxH` (picture size, 16 to 8192 each way): see [rendering.md](rendering.md). Both are only parsed and checked here; the game applies them.
- An unknown option is an error (exit code 2), so a typo cannot pass silently.

Example: `build\dev\bin\toy.exe --headless --ticks 600 --dump-frame frame.png`.

## Window and loop pacing

- With a window, `Renderer::EndFrame` waits for the display refresh (vsync). The game then sets `GameLoopConfig::render_every_iteration = true`, so the loop renders on every iteration, also when no tick was due.
- `ProcessPlatformEvents()` is called once per frame in `Game::BeginFrame` (before the frame's ticks); it returns `false` when the user closed the window or pressed Escape.
- Order within one frame: `BeginFrame`, then for each tick `Input::AdvanceTick` (if `GameLoopConfig::input` is set) and `FixedUpdate`, and finally `Render`.

## Repeatability test

`cmake/CompareTwoRuns.cmake` runs a program twice and requires identical output. The test `toy_headless_is_repeatable` uses it on the toy game.
