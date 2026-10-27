#include "poligon/core/input.h"

#include <charconv>
#include <cstring>
#include <sstream>

#include <SDL3/SDL.h>
#include <imgui.h>

#include "poligon/core/fatal_error.h"

namespace poligon {

namespace {

// Recording file format, version 1 (plain text, one statement per line, '#' starts a comment):
//   poligon-input 1
//   ticks_per_second 60
//   action <name>              one line per action
//   tick <n> <name>=<value>... values that changed at tick n; a value stays until changed again
//   end <tick count>           total number of ticks in the recording
constexpr const char* kFormatName = "poligon-input";
constexpr int kFormatVersion = 1;

const SDL_Scancode kKeyToScancode[static_cast<int>(Key::Count)] = {
    SDL_SCANCODE_A,     SDL_SCANCODE_B,     SDL_SCANCODE_C,         SDL_SCANCODE_D,      SDL_SCANCODE_E,
    SDL_SCANCODE_F,     SDL_SCANCODE_G,     SDL_SCANCODE_H,         SDL_SCANCODE_I,      SDL_SCANCODE_J,
    SDL_SCANCODE_K,     SDL_SCANCODE_L,     SDL_SCANCODE_M,         SDL_SCANCODE_N,      SDL_SCANCODE_O,
    SDL_SCANCODE_P,     SDL_SCANCODE_Q,     SDL_SCANCODE_R,         SDL_SCANCODE_S,      SDL_SCANCODE_T,
    SDL_SCANCODE_U,     SDL_SCANCODE_V,     SDL_SCANCODE_W,         SDL_SCANCODE_X,      SDL_SCANCODE_Y,
    SDL_SCANCODE_Z,     SDL_SCANCODE_0,     SDL_SCANCODE_1,         SDL_SCANCODE_2,      SDL_SCANCODE_3,
    SDL_SCANCODE_4,     SDL_SCANCODE_5,     SDL_SCANCODE_6,         SDL_SCANCODE_7,      SDL_SCANCODE_8,
    SDL_SCANCODE_9,     SDL_SCANCODE_LEFT,  SDL_SCANCODE_RIGHT,     SDL_SCANCODE_UP,     SDL_SCANCODE_DOWN,
    SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN, SDL_SCANCODE_TAB,      SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_LSHIFT,
    SDL_SCANCODE_LCTRL, SDL_SCANCODE_LALT,
    SDL_SCANCODE_F1,    SDL_SCANCODE_F2,    SDL_SCANCODE_F3,        SDL_SCANCODE_F4,     SDL_SCANCODE_F5,
    SDL_SCANCODE_F6,    SDL_SCANCODE_F7,    SDL_SCANCODE_F8,        SDL_SCANCODE_F9,     SDL_SCANCODE_F10,
    SDL_SCANCODE_F11,   SDL_SCANCODE_F12,   SDL_SCANCODE_ESCAPE,
};

void ReadLiveDeviceState(InputDeviceState& state) {
    state = InputDeviceState{};
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        return;
    }

    int key_count = 0;
    const bool* keys = SDL_GetKeyboardState(&key_count);
    for (int key = 0; key < static_cast<int>(Key::Count); ++key) {
        const int scancode = static_cast<int>(kKeyToScancode[key]);
        state.keys[key] = scancode < key_count && keys[scancode];
    }

    // Relative state is the movement accumulated since the previous call, which is once per tick.
    const SDL_MouseButtonFlags buttons = SDL_GetRelativeMouseState(&state.mouse_motion_x, &state.mouse_motion_y);
    state.mouse_buttons[static_cast<int>(MouseButton::Left)] = (buttons & SDL_BUTTON_LMASK) != 0;
    state.mouse_buttons[static_cast<int>(MouseButton::Right)] = (buttons & SDL_BUTTON_RMASK) != 0;
    state.mouse_buttons[static_cast<int>(MouseButton::Middle)] = (buttons & SDL_BUTTON_MMASK) != 0;

    // While a debug window (poligon/debug/debug_ui.h) is using the mouse or the keyboard, the
    // game sees them as untouched: dragging a slider must not also turn the camera.
    if (ImGui::GetCurrentContext() != nullptr) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            state.mouse_motion_x = 0.0f;
            state.mouse_motion_y = 0.0f;
            for (bool& button : state.mouse_buttons) {
                button = false;
            }
        }
        if (io.WantCaptureKeyboard) {
            for (bool& key : state.keys) {
                key = false;
            }
        }
    }
}

