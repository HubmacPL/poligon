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
| `release` | RelWithDebInfo, nothing added | no; not part of `check.ps1` |

Use `release` to measure speed and to look at heavy scenes: `cmake --preset release`, then `cmake --build --preset release`. The Debug build draws the forest scene about three times slower and loads it four times slower.

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
- `imgui` with the features `sdl3-binding` and `sdlgpu3-binding` (MIT; pulls in nothing but SDL3): the debug windows.
- `ozz-animation` 0.17.0 (MIT; depends on nothing but the C++ standard library): skeletal animation. It is not in the vcpkg catalog, so the repository carries its own small port in `ports/ozz-animation` (registered in `vcpkg-configuration.json`), pinned to the release by version and checksum. Only the libraries are built, as DLLs (as static libraries they cannot be linked into the AddressSanitizer build): no tools, importers or samples.
- `joltphysics` 5.6.0 (MIT; depends on nothing but the C++ standard library): physics.
- `assimp` (BSD-3; brings kubazip, pugixml and rapidjson under MIT, poly2tri under BSD-3, minizip and zlib under zlib, polyclipping and utfcpp under Boost; vcpkg also lists ISC for a bundled part).
- `glm` (MIT or "Happy Bunny"; we use it under MIT).
- `flecs` (MIT; contains pieces under BSD-2, MIT and one in the public domain).
- `sdl3` with the `vulkan` feature (zlib, with parts under MIT, Apache 2.0 and BSD-3), `stb` (MIT or public domain), `directx-dxc` as a build tool (NCSA; nothing from it is linked or shipped with a game).

## Engine layout

The engine is a set of static libraries, one per area. Each area has its public headers in `engine/include/poligon/<area>/` (included as `poligon/<area>/<name>.h`), its sources in `engine/src/<area>/`, with one folder per module where the area has several, and its own `CMakeLists.txt` there.

| Library | Area | Modules (folders under `engine/src/<area>/`) | May use |
|---|---|---|---|
| `poligon_core` | `core` | `app` (game loop, clock, command line, version), `platform` (events, errors, crash reports), `input`, `image` (pictures, block compression) | nothing of ours |
| `poligon_ecs` | `ecs` | flecs setup, state dump and hash | core |
| `poligon_render` | `render` | `renderer`, `settings` (presets, render settings file), `impostor`, `shaders/<group>` | core |
| `poligon_animation` | `animation` | skeletons, clips, players | core |
| `poligon_physics` | `physics` | bodies, characters, rays | core |
| `poligon_assets` | `assets` | model files, asset list, cooking, loading by name | core, render, animation |
| `poligon_world` | `world` | `terrain` (with the generator and sky occlusion), `vegetation` (with plant rules, plants made by code, trampling), `water`, `level` | core, render, animation, assets, physics |
| `poligon_debug` | `debug` | debug windows, free camera | core, render, ecs |

- `poligon_engine` links all of them; games, tools, tests and examples link that.
- The "may use" column is checked on every run of `tools/check.ps1` by `tools/check_layers.ps1`, which reads every include (all headers share one include folder, so a wrong include would still compile). The renderer, for example, knows nothing of the world: trampled ground and sky occlusion reach it as a `GroundMap` through the `GroundMapSource` interface.
- Shaders are grouped by what they draw: `mesh`, `terrain`, `vegetation` (impostors), `sky` (sky and clouds), `water`, `post` (full-screen passes); `shaders/common` holds the files several shaders include.

## Adding things

- Engine code: a file in the area's folder (`engine/src/<area>/<module>/`), listed in that area's `CMakeLists.txt`; a public header in `engine/include/poligon/<area>/`. A new area needs a line in `engine/CMakeLists.txt` and in `tools/check_layers.ps1`.

- Test: a file `tests/<name>.cpp` using `test::Expect(condition, "description")` from `tests/test_support.h` and ending `main` with `return test::ExitCode();`, plus an entry `poligon_add_test(<name>)` in `tests/CMakeLists.txt`.
- Example: `examples/<name>/main.cpp` and an entry `poligon_add_example(<name>)` in `examples/CMakeLists.txt`. Every example is also a test; arguments after the name are passed when it runs.
