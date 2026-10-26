# World streaming

A large world is cut into square **patches**, and only a window of patches around the camera is kept in memory. The pattern is theHunter Classic's: a 32 768 m reserve is 64 x 64 patches of 512 m, a 9 x 9 window of them is loaded around the camera, loading runs in the background ring by ring from the nearest, patches outside the window are let go, and each kind of data does this on its own.

Headers: `poligon/world/streaming.h` (the grid and the streamer), `poligon/world/land.h` (the land by patches). Examples: `examples/world/streaming` (the streamer alone), `examples/world/streamed_land` (a drawn world with every source). Tests: `tests/world/streaming_test.cpp`, `tests/world/terrain_streaming_test.cpp` (both backends).

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

## The land and its sources

`LandSource` hands out the land patch by patch (`LandPatch`): the generator's maps over the patch with a margin of 8 m (`kLandPatchMargin`), and the ground layer weights over the patch's map points. `LandInfo` says how the land is cut, where it lies (`world_origin`), its lakes, and its **background**: the whole land's heights a point every 64 m, used where no patch is held. `LandFromMaps` cuts patches from whole maps in memory and keeps the last 32 (newest first, the oldest unused go); every source reads through one LandSource, so a patch is cut or read once.

Sources, each with its own window:

| Source | Header | Mode | What a patch becomes |
|---|---|---|---|
| `TerrainPatchSource` | terrain.h | Background | a page of the streamed `Terrain` |
| `TerrainPhysicsSource` | terrain.h | Immediate | one height field, the whole map's heights |
| `WaterPatchSource` | water.h | Background | a mesh per lake of the patch's wet squares |
| `PlantPatchSource` | plants.h | Background | the plants a `Planter` puts there, in the `Vegetation` |
| `PlantColliderSource` | plants.h | Immediate | their trunks and blocks in the physics world |
| `SkyOcclusionPatchSource` | sky_occlusion.h | Background | a tile of sky occlusion (land and crowns), joined for the renderer |

