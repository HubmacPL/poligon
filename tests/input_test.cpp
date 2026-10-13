// Usage: input_test OUTPUT_DIRECTORY
#include <cstdio>
#include <string>
#include <vector>

#include "poligon/input.h"
#include "test_support.h"

namespace {

struct Actions {
    poligon::InputAction move;
    poligon::InputAction jump;
    poligon::InputAction look;
};

Actions AddActions(poligon::Input& input) {
    Actions actions;
    actions.move = input.AddAction("move");
    actions.jump = input.AddAction("jump");
    actions.look = input.AddAction("look");
    input.BindKey(actions.move, poligon::Key::D, 1.0f);
    input.BindKey(actions.move, poligon::Key::A, -1.0f);
    input.BindKey(actions.jump, poligon::Key::Space);
    input.BindMouseButton(actions.jump, poligon::MouseButton::Left);
    input.BindMouseMotion(actions.look, poligon::MouseAxis::X, 0.5f);
    return actions;
}

// The device state used for a given tick of the scripted "play session".
poligon::InputDeviceState ScriptedDevices(int tick) {
    poligon::InputDeviceState state;
    state.keys[static_cast<int>(poligon::Key::D)] = tick >= 2 && tick < 6;
    state.keys[static_cast<int>(poligon::Key::A)] = tick >= 5 && tick < 8;
    state.keys[static_cast<int>(poligon::Key::Space)] = tick == 3 || tick == 4;
    state.mouse_buttons[static_cast<int>(poligon::MouseButton::Left)] = tick == 9;
    state.mouse_motion_x = tick % 3 == 0 ? 0.1f * static_cast<float>(tick) : 0.0f;
    return state;
}

std::string ReadFile(const std::string& path) {
    std::string content;
    std::FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") == 0 && file != nullptr) {
        char buffer[4096];
        size_t count = 0;
        while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
            content.append(buffer, count);
        }
        std::fclose(file);
    }
    return content;
}

void WriteFile(const std::string& path, const std::string& content) {
    std::FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "wb") == 0 && file != nullptr) {
        std::fwrite(content.data(), 1, content.size(), file);
        std::fclose(file);
    }
}

// Returns the error text of a replay that is expected to fail, or "" if it unexpectedly succeeded.
std::string ReplayError(const std::string& path, const std::string& content) {
    WriteFile(path, content);
    poligon::Input input;
    AddActions(input);
    std::string error;
    if (input.StartReplay(path.c_str(), 60, error)) {
        return "";
    }
    return error;
}

