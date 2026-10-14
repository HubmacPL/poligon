# Determinism and its test

## What is guaranteed

The same input recording replayed by the same executable on the same machine gives an identical game state after every tick.

This is not guaranteed between different builds or machines. Checked: the `dev` (Debug) and `asan` (optimized) builds give the same values of all components but different entity numbers, because the debug and release versions of the flecs library create a different number of entities of their own.

## Requirements

- **Hard:** identical state dump (JSON, byte for byte) and identical state hash after every tick.
- **With tolerance:** the final frame (PNG) matches according to `image_compare`. Bit-for-bit identity is not required, though on one backend we observe it today.

## How to check

Two runs with the same recording:

```
toy --headless --replay RECORDING --dump-state a.json --hash-log a.hashes --hash-interval 1 --dump-frame a.png
toy --headless --replay RECORDING --dump-state b.json --hash-log b.hashes --hash-interval 1 --dump-frame b.png
state_compare a.json b.json --hash-logs a.hashes b.hashes
image_compare a.png b.png
```

- `--hash-log FILE` writes the state hash every `--hash-interval` ticks (60 by default), one line each: the number of completed ticks and the hash.
- `state_compare` exits with code 0 and the word `IDENTICAL`, or with code 1 and a description: after which tick the states were still equal, after which they no longer were, and the first differing line of the dump with the entity header.
- When looking for the cause of a divergence: `--hash-interval 1` gives the exact tick; then `--ticks N --dump-state` for that tick in both runs shows which component differs.

## Tests in `check.ps1`

In `game/CMakeLists.txt`:

- `toy_replay_run_a` (Direct3D 12) and `toy_replay_run_b` (Vulkan) replay `game/tests/recordings/walk_jump_look.txt` in separate processes. The graphics backend must not influence the simulation.
- `toy_replay_state_is_identical`: the hard requirement on these two runs.
- `toy_replay_frame_matches`: the requirement with tolerance.
- `toy_replay_difference_is_detected`: a run with a recording whose second jump is one tick later must be found different, and the tool must name tick 151. This proves that the test can see a difference and locates it correctly.

Related: `tests/ecs_order_probe.cpp` (flecs repeatability), `game/tests/toy_world_test.cpp` (two worlds in one process), `tests/physics_test.cpp` (the physics world alone).

The toy game is simulated by the physics engine, so all of the tests above cover it too; see [physics.md](physics.md) for what keeps physics repeatable.

## What breaks determinism

The rules from [ecs.md](ecs.md), [input.md](input.md) and [game_loop.md](game_loop.md) in one place:

- time other than the tick's `delta` (system clock, frame time);
- input read around `Input`;
- `rand()` or a generator whose state is not in a component;
- simulation state kept outside the ECS (globals, members of the game class);
- changing state in `Render`;
- several threads in flecs;
- systems created after any entity was deleted;
- uninitialized component members;
- library math functions (such as `sin`, `cos`) are repeatable on the same executable but may differ between builds.
