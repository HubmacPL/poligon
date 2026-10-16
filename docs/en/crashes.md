# Crashes and fatal errors

Headers: `poligon/core/crash_handler.h`, `poligon/core/fatal_error.h`. Example: `examples/crash_handler`. Tests: `tests/crash_test.cpp`.

## What it does

`poligon::InstallCrashHandler()` as the first line of `main` makes a program that dies:

- never open a dialog box (a headless run must not hang);
- print a report to stderr: what happened and the call stack with function names, files and line numbers;
- save the same report to the file `<program>_crash.txt` next to the executable (or to the path in `CrashHandlerConfig::report_path`);
- exit with code 4.

`poligon::FatalError(message)` does the same with the headline `FATAL:` and exit code 3; it prints the stack also without the crash handler installed.

## Exit codes of Poligon programs

| Code | Meaning |
|---|---|
| 0 | success |
| 1 | a run-time error reported by the program (for example a file could not be written) |
| 2 | bad command-line arguments |
| 3 | `FatalError`: a programmer error detected by the engine |
| 4 | crash |

## Reading a report

```
CRASH: access violation writing address 0x0000000000000000
Program: toy.exe
Stack:
  #0 toy::MoveSystem (...\toy_world.cpp:123)
  #1 ...
```

- The first line says what happened. `#0` is where it happened, the callers follow below.
- For an uncaught C++ exception the first line holds its `what()`, and the stack starts at the `throw`.
- For `abort()` and checks of the debug library the first frames belong to the library; game code is right below them.

## What is caught

Invalid memory access, stack overflow, integer division by zero and other CPU exceptions, `abort()`, uncaught C++ exceptions, pure virtual calls, invalid arguments to C library functions, and in Debug builds also the checks of the debug library (for example a vector index out of range).

## Limits

- Function names need the `.pdb` file next to the program. All current presets produce it.
- In the `asan` build, memory errors are reported by AddressSanitizer in its own format before our handler sees them.
- Crashes that Windows ends without exception handling (`__fastfail`, for example a detected stack overwrite with `/GS`) give no report, only the exit code `0xC0000409`.
- The stack reserve for reporting a stack overflow covers the main thread; the engine is single-threaded today.
- The report is text. No memory dump (`.dmp`) for a debugger is written.
- File paths in a report are absolute (from the machine the program was built on).
