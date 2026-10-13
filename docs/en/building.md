# Building and checking

## One command

```
powershell -ExecutionPolicy Bypass -File tools\check.ps1
```

Configures, builds and tests every preset. Exit code 0 and a last line `CHECK PASSED` mean success. On the first failure the script exits with code 1 and a line `FAILED: <step>`.

One preset: `tools\check.ps1 -Presets dev`.

## Presets (CMakePresets.json)

| Preset | What it is | Tests |
|---|---|---|
| `dev` | Debug, for everyday work | yes |
| `asan` | RelWithDebInfo with AddressSanitizer | yes, plus the canary |
| `analyze` | Debug with MSVC static analysis (`/analyze`) | no, compile only |

Output goes to `build/<preset>/`; all programs and DLLs to `build/<preset>/bin/`, files produced by tests (frame dumps) to `build/<preset>/test_output/`.

## Common rules

- Every target of ours (engine, game, examples, tests) links `poligon_build_options`: `/W4 /WX /permissive- /utf-8`. A warning stops the build. Third-party dependencies do not link it.
- AddressSanitizer is enabled globally for everything our CMake builds. Libraries from vcpkg are built separately and are not instrumented. The sanitizer's runtime DLL is copied next to the programs.
- The test `asan_canary_detects_overflow` writes past a buffer on purpose and passes only when the sanitizer reports it. If it starts failing, the `asan` build no longer protects anything.

## Environment

`tools\dev_env.ps1` finds Visual Studio 2022 (or Build Tools) through `vswhere` and loads the x64 environment into the current process. That is where `cl`, `cmake` and `ninja` come from; nothing is added to the system PATH. For manual work in a terminal: `. tools\dev_env.ps1`, then the usual `cmake --preset dev` and so on.

## Dependencies (vcpkg)

- We use the vcpkg copy bundled with Visual Studio; `dev_env.ps1` points `VCPKG_ROOT` at it for the process.
- The dependency list is in `vcpkg.json`. The `builtin-baseline` field pins the package catalog to one commit, so versions are repeatable.
- Installed libraries land in `vcpkg_installed/` in the project (shared by all presets). vcpkg keeps downloaded sources and built packages in its cache in the user profile.
- Before adding a dependency: look for it (library-first rule), check its license at the source and get the author's approval.
- `glm` (MIT or "Happy Bunny"; we use it under MIT).
- `flecs` (MIT; contains pieces under BSD-2, MIT and one in the public domain).
- `sdl3` with the `vulkan` feature (zlib, with parts under MIT, Apache 2.0 and BSD-3), `stb` (MIT or public domain), `directx-dxc` as a build tool (NCSA; nothing from it is linked or shipped with a game).

## Adding things

- Test: a file `tests/<name>.cpp` using `test::Expect(condition, "description")` from `tests/test_support.h` and ending `main` with `return test::ExitCode();`, plus an entry `poligon_add_test(<name>)` in `tests/CMakeLists.txt`.
- Example: `examples/<name>/main.cpp` and an entry `poligon_add_example(<name>)` in `examples/CMakeLists.txt`. Every example is also a test; arguments after the name are passed when it runs.
