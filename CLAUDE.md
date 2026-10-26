# Poligon

A game engine for PC (Windows) built for work with an AI agent. The name is deliberate ("poligon" is Polish for a proving ground): it is where the agent proves that its code works.
This file is the table of contents and the rule book. Details live in topic files linked from here.

## Table of contents

- Building, presets, dependencies, adding tests and examples: [docs/en/building.md](docs/en/building.md)
- Game loop, tick and frame, headless mode, command line: [docs/en/game_loop.md](docs/en/game_loop.md)
- Renderer, graphics quality presets (low to ultra), water surfaces, volumetric clouds, sky light and haze, math conventions, textures and materials, HLSL shaders, backends, frame dumps and image comparison: [docs/en/rendering.md](docs/en/rendering.md)
- Rendering work agreed but not done yet (AgX, XeGTAO, terrain occlusion culling, FSR 2): [docs/en/rendering_roadmap.md](docs/en/rendering_roadmap.md)
- ECS (flecs): rules, changes during a system, JSON state dump, state hash, repeatability test: [docs/en/ecs.md](docs/en/ecs.md)
- Input as actions, bindings, recording and replay, recording file format: [docs/en/input.md](docs/en/input.md)
- Determinism: what is guaranteed, how to compare two runs and find the tick where they diverge, what breaks it: [docs/en/determinism.md](docs/en/determinism.md)
- Crashes: report with call stack, program exit codes, how to read a report: [docs/en/crashes.md](docs/en/crashes.md)
- Debug windows (Dear ImGui), render settings file, debug views, free camera: [docs/en/debug_tools.md](docs/en/debug_tools.md)
- Skeletal animation: skeleton and clips from model files, skinned meshes, joint matrices: [docs/en/animation.md](docs/en/animation.md)
- Physics: bodies, characters, rays, how it stays deterministic, the pattern for flecs: [docs/en/physics.md](docs/en/physics.md)
- Particles: the emitter in the world module (repeatable for a seed, state dump), drawing them in the renderer, why no library: [docs/en/particles.md](docs/en/particles.md)
- Vegetation: instancing, levels of detail and fading between them, impostors, ground cover made on the fly, plant rules that read the terrain maps, water and shore plants, grass and flowers made by code, trampled paths, cells, making simpler tree meshes, what shadows cost: [docs/en/vegetation.md](docs/en/vegetation.md)
- Assets and levels: the asset list, the cooker and texture compression, loading by name from a folder or an archive, plants made from models, the level file: [docs/en/assets.md](docs/en/assets.md)
- World streaming: patches, the window around the camera, background loading, what keeps it deterministic, patches computed from the whole land: [docs/en/streaming.md](docs/en/streaming.md)
- Terrain: the terrain generator (uplift, erosion, lakes, maps), ground layers chosen by the maps, height maps, real elevation data from Geoportal and its pitfalls, the terrain tool, the Batorz map: [docs/en/terrain.md](docs/en/terrain.md)
- Layout: `engine/` (libraries per area: core, ecs, render, animation, physics, assets, world, debug, all linked by `poligon_engine`; headers in `engine/include/poligon/<area>/`, sources in `engine/src/<area>/<module>/`; layers and how to add code: [docs/en/building.md](docs/en/building.md)), `game/` (toy game `toy`), `examples/<area>/` and `tests/<area>/` (grouped like the engine), `tools/` (scripts, `image_compare`, `state_compare`, `model_viewer`, `forest_scene`, `image_tool`, `terrain_tool`, `impostor_baker`, `asset_cooker`), `cmake/`, `docs/en/` and `docs/pl/`.
- After every code change run `powershell -ExecutionPolicy Bypass -File tools\check.ps1`. Only `CHECK PASSED` means it works.

## Project state

