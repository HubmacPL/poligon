# Input, recording and replay

Header: `poligon/core/input.h`. Example: `examples/input`. Pattern in a game: `SetUpToyInput` and `UpdatePlayerInput` in `game/src/toy_world.cpp`.

## Model

- The game does not read the keyboard or mouse. It reads **actions**: named numbers, one value per tick (a button is 0 or 1, an axis any number).
- **Bindings** turn devices into actions: `BindKey(action, key, value)` (keys: letters, digits, arrows, Space, Enter, Tab, Backspace, left Shift, Control and Alt, F1 to F12), `BindMouseButton`, `BindMouseMotion`. Two keys with +1 and -1 on the same action make an axis. Binding values add up, so limiting the range is the game's job.
- The source of values is chosen once per run: real devices (`UseLiveDevices`), a state supplied by hand (`UseManualDevices` + `SetDeviceState`, for tests), a recording (`StartReplay`) or nothing (everything 0).
- `GameLoopConfig::input = &input` makes the loop call `AdvanceTick` before every `FixedUpdate`.
- Reading: `GetValue`, `IsDown` (above 0.5), `WasPressed` and `WasReleased` (only on the tick of the change).

## Rules

1. Add all actions before the first tick and before recording or replay starts.
2. ECS systems do not touch `Input`. Once per tick the game copies actions into a singleton component (`PlayerInput` in the toy), and systems read that component. This puts the tick's input into the state dump.
3. Process window events in `Game::BeginFrame` (`ProcessPlatformEvents`), that is before the frame's ticks.
4. Everything that should replay from a recording must go through actions. Reading a device around `Input` does not reach the recording.

## Program flags

- `--record FILE`: save this run's input.
- `--replay FILE`: take input from a recording instead of devices. Without `--ticks` the run lasts as many ticks as the recording.
- Both together are allowed (replay and record again).

Example: `build\dev\bin\toy.exe --headless --replay game\tests\recordings\walk_jump_look.txt --dump-state state.json --dump-frame frame.png`

## Recording format

Plain text that can be written by hand. `#` starts a comment.

```
poligon-input 1
ticks_per_second 60
action move_x
action jump
tick 30 move_x=1
tick 60 jump=1
tick 62 jump=0
end 480
```

- `tick N name=value ...` gives the values that changed at tick N; a value holds until the next change. Tick numbers must increase.
- `end N` is the number of ticks in the recording; after it all actions return to 0. A file without `end` (cut short by a crash) still loads.
- A file may declare a subset of the game's actions; the others are 0. An action the game does not know is an error, and so is a different tick rate.
- Numbers are written with 9 significant digits, so a `float` comes back exactly.
- The file is written as the run goes and flushed after every change, so it survives a crash of the game.

## Limits

- Keyboard and mouse, no gamepad.
- Key state is sampled once per tick; a press and release within one frame can be missed.
- Mouse motion is read without capturing the cursor.
- A recording holds input only. It assumes the same starting state of the game; it stores neither a seed nor the game version.
