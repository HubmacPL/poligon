#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "poligon/core/graphics_backend.h"

namespace poligon {

// Options every Poligon program understands:
//   --headless            run without a window and without the wall clock, one tick per frame
//   --ticks N             stop after N simulation ticks (N >= 1)
//   --backend NAME        graphics backend: d3d12 or vulkan
//   --gpu-validation      turn on the graphics API debug layer (if installed)
//   --dump-frame PATH     save the last rendered frame to PATH as PNG when the run ends
//   --dump-state PATH     save the complete game state to PATH as JSON when the run ends
//   --record PATH         write the input of this run to PATH
//   --replay PATH         take input from the recording PATH instead of keyboard and mouse
//   --hash-log PATH       write the state hash to PATH every --hash-interval ticks
//   --render-settings PATH  load sun, environment and post-processing settings from PATH
//                         (a file saved from the render settings window, see render_settings.h)
//   --hash-interval N     ticks between hash log entries (default 60, N >= 1)
//   --quality NAME        graphics preset: low, medium, high or ultra (see graphics_quality.h);
//                         applied after --render-settings
//   --resolution WxH      size of the picture in pixels, for example 1920x1080
struct CommandLineOptions {
    bool headless = false;
    std::int64_t ticks = 0;  // 0 = not given, run until the game quits
    GraphicsBackend backend = GraphicsBackend::Default;
    bool gpu_validation = false;
    std::string dump_frame_path;  // empty = not given
    std::string dump_state_path;  // empty = not given
    std::string record_path;      // empty = not given
    std::string replay_path;      // empty = not given
    std::string hash_log_path;    // empty = not given
    std::string render_settings_path;  // empty = not given
    std::int64_t hash_interval = 60;
    std::string quality;  // empty = not given; otherwise a preset name, already checked
    int width = 0;        // 0 = not given
    int height = 0;
    // Arguments that are not options (do not start with "--"), in order. What they mean is up
    // to the program, for example a file to open. A program that takes none should reject them.
    std::vector<std::string> positional;
    // The program's own options (ParseCommandLine's `extra_options`) that were given: name
    // without "--" -> value ("" for an option that takes none). Given twice, the last counts.
    std::map<std::string, std::string> extra;
};

// An option of the program's own, on top of the standard ones: "--NAME" or "--NAME VALUE".
struct CommandLineOption {
    std::string name;          // without "--", for example "data"
    bool takes_value = false;  // true: the next argument is its value
};

// Parses argv (argv[0] is skipped). Returns false and fills `error` on an unknown option,
// a missing value or an invalid value. Unknown options are errors on purpose: a mistyped
// flag must not be silently ignored.
// `extra_options`: the program's own options, read into CommandLineOptions::extra; a name
// that is one of the standard options is an error.
bool ParseCommandLine(int argc, const char* const* argv, CommandLineOptions& options, std::string& error,
                      const std::vector<CommandLineOption>& extra_options = {});

} // namespace poligon
