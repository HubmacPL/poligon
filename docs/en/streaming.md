# World streaming

A large world is cut into square **patches**, and only a window of patches around the camera is kept in memory. The pattern is theHunter Classic's: a 32 768 m reserve is 64 x 64 patches of 512 m, a 9 x 9 window of them is loaded around the camera, loading runs in the background ring by ring from the nearest, patches outside the window are let go, and each kind of data does this on its own.

Header: `poligon/world/streaming.h`. Example: `examples/world/streaming`. Test: `tests/world/streaming_test.cpp`.

## Terms

- **Patch**: a square of the world, `StreamingGridSettings::patch_size` meters on a side, named by its column and row (`PatchCoord`). Patch (0, 0) starts at `StreamingGridSettings::origin`; the world has `patch_count_x` x `patch_count_z` of them.
- **Window**: the patches within `radius` patches of the one holding a position, a square of (2 radius + 1)^2 (radius 4 = 9 x 9). `StreamingGrid::GetWindow` lists it nearest **ring** first (ring 0 is the patch itself, ring 1 its eight neighbors...).
- **Source** (`PatchSource`): one kind of data loaded by patches. `Load` reads or computes a patch, `Attach` makes it live on the main thread (uploads images, adds plants or bodies), `Detach` lets it go.

## How it works

- `WorldStreamer` owns one loading thread (`poligon/core/worker.h`: SDL3's `SDL_CreateThread`, `SDL_Mutex` and `SDL_Condition`; SDL3 has no ready job queue, so the queue is our own, about 150 lines).
- `AddSource(source, radius, mode)` gives each source its own window.
- `Update(observer)`, once per frame at a fixed place before drawing: when the observer has entered another patch, patches that left a **Background** source's window are detached, loads still waiting are dropped, and the missing patches are queued ring by ring over all sources, so every kind of data near the observer comes first. Then at most `SetMaxAttachPerFrame` (4) finished loads are attached. A load that finishes after its patch has left the window is dropped (`StreamingStats::discarded`).
- `RequirePatches(positions)`, before a tick: every **Immediate** source gets each patch within its radius of any position, loaded and attached at once on the calling thread in a fixed order, and lets go of the patches no position needs.
- `SetSynchronous(true)` makes `Update` load Background sources at once too: for headless runs whose pictures must not depend on timing. `FinishLoading` waits for every queued load and attaches it.

## Determinism

The game state must never depend on how fast loading went:

- What the simulation stands on (terrain heights for physics, colliders of trees and rocks) is an **Immediate** source. The game asks for it before every tick with the positions of everything that moves (`RequirePatches({player, animals, thrown things})`), so it is loaded and let go by the game state alone. Body numbers in the physics world therefore come in the same order in every run.
- Everything that is only drawn is a **Background** source; its timing changes pictures, never the state.
- A source's `Load` runs on the loading thread: it may only read data the main thread does not change (the cooked files, the whole maps, the rules).
- `streaming_test` walks a character over 1 km of hills cut into 128 m patches, with crates thrown now and then, once with background loads taking 0 ms and once 15 ms: the physics state hash is the same on all 600 ticks, though the slow run handed over 10 drawn patches and the fast one 25.

## Patches computed from the whole

The terrain generator simulates the whole land at once (rivers and erosion are not local), so the land is generated whole when it is cooked and cut into patches then (decided on 2026-10-26). Everything computed from it per patch gives exactly what the whole map gives:

- `CutHeightMap` and `CutTerrainMaps` cut a window of a height map or of all the generator's maps. The window keeps the whole map's coordinates (`HeightMap::first_x`, `TerrainMaps::map_first_x`), so inside it `GetHeight`, `Sample`, `GetWaterDepth` and `MapConditions::Weight` give the whole map's numbers to the bit, as long as the window has a margin of two map points around the patch.
- `PlacePlantsInRectangle` and `PlacePlantGroupInRectangle` place a rule's plants only inside a patch's rectangle. Every grid cell decides from its own coordinates and the seed, and a place belongs to exactly one rectangle, so the patches together give the whole map's plants, each once, in any order of patches. `plant_rules_test` checks this to the bit on 6 x 6 patches of 100 m (which cut through the 4 m grid cells), visited in a scrambled order.

## Known limits

- One loading thread. Loads are not spread over several threads.
- No hysteresis yet: walking back and forth over a patch border loads and lets go the same row of patches each time.