- **Streamed terrain**: `Terrain(renderer, land_info, config, pages)` holds up to `pages` patches; each is a page of a texture array on the graphics card (`Renderer::CreateTerrainPages`, `SetTerrainPage`), with a rim of one point so that the ground's direction at the edge is right, and its own ground layer weights. Each patch is its own tree of squares (the patch size must be 64, 128, 256, 512... height points); all held patches go out in one `DrawTerrain` call, one draw call per pass. `GetHeight`, `GetNormal` and `GetSlopeDegrees` read the held patch, or the background where none is held.
- **Plants**: the game gives a `Planter`, a function that puts the plants of a rectangle of the land (usually `PlacePlantGroupInRectangle` with the level's rules). It is called on the loading thread and by several sources for the same patch, so it must only read. The `Vegetation`'s cells must fit the patches: `SetCellOrigin(land origin)` and a patch size that is a whole number of cells; `RemoveInstancesInRectangle` then takes exactly a patch's plants.
- **Sky occlusion**: each patch's tile is worked out from the patch and its eight neighbors (so `reach` must not be longer than a patch) and the crowns the planter puts on and around it; the held tiles are joined into one map, built again only after a change.
- **Ground cover** is not a source (it is grown in small cells around the camera anyway): `GroundCover::SetLand(&land)` makes it read each place's maps from the land patch there, and a cell grows only once every patch it touches is held by the terrain; until then it is tried again on every `Update` (a patch the land source has let go of meanwhile is read again).
- **Trample map**: it is game state that follows the player, not loaded data. `TrampleMap::SetRememberOutside(true)` keeps the values of cells that leave its grid and gives them back, less what they would have recovered meanwhile, when the grid comes back.

## Cooked land

A game reads the land cooked (`poligon/world/land.h`): the level names its world (`world worlds/forest`, `world.patch_size`, `world.background_spacing`, see [assets.md](assets.md)), and `CookLevelWorld` (or `asset_cooker --level`) generates the land, makes its ground layer weights from the level's `ground` blocks and writes:

- `NAME.pworld`: the header line `poligon-world 1`, a number naming the settings it was made from (the generator's, the ground layers', the patch size: when they change, it is cooked again), then the `LandInfo` (patches, spacings, size, lakes, the background heights and weights);
- `NAME/X_Z.ppatch`: the header line `poligon-patch 1`, then one `LandPatch` (its window of the maps and its weights) as raw numbers.

`LandFromFiles(library, name, world_origin)` reads them through an `AssetLibrary`, from the cooked folder or a `POLIPAK1` archive (read file by file, so a released game holds only the patches it uses), on any thread. A patch read back equals the patch cut from the whole maps to the bit (`land_files_test`, from the folder and from the archive). Binary files are written with `poligon/core/binary_io.h`, shared with the asset files.

## The far background

Beyond the window the land is still there: a streamed `Terrain` draws it from `LandInfo::background` (the whole land's heights a point every 64 m, with ground layer weights at the same points) wherever no patch is held. The coarse heights are brought to 64 cells per patch (8 m for 512 m patches), so that the background's smallest square is exactly one patch; a square with held patches inside is split down to single patches and those are left out, and far squares grow to 2, 4, 8... patches by the same distance rule as the patches' own squares. It is one more `DrawTerrain` call, so one more draw call per pass; the haze and fog of the renderer cover it like any land. `SetBackgroundVisible(false)` turns it off; `TerrainStats::background_patches` counts its squares.

Forest impostors on far patches are not drawn yet (see Known limits).

What `terrain_streaming_test` checks, on 512 m of generated land cut into 4 x 4 patches of 128 m, on both backends:

- heights and normals of the streamed terrain equal the whole terrain's to the bit, on patch borders too; the largest step across a border over 2 mm is 0.5 mm (the slope, not a seam);
- the land drawn patch by patch looks like the land drawn whole (at most 0.009% of pixels differ by more than 2 of 255; the trees of squares end in the same sizes);
- collisions: rays across a patch border hit within 0.3 mm of the land's height; a window of 2 x 2 patches at the land's corner has 4 height fields and land outside it has no collisions; when the window moves, the old patches are neither drawn nor collide;
- plants from patches are the whole land's (2599 of them, the same count of trunks), and the picture with them is the same pixel for pixel, also at patch borders;
- water from patches covers the same squares as `CreateLakeSurfaces` over the whole land (no pixel differs);
- sky occlusion joined from patches equals the whole land's point for point, also with the crowns of the plants (973 points under crowns, no difference);
- ground cover on the streamed terrain grows the whole land's tufts (4611, the same transforms), standing on a corner of four patches;
- a trampled path left behind is there again on return, less what it recovered;
- with a window of 2 x 2 patches, the rest of the land is drawn from the background (16 m points there): 12 squares, and 0.07% of the pixels of a wide view from above differ from the whole land by more than 16 of 255.

## forest_scene on streaming

Since 2026-10-26 `forest_scene` streams its 6 x 6 km land: the level names `worlds/forest` (12 x 12 patches of 512 m), which is cooked into `poligon-assets/cooked` on the first run (about 10 s on top of generating the land); the ground layers moved from code into the level file. Windows: terrain and water 9 x 9 patches around the camera, plants 7 x 7, sky occlusion 5 x 5, collisions of the land and of trunks and rocks 3 x 3 around the player and each crate (`RequirePatches` before every tick). Heights for what stands on the land (animals, crates, prints) are read from the land patches, never from the drawn terrain. Headless runs load synchronously, so their pictures do not depend on timing; a replay of the same recording gives the same state to the bit.

Measured on the GTX 980 at 1920 x 1080, high, the four fixed views (before: the land held whole):

| | before | after |
|---|---|---|
| graphics card, textures (renderer's own count) | 614 MB | 491 MB |
| graphics card, buffers | 30-31 MB | 30-31 MB |
| process memory (private bytes) | 1750-1960 MB | 1390-1660 MB |
| frame, medium | 6.2-8.8 ms | 6.5-9.4 ms |
| frame, high | 8.7-13.3 ms | 9.0-13.7 ms |
| processor per frame | 2.6-5.0 ms | 2.7-5.0 ms |
| physics step | 0.048-0.055 ms | 0.061-0.071 ms |

The pictures of the four views match the land held whole except on 0.2 to 3% of pixels by more than 12 of 255, on sunlit leaves and grass blades: the sky occlusion grid now starts at patch corners, a meter away from where it started before, and crowns' and grass blades' thin edges show it. The physics step grew because collisions now cover 3 x 3 patches around the player (2.4 km², about 13 000 trunks and rocks) instead of a fixed square of 1 km around the start; `forest_scene` makes its physics world with room for 65 536 bodies.

## Known limits

- One loading thread. Loads are not spread over several threads.
- No hysteresis yet: walking back and forth over a patch border loads and lets go the same row of patches each time.
- A streamed terrain's pages are fixed when it is made; a window larger than its pages ends the program with a message.
- Sky occlusion looks at most one patch far (`reach` up to the patch size).
- No forest beyond the plants' window: far patches show bare background land. Impostors placed from a forest density map were not built: in `forest_scene`, the only measured scene, the rules plant trees only within 1.2 km of the start, so there is no far forest whose cost could be measured against the limit of 1 ms in high.
- Where a held patch meets the background, the fine and the coarse land may differ by up to a few meters; the patches' skirts hide the gap, not the step.
- A patch's plants are placed twice when both the drawn plants and their colliders are streamed (the planter runs once per source); it costs loading time, not memory.