bool IsValidActionName(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    for (const char* c = name; *c != '\0'; ++c) {
        const bool letter = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z');
        const bool digit = *c >= '0' && *c <= '9';
        if (!letter && !digit && *c != '_') {
            return false;
        }
    }
    return true;
}

// 9 significant digits are enough to read a float back exactly.
std::string FormatFloat(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    return buffer;
}

template <typename T>
bool ParseNumber(const std::string& text, T& value) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end && !text.empty();
}

std::vector<std::string> SplitWords(const std::string& line) {
    std::vector<std::string> words;
    std::istringstream stream(line);
    std::string word;
    while (stream >> word) {
        words.push_back(word);
    }
    return words;
}

} // namespace

Input::Input() = default;

Input::~Input() {
    FinishRecording();
}

InputAction Input::AddAction(const char* name) {
    if (next_tick_ != 0 || recording_file_ != nullptr || source_ == Source::Replay) {
        FatalError("Input::AddAction: add all actions before recording, replay and the first tick");
    }
    if (!IsValidActionName(name)) {
        FatalError("Input::AddAction: a name may contain only letters, digits and '_'");
    }
    for (const std::string& existing : action_names_) {
        if (existing == name) {
            FatalError("Input::AddAction: an action with this name already exists");
        }
    }
    action_names_.emplace_back(name);
    values_.push_back(0.0f);
    previous_values_.push_back(0.0f);

    InputAction action;
    action.index = static_cast<int>(action_names_.size()) - 1;
    return action;
}

int Input::CheckAction(InputAction action, const char* function_name) const {
    if (action.index < 0 || action.index >= static_cast<int>(action_names_.size())) {
        const std::string message = std::string(function_name) + ": invalid action";
        FatalError(message.c_str());
    }
    return action.index;
}

void Input::BindKey(InputAction action, Key key, float value) {
    const int index = CheckAction(action, "Input::BindKey");
    if (key == Key::Count) {
        FatalError("Input::BindKey: invalid key");
    }
    bindings_.push_back(Binding{index, BindingKind::Key, static_cast<int>(key), value});
}

void Input::BindMouseButton(InputAction action, MouseButton button, float value) {
    const int index = CheckAction(action, "Input::BindMouseButton");
    if (button == MouseButton::Count) {
        FatalError("Input::BindMouseButton: invalid button");
    }
    bindings_.push_back(Binding{index, BindingKind::MouseButton, static_cast<int>(button), value});
}

void Input::BindMouseMotion(InputAction action, MouseAxis axis, float scale) {
    const int index = CheckAction(action, "Input::BindMouseMotion");
    const BindingKind kind = axis == MouseAxis::X ? BindingKind::MouseMotionX : BindingKind::MouseMotionY;
    bindings_.push_back(Binding{index, kind, 0, scale});
}

void Input::UseLiveDevices() {
    source_ = Source::Live;
}

void Input::UseManualDevices() {
    source_ = Source::Manual;
}

void Input::SetDeviceState(const InputDeviceState& state) {
    manual_state_ = state;
}

