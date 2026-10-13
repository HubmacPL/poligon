# Poligon

A game engine for PC (Windows) built for work with an AI agent. The name is deliberate ("poligon" is Polish for a proving ground): it is where the agent proves that its code works.
This file is the table of contents and the rule book. Details live in topic files linked from here.

## Table of contents

- Building, presets, dependencies, adding tests and examples: [docs/en/building.md](docs/en/building.md)
- Game loop, tick and frame, headless mode, command line: [docs/en/game_loop.md](docs/en/game_loop.md)
- Renderer, math conventions, textures and materials, HLSL shaders, backends, frame dumps and image comparison: [docs/en/rendering.md](docs/en/rendering.md)
- ECS (flecs): rules, changes during a system, JSON state dump, state hash, repeatability test: [docs/en/ecs.md](docs/en/ecs.md)
- Input as actions, bindings, recording and replay, recording file format: [docs/en/input.md](docs/en/input.md)
- Determinism: what is guaranteed, how to compare two runs and find the tick where they diverge, what breaks it: [docs/en/determinism.md](docs/en/determinism.md)
- Crashes: report with call stack, program exit codes, how to read a report: [docs/en/crashes.md](docs/en/crashes.md)
- Layout: `engine/` (library `poligon_engine`, headers in `engine/include/poligon/`), `game/` (toy game `toy`), `examples/`, `tests/`, `tools/` (scripts, `image_compare`, `state_compare`), `cmake/`, `docs/en/` and `docs/pl/`.
- After every code change run `powershell -ExecutionPolicy Bypass -File tools\check.ps1`. Only `CHECK PASSED` means it works.

## Project state

- Milestone 1 (verification layer) is complete: headless runs with a fixed time step, input recording and replay, JSON state dump, PNG frame dump, determinism test, crash reports.
- Milestone 2 (models and textures) is done except for real models: GLM, textures and materials, model import through Assimp and the `model_viewer` tool exist; only self-made test models have been loaded so far.
- Next: milestone 3 (scene look).
- Git repository on branch `main`, no remote. The toy game is a test bed for the engine, not a product.

## Roadmap

Goal scene: about 1 km² of terrain with forest and a lake, one animal species, a player walking in first person. It should look impressive.

2. Models and textures. 3. Scene look (shadows, sky, fog, glTF-style materials, anti-aliasing). 4. In-game tools (debug windows, free camera, state view). 5. Skeletal animation. 6. Physics. 7. Terrain 1 km². 8. Vegetation and water.

The order after milestone 2 is the agent's call. Audio, networking, scripting, hot reload and gamepad come after milestone 8.

## What this is

- A C++ framework/library on which a game is written in code. The level editor is a separate program.
- For the author only, for now. Nothing is built for outside users yet, but publishing must stay possible.
- Target games: open terrain, vegetation, animated animals and humanoids (theHunter Classic, a fishing game, an open-world survival). Mostly 3D, 2D possible.
- The key part of our own is the verification layer for the agent: headless runs, frame dumps, game state dumps, input recording and deterministic replay (same machine, same executable). Anything that lets the agent find out by itself whether a change works matters more than convenience of writing by hand.

## Standing rules

1. **Library first.** Before writing any non-trivial code, look for an existing library that does it and fits the license rules. If there is one, use it and say so; write our own only when nothing suitable exists, and say why. The author asked for this explicitly: less code of our own is better.
2. **Licenses.** Dependencies of the engine and the game: MIT, BSD, Apache 2.0, zlib, Boost, MPL 2.0 only. No GPL or LGPL. Check the license of every new dependency (and of what it pulls in) at the source and report it before adding. Tools that run as separate programs (Blender, shader compiler) may have any license.
3. **Verify, do not remember.** State of libraries and licenses is checked online or in the repository. When something is from memory, say so.
4. **No silent shortcuts.** If there is a cheaper way, describe it with the cost of both versions and wait for a decision. In the report, name every simplification.
5. **Clean history.** No third-party assets, unlicensed code, passwords or private paths in the repository. Downloaded models and textures live outside the repository.
6. **Engine and game are separate.** The game knows the engine; the engine never includes game code.
7. **Boring API.** The agent does not know this engine from training. So: one small runnable example per engine feature, built and run on every check; predictable names as in well-known engines; no mini-languages or clever macros. Libraries (flecs, GLM) are used directly, not wrapped.
8. **Hardware.** Must run on Windows 10 with a GTX 980 (Direct3D 12 feature level without ray tracing, Vulkan 1.3, 4 GB). Quality is not capped by that card, but no technique may require what it lacks: no ray tracing, no mesh shaders. Check before proposing a rendering technique.
9. **Documentation.** `CLAUDE.md` is in English. Topic docs exist in English (`docs/en/`) and Polish (`docs/pl/`) with the same file names; change both in the same commit.

