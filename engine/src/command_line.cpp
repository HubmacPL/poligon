#include "poligon/command_line.h"

#include <charconv>
#include <cstring>
#include <string>


namespace poligon {

namespace {

// Returns the argument after option argv[index] and moves index onto it.
// Returns nullptr and fills `error` when the option is the last argument.
const char* ReadOptionValue(int argc, const char* const* argv, int& index, std::string& error) {
    if (index + 1 >= argc) {
        error = std::string(argv[index]) + " needs a value";
        return nullptr;
    }
    ++index;
    return argv[index];
}

} // namespace

bool ParseCommandLine(int argc, const char* const* argv, CommandLineOptions& options, std::string& error) {
    options = CommandLineOptions{};
    error.clear();

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];

        if (argument == "--headless") {
            options.headless = true;
        } else if (argument == "--gpu-validation") {
            options.gpu_validation = true;
        } else if (argument == "--ticks" || argument == "--hash-interval") {
            const char* value = ReadOptionValue(argc, argv, i, error);
            if (value == nullptr) {
                return false;
            }
            const char* value_end = value + std::strlen(value);
            std::int64_t number = 0;
            const std::from_chars_result parsed = std::from_chars(value, value_end, number);
            if (parsed.ec != std::errc{} || parsed.ptr != value_end || number < 1) {
                error = argument + " needs a whole number of at least 1, got \"" + value + "\"";
                return false;
            }
            if (argument == "--ticks") {
                options.ticks = number;
            } else {
                options.hash_interval = number;
            }
        } else if (argument == "--backend") {
            const char* value = ReadOptionValue(argc, argv, i, error);
            if (value == nullptr) {
                return false;
            }
            if (std::strcmp(value, "d3d12") == 0) {
                options.backend = GraphicsBackend::Direct3D12;
            } else if (std::strcmp(value, "vulkan") == 0) {
                options.backend = GraphicsBackend::Vulkan;
            } else {
                error = std::string("--backend needs d3d12 or vulkan, got \"") + value + "\"";
                return false;
            }
        } else if (argument == "--dump-frame" || argument == "--dump-state" || argument == "--record" ||
                   argument == "--replay" || argument == "--hash-log" ||
                   argument == "--render-settings") {
            const char* value = ReadOptionValue(argc, argv, i, error);
            if (value == nullptr) {
                return false;
            }
            if (value[0] == '\0') {
                error = argument + " needs a file path";
                return false;
            }
            if (argument == "--dump-frame") {
                options.dump_frame_path = value;
            } else if (argument == "--dump-state") {
                options.dump_state_path = value;
            } else if (argument == "--record") {
                options.record_path = value;
            } else if (argument == "--hash-log") {
                options.hash_log_path = value;
            } else if (argument == "--render-settings") {
                options.render_settings_path = value;
            } else {
                options.replay_path = value;
            }
        } else if (argument == "--quality") {
            const char* value = ReadOptionValue(argc, argv, i, error);
            if (value == nullptr) {
                return false;
            }
            // The names of GraphicsPreset (poligon/graphics_quality.h); render_settings_test
            // checks that both lists agree.
            const std::string name = value;
            if (name != "low" && name != "medium" && name != "high" && name != "ultra") {
                error = std::string("--quality needs low, medium, high or ultra, got \"") + value + "\"";
                return false;
            }
            options.quality = value;
        } else if (argument == "--resolution") {
            const char* value = ReadOptionValue(argc, argv, i, error);
            if (value == nullptr) {
                return false;
            }
            const char* end = value + std::strlen(value);
            int width = 0;
            int height = 0;
            const std::from_chars_result first = std::from_chars(value, end, width);
            bool valid = first.ec == std::errc{} && first.ptr < end && *first.ptr == 'x';
            if (valid) {
                const std::from_chars_result second = std::from_chars(first.ptr + 1, end, height);
                valid = second.ec == std::errc{} && second.ptr == end;
            }
            if (!valid || width < 16 || height < 16 || width > 8192 || height > 8192) {
                error = std::string("--resolution needs WIDTHxHEIGHT between 16 and 8192, got \"") + value + "\"";
                return false;
            }
            options.width = width;
            options.height = height;
        } else if (argument.rfind("--", 0) != 0) {
            options.positional.push_back(argument);
        } else {
            error = "unknown option \"" + argument + "\"";
            return false;
        }
    }
    return true;
}

} // namespace poligon
