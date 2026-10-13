#pragma once

// Internal to the engine: shared by crash_handler.cpp and fatal_error.cpp.

namespace poligon {

// Prints `headline` and the current call stack to stderr, saves the report file if the
// crash handler is installed, and ends the process with `exit_code`. Never returns.
// frames_to_skip: how many innermost stack frames (reporting helpers) to leave out.
[[noreturn]] void ReportAndExit(const char* headline, int exit_code, int frames_to_skip);

} // namespace poligon