- Milestone 1 (verification layer) is complete: headless runs with a fixed time step, input recording and replay, JSON state dump, PNG frame dump, determinism test, crash reports.
- Milestone 2 (models and textures) is complete: GLM, textures and materials, model import through Assimp, the `model_viewer` gallery, and an asset pipeline outside the repository (Poly Haven downloads, Blender simplification, EZ-Tree generated trees, alpha repair with `image_tool`).
- Milestone 3 (scene look) is nearly complete: cascaded shadows, sky and sky light, PBR materials, MSAA, tone mapping, ambient occlusion, volumetric fog with light shafts, bloom, automatic exposure, frustum culling, and the showcase `forest_scene` (about 34 ms per frame on the GTX 980 in the `release` preset). Known cost to remove in the vegetation milestone: every tree is drawn at full detail in every shadow cascade (no levels of detail, no instancing).
- Milestone 4 (in-game tools) is complete: debug windows with Dear ImGui (render settings, frame statistics, world state), a render settings file with the option `--render-settings`, debug views of single frame stages, a free camera.
- Milestone 5 (skeletal animation) is complete: skeleton, skin weights and clips are loaded from model files; skinned meshes are drawn in any pose in all passes; `AnimationSet` and `AnimationPlayer` (on ozz-animation) play clips and fade between them; three foxes move in `forest_scene`. Test model: the Khronos "Fox" sample in `poligon-assets/source/khronos/fox` (CC0 model, CC-BY 4.0 animations). A realistic animal model is still to be bought or found by the author.
- Milestone 6 (physics) is complete: `PhysicsWorld` on Jolt Physics (static and dynamic bodies, a walking character, rays), bit-identical replays checked by tests, the toy game rewritten on it, and collisions, a walking player and throwable crates in `forest_scene`.
- Milestone 7 (terrain 1 km²) and the vegetation system are being built together, by the author's decision of 2026-10-14: vegetation is what these games show all the time, so it gets a solid, AAA-capable foundation from the start. Done so far: instanced drawing, the `Vegetation` system (species with levels of detail, cells, separate shadow handling), a Blender tool for simpler tree levels that keeps crowns full, `forest_scene` moved onto it (34 ms -> 16 ms per frame). Since then: trees generated with make_tree.mjs recipes (pine, spruce, birch, oak, young pine and spruce) with three levels each, simpler levels of downloaded rocks and stumps from Blender, impostors baked by our own renderer (hemi-octahedral, 8 x 8 frames) as the farthest tree level, dithered fading between all levels; forest_scene runs at 6 to 17 ms per frame. On 2026-10-14 the shadow path was audited and fixed (coverage-preserving mipmaps for cut-out textures, terrain skirts out of the shadow pass, fades at the shadow range, 5 x 5 tent filtering). On 2026-10-15 wind and ground cover were done, and the author then set the current large step, in this order: (1) temporal anti-aliasing and color grading, (2) graphics presets low, medium, high, ultra (in the render settings file and on the command line; every feature of this step gets values in each; medium must hold 60 fps, at most 16.7 ms, on the GTX 980 at 1920 x 1080 in all measured views, high as close as possible, ultra unlimited but it must start on that card), (3) a terrain generator in the engine based on simulated processes (uplift, hydraulic and thermal erosion, drainage and stream networks), 6 x 6 km at 1 m, bit-identical for a seed, with maps for water, moisture, flow, sediment and rock, terrain materials from those maps and physics as today, (4) water: at least one lake from the generator's hydrology visible from the player's start, with reflection, refraction, depth color and shore, scaled by the presets, (5) the asset system as agreed on 2026-10-14, with texture compression and the forest_scene debt moved into the engine, (6) planting by rules in both Vegetation and ground cover from the generator's maps, including water and shore plants, and a trample map for paths through grass, (7) volumetric clouds with shadows on the ground and influence on sky light, plus lighting (sky-colored shadows, leaf translucency, aerial perspective, large-scale occlusion), (8) more model variety, including better grass. Working mode for it: approvals collected in one list up front, look-only judgments not waited for (screenshots of fixed views in every preset into one folder), a short report after every point. Cheaper shadows were judged not worth it on 2026-10-14 (about 1 ms).
- That step was done on 2026-10-15, all eight points: TAA with upscaling and color grading; presets (`poligon/render/graphics_quality.h`, medium at 8 to 11 ms in all measured views at 1920 x 1080 on the GTX 980, high 10 to 15 ms, ultra 19 to 37 ms); the terrain generator (`poligon/world/terrain_generator.h`) with ground layers chosen by its maps; water surfaces (`poligon/world/water.h`); the asset system (asset list, `asset_cooker`, BC texture compression, loading by name from a folder or a `POLIPAK1` archive, `poligon/world/plants.h`, the level file `poligon/world/level.h`); planting by rules (`poligon/world/plant_rules.h`) with water and shore plants made by code and a trample map; volumetric clouds with cloud shadows, sky occlusion, haze and leaf translucency; grass and flowers made by code (real blades). On 2026-10-16 the author approved and the agent added: Poly Haven grass clumps with simpler levels, a second tree of each kind (pine, spruce, birch, oak; recipes `*_b.json`), the `Material::normal_up` setting for grass and low plants, and the game data folder split into `poligon-assets/source` and `poligon-assets/cooked`. On 2026-10-17, after a comparison with Godot 4.7, the author approved six renderer points and the agent did them: cloud shadows fixed, a sky light map (ambient light and reflections from the captured sky with its clouds), shadows softening with distance from their caster, clouds built up over frames, motion vectors for TAA (`object_id` in `DrawMesh`, `DrawSkinnedMesh`, `DrawModel`), fog and haze in the sky's color; the rest is in `docs/en/rendering_roadmap.md`. Medium is now 9 to 12 ms in the four views, high 11 to 16 ms, ultra 20 to 38 ms.
- On 2026-10-25 and 2026-10-26 (packages 3a and 3b of the author's list for a theHunter Classic port): world positions are double and the renderer draws relative to the camera (Jolt built with double precision from `ports/joltphysics`); the world is streamed by patches (`poligon/world/streaming.h`, `land.h`): the generated land is cooked whole into 512 m patch files, and terrain, collisions, water, plants and sky occlusion are loaded in windows around the camera or the player, with a coarse far background; `forest_scene` runs on it. Decided on 2026-10-26: the terrain generator stays a simulation of the whole land, run when the land is cooked (a 32 km reserve would need a tiled generator later).
- Decided on 2026-10-15: `forest_scene` moves from the Batorz map to terrain generated by the engine. The TIFF reader, `HeightMap` and the Batorz data in `poligon-assets` stay; nothing is deleted.
- The terrain is real: the middle 6 x 6 km around Batorz (50.861443 N, 22.496135 E) from Geoportal, one height per meter, at scale 1:1 (squeezing it to pronounce the hills was tried on paper and dropped by the author on 2026-10-14; the land has enough gullies as it is). Data and maps are in `poligon-assets/source/terrain/batorz`. Done: `HeightMap`, the TIFF reader, `terrain_tool`, `Terrain` (patches from a quadtree, heights read from an image in the vertex shader, ground layers from texture arrays and weight maps), height-field physics. `forest_scene` used it until 2026-10-15 and now runs on generated terrain (cached in `build/terrain_cache`), starting on a hill with a lake in view.
- Asset system, agreed on 2026-10-14, built in full from 2026-10-15 to 2026-10-17 (the last part: clips cooked as ozz-animation archives): (1) one game data folder with `source` and `cooked` halves, (2) an asset list in the repository giving every asset a name and its settings, (3) `asset_cooker` that turns sources into fast-loading files (compressed textures, meshes with levels of detail, ozz animations), (4) loading by name through one entry point, (5) a level file in plain text (terrain, light, plant rules, placed objects, player start), (6) for a released game, archives in a format of our own with hashed names and **no encryption** (the author chose the middle level: keeps casual hands out, like theHunter's archives; encryption was judged not worth it). Text formats follow the style of the render settings file.
- `forest_scene` is a showcase and measuring tool, not a product: it holds only the scene's data and choices (which models, where to plant, distances). Mechanisms that appear there are debt to be moved into the engine; grouping meshes into variants, simpler levels and impostors, the planting loop and the choice of collider moved into `plants.h`, `plant_rules.h` and the asset system on 2026-10-15.
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
8. **Hardware.** Must run on Windows 10 with a GTX 980 (Direct3D 12 feature level without ray tracing, Vulkan 1.3, 4 GB). No technique may require what that card lacks: no ray tracing, no mesh shaders. Check before proposing a rendering technique. Since 2026-10-15 quality is no longer fitted to this card: it is scaled by the graphics presets (low to ultra), and the card is the target for medium.
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
- Math: GLM (under MIT), used directly, always through `poligon/core/math.h`.
- Images: `stb_image` and `stb_image_write` from the stb package.
- Debug windows: Dear ImGui (MIT) with its SDL3 and SDL GPU backends, from vcpkg. Used directly (`ImGui::` calls in game code); never active in headless mode. Every setting a window can change must also be settable from code or from a file, so the agent can reproduce what the author set by hand.
- Skeletal animation: ozz-animation (MIT), approved on 2026-10-14 over a player of our own because blending by body part and foot placement come with it. Built as DLLs from our own vcpkg port in `ports/ozz-animation`, since the vcpkg catalog does not have it. Hidden behind `poligon/animation/animation.h`, like Assimp behind `model.h`.
- Physics: Jolt Physics 5.6.0 (MIT, in vcpkg, a DLL), approved on 2026-10-14. Bullet was considered and rejected (no development for games since 2022, determinism not a goal); writing our own was rejected. Hidden behind `poligon/physics/physics.h`. Single-threaded on purpose. Jolt's own code is not to be modified; if a need arises that it does not cover, stop and ask.
- Model import: Assimp (BSD-3), linked into the engine, behind `poligon/assets/model.h`. Any format Assimp reads is accepted. cgltf was approved earlier and then dropped in favor of Assimp.
- Models for the goal scene: the agent searches (Poly Haven is CC0), presents candidates with pictures from `model_viewer`, the author chooses. Every download is shown first (file, source, size). Downloaded files live in `poligon-assets/source` next to the project folder, outside the repository; cooked files in `poligon-assets/cooked`.
- ECS: flecs (MIT). Typed C++ API only; query strings and flecs script are forbidden. Every component registers its members. All systems are created at startup, before any entity is deleted, each with an explicit phase (flecs orders systems by id, and a recycled id breaks the order). Single-threaded. The flecs REST server only in development builds. EnTT was rejected.
- State dump is our own (`poligon/ecs/state_dump.h`), because flecs' `world.to_json()` truncates floating-point numbers.
- Determinism: identical state JSON from two replays is a hard requirement. PNG frames are compared with a tolerance.
- Memory safety is the verification layer's job: AddressSanitizer preset, warnings as errors, static analysis, crash reports with call stacks.
- The engine does not force ECS on a game: every module has a plain C++ API, and flecs is for the game's living state (player, animals, items), where it gives the state dump, the state hash and a repeatable order of systems. Static masses (trees, grass, rocks, terrain) are never entities.
- Scripting comes after milestone 8. Decided on 2026-10-15: no language of our own (the agent would not know it, and it would cost years), no Python translated to C++ (no mature tool, two languages to debug). The main candidate is an embedded Lua for game logic; Python stays on the tool side (Blender scripts).
- Module names are plain English words as in well-known engines (`wind`, `vegetation`, `terrain`), never brand names, because the agent must understand a name without looking it up (rule 7). Brand names, if ever wanted, go into one list in the docs at publication, not into the code.

## Library candidates (not yet added)

Navigation: Recast/Detour (zlib).

## Open

- Terrain, vegetation, world streaming, water: the largest work of our own.
- Terrain and level editor; source of the height map.
- License of the engine itself: chosen at publication.
- Loading a state dump back (save games; replaying a bug from the tick before it): not done yet. Comes with the asset system or the first save-game feature.
- The engine is split into libraries per area (2026-10-17), with a tool side (`poligon_animation_build`, `poligon_assets_cook`) a released game leaves out; `poligon_runtime` is the game side. A game without physics not linking Jolt would need `poligon_world` split further (plants make colliders).
