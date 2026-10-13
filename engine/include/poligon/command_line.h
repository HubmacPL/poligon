#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "poligon/renderer.h"

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
//   --hash-interval N     ticks between hash log entries (default 60, N >= 1)
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
    std::int64_t hash_interval = 60;
    // Arguments that are not options (do not start with "--"), in order. What they mean is up
    // to the program, for example a file to open. A program that takes none should reject them.
    std::vector<std::string> positional;
};

// Parses argv (argv[0] is skipped). Returns false and fills `error` on an unknown option,
// a missing value or an invalid value. Unknown options are errors on purpose: a mistyped
// flag must not be silently ignored.
bool ParseCommandLine(int argc, const char* const* argv, CommandLineOptions& options, std::string& error);

} // namespace poligon