## Working mode

- Conversation with the author is in Polish. Code, comments and commit messages are in English.
- Work in whole milestones, not step by step. Commit after every green check. Report at the end of a milestone, briefly; the author dislikes long reports.
- Stop and ask only for: a new dependency, a change outside the project folder, a download, a departure from the plan, or something only the author's eyes can judge. If the author seems to have forgotten a decision, ask.
- Estimates in hours of our sessions. So far real time has been about a tenth of the estimates.
- Ask before starting parallel agents and give the token cost.
- Do not create plan or analysis documents unless asked. This file is the exception.
- The author knows C++. Explain game-engine terms on first use.

## Decisions made (do not reopen)

- Language: C++. Rust with Bevy was rejected.
- Our own core assembled from separate libraries. Wicked Engine as a base was rejected.
- MSVC from Visual Studio 2022 Build Tools, x64, CMake, command-line builds. The Visual Studio installation is not changed (compiler messages stay Polish).
- vcpkg: the copy bundled with Build Tools, manifest mode, package catalog pinned to a commit in `vcpkg.json`. The author's separate vcpkg copy outside the project is not touched.
- Renderer: our own, on SDL3 GPU (zlib). SDL3 also provides the window, input and audio. Plain HLSL shaders compiled at build time. bgfx and Filament were rejected. Diligent Engine (with its ready-made PBR, shadows and post-processing) was considered twice and rejected both times, the second time on 2026-10-13 after a detailed review; do not raise it again.
- Graphics backend: Direct3D 12 by default, because its debug layer works on this machine out of the box. Vulkan is built and tested as the second backend; every image test runs on both. Image comparisons between backends use a wider tolerance (drivers differ in anisotropic filtering).
- Shader compiler DXC (NCSA license, not on the list) is allowed only as a build-time tool. Shipping `dxcompiler.dll` with a game would be a new dependency and a separate decision.
- Math: GLM (under MIT), used directly, always through `poligon/math.h`.
- Images: `stb_image` and `stb_image_write` from the stb package.
- Model import: Assimp (BSD-3), linked into the engine, behind `poligon/model.h`. Any format Assimp reads is accepted. cgltf was approved earlier and then dropped in favor of Assimp.
- Models for the goal scene: the agent searches (Poly Haven is CC0), presents candidates with pictures from `model_viewer`, the author chooses. Every download is shown first (file, source, size). Downloaded files live in the folder `poligon-assets` next to the project folder, outside the repository.
- ECS: flecs (MIT). Typed C++ API only; query strings and flecs script are forbidden. Every component registers its members. All systems are created at startup, before any entity is deleted, each with an explicit phase (flecs orders systems by id, and a recycled id breaks the order). Single-threaded. The flecs REST server only in development builds. EnTT was rejected.
- State dump is our own (`poligon/state_dump.h`), because flecs' `world.to_json()` truncates floating-point numbers.
- Determinism: identical state JSON from two replays is a hard requirement. PNG frames are compared with a tolerance.
- Memory safety is the verification layer's job: AddressSanitizer preset, warnings as errors, static analysis, crash reports with call stacks.

## Library candidates (not yet added)

Physics: Jolt (MIT, in vcpkg). Skeletal animation: ozz-animation (MIT, not in vcpkg). Navigation: Recast/Detour (zlib). Tools and debug UI: Dear ImGui (MIT, in vcpkg with SDL3 and SDL GPU bindings).

## Open

- Terrain, vegetation, world streaming, water: the largest work of our own.
- Terrain and level editor; source of the height map.
- Texture compression: needed by the terrain milestone.
- License of the engine itself: chosen at publication.