bool Input::StartReplay(const char* path, int ticks_per_second, std::string& error) {
    if (next_tick_ != 0) {
        FatalError("Input::StartReplay: start the replay before the first tick");
    }

    std::FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || file == nullptr) {
        error = std::string("cannot open ") + path;
        return false;
    }
    std::string content;
    char buffer[4096];
    size_t read_count = 0;
    while ((read_count = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        content.append(buffer, read_count);
    }
    std::fclose(file);

    std::vector<ReplayEvent> events;
    std::vector<std::string> file_actions;
    std::int64_t tick_count = -1;
    std::int64_t last_tick = -1;
    bool has_header = false;
    bool has_tick_rate = false;

    std::istringstream stream(content);
    std::string line;
    int line_number = 0;
    const auto fail = [&](const std::string& message) {
        error = std::string(path) + ":" + std::to_string(line_number) + ": " + message;
        return false;
    };

    while (std::getline(stream, line)) {
        ++line_number;
        const size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        const std::vector<std::string> words = SplitWords(line);
        if (words.empty()) {
            continue;
        }

        if (!has_header) {
            int version = 0;
            if (words.size() != 2 || words[0] != kFormatName || !ParseNumber(words[1], version) ||
                version != kFormatVersion) {
                return fail("expected the first line to be \"poligon-input 1\"");
            }
            has_header = true;
            continue;
        }
        if (tick_count >= 0) {
            return fail("nothing may follow the \"end\" line");
        }

        if (words[0] == "ticks_per_second") {
            int file_rate = 0;
            if (words.size() != 2 || !ParseNumber(words[1], file_rate)) {
                return fail("expected \"ticks_per_second <number>\"");
            }
            if (file_rate != ticks_per_second) {
                return fail("recorded at " + std::to_string(file_rate) + " ticks per second, but the game runs at " +
                            std::to_string(ticks_per_second));
            }
            has_tick_rate = true;
        } else if (words[0] == "action") {
            if (words.size() != 2) {
                return fail("expected \"action <name>\"");
            }
            bool known = false;
            for (const std::string& name : action_names_) {
                known = known || name == words[1];
            }
            if (!known) {
                return fail("the game has no action named \"" + words[1] + "\"");
            }
            file_actions.push_back(words[1]);
        } else if (words[0] == "tick") {
            std::int64_t tick = 0;
            if (words.size() < 3 || !ParseNumber(words[1], tick) || tick < 0) {
                return fail("expected \"tick <number> <action>=<value> ...\"");
            }
            if (tick <= last_tick) {
                return fail("tick numbers must increase");
            }
            last_tick = tick;
            for (size_t i = 2; i < words.size(); ++i) {
                const size_t equals = words[i].find('=');
                float value = 0.0f;
                if (equals == std::string::npos || !ParseNumber(words[i].substr(equals + 1), value)) {
                    return fail("expected <action>=<value>, got \"" + words[i] + "\"");
                }
                const std::string name = words[i].substr(0, equals);
                bool declared = false;
                for (const std::string& file_action : file_actions) {
                    declared = declared || file_action == name;
                }
                if (!declared) {
                    return fail("action \"" + name + "\" is used but has no \"action\" line");
                }
                int action_index = 0;
                while (action_names_[static_cast<size_t>(action_index)] != name) {
                    ++action_index;
                }
                events.push_back(ReplayEvent{tick, action_index, value});
            }
        } else if (words[0] == "end") {
            if (words.size() != 2 || !ParseNumber(words[1], tick_count) || tick_count <= last_tick) {
                return fail("expected \"end <tick count>\" with a count above the last tick");
            }
        } else {
            return fail("unknown statement \"" + words[0] + "\"");
        }
    }

    line_number = 0;
    if (!has_header) {
        return fail("the file is empty");
    }
    if (!has_tick_rate) {
        return fail("missing \"ticks_per_second\" line");
    }
    if (tick_count < 0) {
        // A recording cut short by a crash has no "end" line; it lasts until its last change.
        tick_count = last_tick + 1;
    }

    replay_events_ = events;
    replay_cursor_ = 0;
    replay_tick_count_ = tick_count;
    source_ = Source::Replay;
    error.clear();
    return true;
}

std::int64_t Input::GetReplayTickCount() const {
    return source_ == Source::Replay ? replay_tick_count_ : 0;
}

bool Input::StartRecording(const char* path, int ticks_per_second, std::string& error) {
    if (next_tick_ != 0 || recording_file_ != nullptr) {
        FatalError("Input::StartRecording: start one recording, before the first tick");
    }
    if (fopen_s(&recording_file_, path, "wb") != 0 || recording_file_ == nullptr) {
        recording_file_ = nullptr;
        error = std::string("cannot create ") + path;
        return false;
    }

    std::string header = std::string(kFormatName) + " " + std::to_string(kFormatVersion) + "\n";
    header += "ticks_per_second " + std::to_string(ticks_per_second) + "\n";
    for (const std::string& name : action_names_) {
        header += "action " + name + "\n";
    }
    recording_failed_ = std::fputs(header.c_str(), recording_file_) < 0;
    recorded_values_.assign(action_names_.size(), 0.0f);
    error.clear();
    return true;
}

