#pragma once

namespace poligon {

// Prints "FATAL: <message>" and the call stack to stderr and ends the process with exit code 3.
// Use for programmer errors that make continuing pointless (invalid configuration, broken invariants).
// Never opens a dialog box, so headless runs cannot hang on it. When the crash handler is
// installed (poligon/core/crash_handler.h), the report is also saved to its report file.
[[noreturn]] void FatalError(const char* message);

} // namespace poligon