bool Contains(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;
    if (argc != 2) {
        std::printf("usage: input_test OUTPUT_DIRECTORY\n");
        return 2;
    }
    const std::string directory = argv[1];
    const std::string recording_path = directory + "/input_test_recording.txt";
    const std::string rerecording_path = directory + "/input_test_rerecording.txt";
    const std::string bad_path = directory + "/input_test_bad.txt";
    const int tick_count = 12;

    // Play the scripted session with manual devices, recording it. Remember every tick's values.
    std::vector<float> original_values;
    {
        poligon::Input input;
        const Actions actions = AddActions(input);
        input.UseManualDevices();
        std::string error;
        Expect(input.StartRecording(recording_path.c_str(), 60, error), "recording starts");

        for (int tick = 0; tick < tick_count; ++tick) {
            input.SetDeviceState(ScriptedDevices(tick));
            input.AdvanceTick(tick);
            original_values.push_back(input.GetValue(actions.move));
            original_values.push_back(input.GetValue(actions.jump));
            original_values.push_back(input.GetValue(actions.look));

            if (tick == 1) {
                Expect(input.GetValue(actions.move) == 0.0f && !input.IsDown(actions.jump), "nothing held: all zero");
            }
            if (tick == 2) {
                Expect(input.GetValue(actions.move) == 1.0f, "D gives +1");
            }
            if (tick == 5) {
                Expect(input.GetValue(actions.move) == 0.0f, "D and A together cancel out");
            }
            if (tick == 6) {
                Expect(input.GetValue(actions.move) == -1.0f, "A gives -1");
            }
            if (tick == 3) {
                Expect(input.IsDown(actions.jump) && input.WasPressed(actions.jump), "pressed on the first tick down");
            }
            if (tick == 4) {
                Expect(input.IsDown(actions.jump) && !input.WasPressed(actions.jump), "held is not pressed again");
            }
            if (tick == 5) {
                Expect(!input.IsDown(actions.jump) && input.WasReleased(actions.jump), "released on the tick it went up");
            }
            if (tick == 9) {
                Expect(input.WasPressed(actions.jump), "a second binding (mouse button) drives the same action");
                Expect(input.GetValue(actions.look) == ScriptedDevices(9).mouse_motion_x * 0.5f,
                       "mouse motion is scaled by the binding");
            }
        }
        Expect(input.FinishRecording(), "recording is written completely");
    }

    const std::string recording = ReadFile(recording_path);
    Expect(Contains(recording, "poligon-input 1\nticks_per_second 60\naction move\naction jump\naction look\n"),
           "the file starts with the header and the action list");
    Expect(Contains(recording, "tick 2 move=1\n"), "a change is written on its tick");
    Expect(!Contains(recording, "tick 1 "), "a tick without changes is not written");
    Expect(Contains(recording, "end 12\n"), "the file ends with the tick count");

    // Replay it, recording again: every tick must give the same values, and the second file must be identical.
    {
        poligon::Input input;
        const Actions actions = AddActions(input);
        std::string error;
        Expect(input.StartReplay(recording_path.c_str(), 60, error), "replay starts");
        Expect(input.GetReplayTickCount() == tick_count, "replay knows its length");
        Expect(input.StartRecording(rerecording_path.c_str(), 60, error), "recording during replay starts");

        // Devices are ignored during replay, even when set.
        poligon::InputDeviceState noise;
        noise.keys[static_cast<int>(poligon::Key::A)] = true;
        input.SetDeviceState(noise);

        bool all_equal = true;
        bool pressed_seen = false;
        for (int tick = 0; tick < tick_count; ++tick) {
            input.AdvanceTick(tick);
            const size_t base = static_cast<size_t>(tick) * 3;
            all_equal = all_equal && input.GetValue(actions.move) == original_values[base] &&
                        input.GetValue(actions.jump) == original_values[base + 1] &&
                        input.GetValue(actions.look) == original_values[base + 2];
            pressed_seen = pressed_seen || (tick == 3 && input.WasPressed(actions.jump));
        }
        Expect(all_equal, "replay gives exactly the recorded values on every tick");
        Expect(pressed_seen, "WasPressed works during replay");

        input.AdvanceTick(tick_count);
        Expect(input.GetValue(actions.move) == 0.0f && input.GetValue(actions.look) == 0.0f,
               "after the end of the recording everything is zero");
    }
    {
        const std::string rerecording = ReadFile(rerecording_path);
        const std::string expected_start = recording.substr(0, recording.rfind("end "));
        Expect(rerecording.compare(0, expected_start.size(), expected_start) == 0,
               "recording a replay reproduces the original file up to its end line");
    }

    // A hand-written recording: comments, blank lines, a subset of actions.
    {
        WriteFile(bad_path, "# written by hand\npoligon-input 1\nticks_per_second 60\n\naction jump\n"
                            "tick 4 jump=1   # press\ntick 5 jump=0\nend 10\n");
        poligon::Input input;
        const Actions actions = AddActions(input);
        std::string error;
        Expect(input.StartReplay(bad_path.c_str(), 60, error), "a hand-written file with a subset of actions loads");
        for (int tick = 0; tick <= 4; ++tick) {
            input.AdvanceTick(tick);
        }
        Expect(input.WasPressed(actions.jump) && input.GetValue(actions.move) == 0.0f,
               "its values arrive on the right tick; undeclared actions stay zero");
    }

    // Malformed files are rejected with a message that names the problem.
    const std::string header = "poligon-input 1\nticks_per_second 60\naction move\n";
    Expect(Contains(ReplayError(bad_path, "something else\n"), "first line"), "wrong first line");
    Expect(Contains(ReplayError(bad_path, "poligon-input 1\nticks_per_second 30\n"), "30 ticks per second"),
           "wrong tick rate");
    Expect(Contains(ReplayError(bad_path, "poligon-input 1\naction move\nend 5\n"), "ticks_per_second"),
           "missing tick rate");
    Expect(Contains(ReplayError(bad_path, header + "action fly\n"), "no action named \"fly\""), "unknown action");
    Expect(Contains(ReplayError(bad_path, header + "tick 3 jump=1\n"), "no \"action\" line"), "undeclared action");
    Expect(Contains(ReplayError(bad_path, header + "tick 3 move=1\ntick 3 move=0\n"), "must increase"),
           "repeated tick");
    Expect(Contains(ReplayError(bad_path, header + "tick 3 move=fast\n"), "<action>=<value>"), "bad number");
    Expect(Contains(ReplayError(bad_path, header + "tick 3 move=1\nend 2\n"), "end"), "end before the last tick");
    Expect(Contains(ReplayError(bad_path, header + "jump 3\n"), "unknown statement"), "unknown statement");
    Expect(ReplayError(bad_path, header + "tick 3 move=1\n").empty(),
           "a file without an end line (cut short by a crash) still loads");
    {
        poligon::Input input;
        AddActions(input);
        std::string error;
        Expect(!input.StartReplay((directory + "/does_not_exist.txt").c_str(), 60, error) &&
                   Contains(error, "cannot open"),
               "a missing file is reported");
    }

    return test::ExitCode();
}
