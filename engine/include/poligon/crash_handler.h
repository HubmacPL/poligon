#pragma once

namespace poligon {

struct CrashHandlerConfig {
    // File that receives a copy of every crash report. nullptr means
    // "<program name>_crash.txt" next to the executable.
    const char* report_path = nullptr;
};

// Call first thing in main. From then on, when the program dies abnormally it
//   - never opens a dialog box (a headless run must not hang waiting for a click),
//   - prints a report to stderr: what happened and the call stack with function names,
//     source files and line numbers,
//   - saves the same report to the report file,
//   - ends with exit code 4 (poligon::FatalError keeps exit code 3).
//
// Covered: invalid memory access, stack overflow, division by zero and other CPU faults,
// abort(), failed assertions of the debug C runtime, uncaught C++ exceptions, pure virtual
// calls, invalid arguments to C runtime functions, and poligon::FatalError.
//
// Function names need the program's .pdb file next to the executable (all Poligon presets produce it).
// In an AddressSanitizer build, memory errors are reported by the sanitizer instead, in its own format.
void InstallCrashHandler(const CrashHandlerConfig& config = CrashHandlerConfig{});

} // namespace poligon
