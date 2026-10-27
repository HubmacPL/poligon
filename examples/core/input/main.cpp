// Example: actions, bindings, recording and replay, without a window.
// Usage: example_input RECORDING_PATH
#include <cstdio>
#include <string>

#include "poligon/core/input.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_input RECORDING_PATH\n");
        return 2;
    }
    const char* recording_path = argv[1];
    const int ticks_per_second = 60;
    std::string error;

    // First run: "play" for 6 ticks and record. Manual devices stand in for a real keyboard here;
    // a game with a window calls UseLiveDevices() instead.
    {
        poligon::Input input;
        const poligon::InputAction move = input.AddAction("move");
        const poligon::InputAction jump = input.AddAction("jump");
        input.BindKey(move, poligon::Key::D, 1.0f);
        input.BindKey(move, poligon::Key::A, -1.0f);
        input.BindKey(jump, poligon::Key::Space);
        // Escape is a key like any other. Under GameLoop it also ends the run unless
        // GameLoopConfig::escape_quits is false; a game with a pause menu sets that and binds
        // Escape to its own action, so that the menu opens in replays too.
        const poligon::InputAction pause = input.AddAction("pause");
        input.BindKey(pause, poligon::Key::Escape);
        input.UseManualDevices();
        if (!input.StartRecording(recording_path, ticks_per_second, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }

        for (int tick = 0; tick < 6; ++tick) {
            poligon::InputDeviceState devices;
            devices.keys[static_cast<int>(poligon::Key::D)] = tick >= 1 && tick <= 3;
            devices.keys[static_cast<int>(poligon::Key::Space)] = tick == 2;
            devices.keys[static_cast<int>(poligon::Key::Escape)] = tick == 4;
            input.SetDeviceState(devices);

            input.AdvanceTick(tick);  // GameLoop does this when GameLoopConfig::input is set
            std::printf("recording tick %d: move=%g jump_pressed=%d pause_pressed=%d\n", tick, input.GetValue(move),
                        input.WasPressed(jump) ? 1 : 0, input.WasPressed(pause) ? 1 : 0);
        }
        if (!input.FinishRecording()) {
            std::fprintf(stderr, "cannot write %s\n", recording_path);
            return 1;
        }
    }

    // Second run: no devices at all, the same values come from the file.
    {
        poligon::Input input;
        const poligon::InputAction move = input.AddAction("move");
        const poligon::InputAction jump = input.AddAction("jump");
        const poligon::InputAction pause = input.AddAction("pause");
        if (!input.StartReplay(recording_path, ticks_per_second, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }

        for (int tick = 0; tick < input.GetReplayTickCount(); ++tick) {
            input.AdvanceTick(tick);
            std::printf("replaying tick %d: move=%g jump_pressed=%d pause_pressed=%d\n", tick, input.GetValue(move),
                        input.WasPressed(jump) ? 1 : 0, input.WasPressed(pause) ? 1 : 0);
        }
    }
    return 0;
}