void Input::WriteRecordedTick(std::int64_t tick_index) {
    std::string line;
    for (size_t i = 0; i < values_.size(); ++i) {
        // Compare bit patterns, so that every change is recorded, including -0 and NaN.
        if (std::memcmp(&values_[i], &recorded_values_[i], sizeof(float)) != 0) {
            line += " " + action_names_[i] + "=" + FormatFloat(values_[i]);
            recorded_values_[i] = values_[i];
        }
    }
    if (line.empty()) {
        return;
    }
    line = "tick " + std::to_string(tick_index) + line + "\n";
    if (std::fputs(line.c_str(), recording_file_) < 0 || std::fflush(recording_file_) != 0) {
        recording_failed_ = true;
    }
}

bool Input::FinishRecording() {
    if (recording_file_ == nullptr) {
        return !recording_failed_;
    }
    const std::string line = "end " + std::to_string(next_tick_) + "\n";
    if (std::fputs(line.c_str(), recording_file_) < 0) {
        recording_failed_ = true;
    }
    if (std::fclose(recording_file_) != 0) {
        recording_failed_ = true;
    }
    recording_file_ = nullptr;
    return !recording_failed_;
}

void Input::ComputeFromDevices(const InputDeviceState& state) {
    for (float& value : values_) {
        value = 0.0f;
    }
    for (const Binding& binding : bindings_) {
        float contribution = 0.0f;
        switch (binding.kind) {
        case BindingKind::Key:
            contribution = state.keys[binding.code] ? binding.value : 0.0f;
            break;
        case BindingKind::MouseButton:
            contribution = state.mouse_buttons[binding.code] ? binding.value : 0.0f;
            break;
        case BindingKind::MouseMotionX:
            contribution = state.mouse_motion_x * binding.value;
            break;
        case BindingKind::MouseMotionY:
            contribution = state.mouse_motion_y * binding.value;
            break;
        }
        values_[static_cast<size_t>(binding.action)] += contribution;
    }
}

void Input::AdvanceTick(std::int64_t tick_index) {
    if (tick_index != next_tick_) {
        FatalError("Input::AdvanceTick: ticks must be advanced in order, starting at 0");
    }
    previous_values_ = values_;

    switch (source_) {
    case Source::None:
        break;
    case Source::Live: {
        InputDeviceState state;
        ReadLiveDeviceState(state);
        ComputeFromDevices(state);
        break;
    }
    case Source::Manual:
        ComputeFromDevices(manual_state_);
        break;
    case Source::Replay:
        // Recorded values stay until the recording changes them. After its end everything is 0.
        if (tick_index >= replay_tick_count_) {
            for (float& value : values_) {
                value = 0.0f;
            }
        }
        while (replay_cursor_ < replay_events_.size() && replay_events_[replay_cursor_].tick <= tick_index) {
            const ReplayEvent& event = replay_events_[replay_cursor_];
            if (tick_index < replay_tick_count_) {
                values_[static_cast<size_t>(event.action)] = event.value;
            }
            ++replay_cursor_;
        }
        break;
    }

    if (recording_file_ != nullptr) {
        WriteRecordedTick(tick_index);
    }
    next_tick_ = tick_index + 1;
}

float Input::GetValue(InputAction action) const {
    return values_[static_cast<size_t>(CheckAction(action, "Input::GetValue"))];
}

bool Input::IsDown(InputAction action) const {
    return GetValue(action) > 0.5f;
}

bool Input::WasPressed(InputAction action) const {
    const size_t index = static_cast<size_t>(CheckAction(action, "Input::WasPressed"));
    return values_[index] > 0.5f && previous_values_[index] <= 0.5f;
}

bool Input::WasReleased(InputAction action) const {
    const size_t index = static_cast<size_t>(CheckAction(action, "Input::WasReleased"));
    return values_[index] <= 0.5f && previous_values_[index] > 0.5f;
}

int Input::GetActionCount() const {
    return static_cast<int>(action_names_.size());
}

const char* Input::GetActionName(InputAction action) const {
    return action_names_[static_cast<size_t>(CheckAction(action, "Input::GetActionName"))].c_str();
}

} // namespace poligon
