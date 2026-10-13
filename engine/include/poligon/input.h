#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace poligon {

// Input for the simulation is a set of named ACTIONS, each a single number per tick
// (0 or 1 for a button, any value for an axis). The game reads actions, never devices.
// Where the numbers come from is chosen once per run:
//   - live devices: keyboard and mouse, through the bindings below;
//   - manual: the program supplies the device state itself (used by tests);
//   - replay: a recording file; devices are ignored;
//   - none of the above: every action stays 0.
// Because the simulation only ever sees the per-tick action values, replaying a recording
// gives it exactly the same input as the original run.

enum class Key {
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    Left, Right, Up, Down,
    Space, Enter, Tab, Backspace, LeftShift, LeftControl, LeftAlt,
    Count
};

enum class MouseButton { Left, Right, Middle, Count };

enum class MouseAxis { X, Y };

// State of the devices at one moment. Mouse motion is the movement since the previous tick, in pixels.
struct InputDeviceState {
    bool keys[static_cast<int>(Key::Count)] = {};
    bool mouse_buttons[static_cast<int>(MouseButton::Count)] = {};
    float mouse_motion_x = 0.0f;
    float mouse_motion_y = 0.0f;
};

// Identifies an action created by Input::AddAction.
struct InputAction {
    int index = -1;
};

class Input {
public:
    Input();
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    // --- Setup, before the first tick -------------------------------------------------

    // Names may contain letters, digits and '_'. They identify actions in recording files.
    InputAction AddAction(const char* name);

    // While the key or button is down it adds `value` to the action. Bind two keys with
    // +1 and -1 to the same action to make an axis.
    void BindKey(InputAction action, Key key, float value = 1.0f);
    void BindMouseButton(InputAction action, MouseButton button, float value = 1.0f);
    // Adds mouse movement since the previous tick, in pixels, multiplied by `scale`.
    void BindMouseMotion(InputAction action, MouseAxis axis, float scale = 1.0f);

    // Read the real keyboard and mouse every tick. Needs a Renderer with a window.
    void UseLiveDevices();
    // Use the state given to SetDeviceState instead of real devices.
    void UseManualDevices();
    void SetDeviceState(const InputDeviceState& state);

    // Take action values from a recording instead of devices. Fails (returns false, fills
    // `error`) if the file is malformed, was recorded at another tick rate, or names an
    // action this game does not have.
    bool StartReplay(const char* path, int ticks_per_second, std::string& error);
    // Number of ticks in the recording being replayed, 0 when not replaying.
    std::int64_t GetReplayTickCount() const;

    // Write every tick's action values to a file. Works together with any source, also replay.
    // The file is flushed as it is written, so it survives a crash of the game.
    bool StartRecording(const char* path, int ticks_per_second, std::string& error);
    // Completes the recording file. Called automatically by the destructor.
    // Returns false if the file could not be written completely.
    bool FinishRecording();

    // --- Every tick ---------------------------------------------------------------------

    // Computes the action values for the given tick. GameLoop calls this before
    // Game::FixedUpdate when GameLoopConfig::input is set; ticks must be 0, 1, 2, ...
    void AdvanceTick(std::int64_t tick_index);

    float GetValue(InputAction action) const;
    // True while the value is above 0.5.
    bool IsDown(InputAction action) const;
    // True only on the tick the action went down / up.
    bool WasPressed(InputAction action) const;
    bool WasReleased(InputAction action) const;

    int GetActionCount() const;
    const char* GetActionName(InputAction action) const;

private:
    enum class Source { None, Live, Manual, Replay };
    enum class BindingKind { Key, MouseButton, MouseMotionX, MouseMotionY };

    struct Binding {
        int action;
        BindingKind kind;
        int code;  // Key or MouseButton as a number; unused for motion
        float value;
    };

    struct ReplayEvent {
        std::int64_t tick;
        int action;
        float value;
    };

    int CheckAction(InputAction action, const char* function_name) const;
    void ComputeFromDevices(const InputDeviceState& state);
    void WriteRecordedTick(std::int64_t tick_index);

    std::vector<std::string> action_names_;
    std::vector<Binding> bindings_;
    std::vector<float> values_;
    std::vector<float> previous_values_;
    Source source_ = Source::None;
    InputDeviceState manual_state_;
    std::int64_t next_tick_ = 0;

    std::vector<ReplayEvent> replay_events_;
    size_t replay_cursor_ = 0;
    std::int64_t replay_tick_count_ = 0;

    std::FILE* recording_file_ = nullptr;
    std::vector<float> recorded_values_;
    bool recording_failed_ = false;
};

} // namespace poligon
