# Vegetation

Drawing plants, rocks and other things that stand around in great numbers: instancing, levels of detail, cells.

Headers: `poligon/vegetation.h`, `poligon/renderer.h` (`DrawMeshInstances`, `BakeImpostor`, `DrawImpostorInstances`), `poligon/impostor.h`. Tool: `impostor_baker`. Examples: `examples/vegetation`, `examples/impostor`. Tests: `tests/vegetation_render_test.cpp`, `tests/impostor_render_test.cpp`, the instancing part of `tests/scene_render_test.cpp`.

**State:** instancing, levels of detail chosen by distance, cells, separate handling of shadows, impostors for the farthest level, fading between levels, tools for simpler levels of trees and for impostors. Not yet: wind, placing plants by rules of the terrain, plants made on the fly around the player.

## Terms

- **Instancing**: one mesh drawn many times in a single draw call, each copy with its own transform. A forest of a thousand pines of one kind costs a handful of draw calls, not thousands.
- **Level of detail** (LOD): the same plant as several meshes, from the full one for up close to simple ones for far away.
- **Cell**: a square piece of the world (32 m by default) that holds its own plants. Whole cells can be skipped or handed over without looking at single plants.
- **Leaf card**: a small flat piece with a picture of a twig, cut out by transparency. Tree crowns are made of thousands of them.
- **Impostor**: a far-away stand-in for a model: one flat card that always faces the camera and shows pictures of the model taken beforehand from many directions.
- **Dithered fade**: two levels of a plant drawn at once, each keeping the pixels the other drops by a fixed per-pixel pattern, so one dissolves into the other instead of jumping.

## Instancing in the renderer

- `Renderer::DrawMeshInstances(mesh, material, transforms, count, options)` draws `count` copies. The transforms are copied at the call.
- Every copy is culled on its own, against the camera's view and against each shadow cascade (by a sphere around the mesh).
- `InstanceOptions::draw_in_view = false` makes the copies cast shadows only; `shadow_cascades` is a bit mask of the cascades they are drawn into (0 = no shadow). This is how shadows get simpler meshes than the picture.
- `ComputeShadowCascadeEnds(near_plane, shadow_distance)` tells where each cascade ends.
- `FrameStats` counts `instances_submitted`, `instances_drawn`, `shadow_instances` and `instance_draw_calls`.
- Under the hood: the surviving transforms of all batches go into one buffer, uploaded once per frame; `mesh_instanced.vert.hlsl` reads the transform by instance number. Not for skinned meshes.

## The vegetation system

```cpp
poligon::VegetationSpecies pine;
pine.levels = {{full_parts, 28.0f}, {medium_parts, 70.0f}, {low_parts, 450.0f}};  // parts, used up to this distance
pine.shadow_distance = 90.0f;   // no shadow from further away
pine.shadow_level_bias = 1;     // shadows use one level simpler than the picture

poligon::Vegetation vegetation;                       // cells of 32 m
const int species = vegetation.AddSpecies(pine);
vegetation.AddInstance(species, transform);           // once per plant
// every frame, between BeginFrame and EndFrame:
vegetation.Draw(renderer, camera);
```

- A **species** is a list of levels, nearest first. Each level has parts (mesh, material, transform relative to the plant's foot) and the distance up to which it is used. Beyond the last level the plant is not drawn.
- Distances are measured along the ground from the camera.
- `Draw` goes through the cells in a fixed order. A cell with nothing in range is skipped; a cell outside the camera's view (its box grown by 30 m for the plants' height) hands its plants over as shadow casters only; a cell lying wholly within one level is handed over in one piece; only cells that straddle a boundary are looked at plant by plant. Then each species and level becomes one `DrawMeshInstances` call per part for the picture, plus caster-only calls (with `draw_in_view = false`) for the plants within shadow range, drawn with the shadow level.
- `SetDistanceScale(detail, shadow)` multiplies all level distances and shadow distances, for tuning with sliders.
- `GetStats()`: plants per level, cells skipped, batches.
- Meshes and textures belong to the caller; the system only holds handles and transforms.
- `VegetationSpecies::fade_width` (default 0.1): the share of each level's distance over which the plant fades into the next level, and into nothing after the last. A plant at 66 m of a level ending at 70 m is drawn at both levels. The fade travels in the last value of each instance matrix (`[3][3]`, 1 in an ordinary transform): between 0 and 1 that share of the copy's pixels is kept, between -1 and 0 that share is dropped. Every vertex shader passes it on and the fragment shaders of the scene, shadow and depth passes apply it (`KeptByFade` in `common.hlsli`), so shadows and fog follow. Without temporal anti-aliasing the pattern can be seen as fine grain on the few plants inside a fade.
- A level with `impostor` set is drawn as that impostor instead of its parts (`DrawImpostorInstances`). Impostors cast no shadows; if the shadow level would be an impostor, the last mesh level casts the shadow.
- A plant's shadow does not stop at `shadow_distance` in one step either: over the last `fade_width` of that distance it dissolves by the same pattern. Casters should reach further than the shadows are shown (`forest_scene`: 110 m for trees against a shadow range of 65 m), because a tree beyond the shadow range still throws its shadow into it; otherwise shadows appear only when their tree comes within range.

## Ground cover

Grass, ferns, nettles, litter, small plants and pebbles: too many to store for a map, so they are made on the fly around the camera (`poligon/ground_cover.h`). Example: `examples/ground_cover`; test: `tests/ground_cover_test.cpp`.

```cpp
poligon::GroundCoverLayer grass;
grass.species = grass_species;          // parts per level, as for Vegetation; keep its last distance below the radius
grass.density = 1.2f;                   // per square meter where allowed in full
grass.max_slope_degrees = 40.0f;
grass.patch_threshold = 0.3f;           // grows in soft patches; 0 = evenly everywhere
grass.align_to_ground = true;           // tilts with the slope
poligon::GroundCover cover(terrain, 90.0f);   // plants exist within 90 m of the camera, in cells of 16 m
cover.AddLayer(grass);
// every frame:
cover.Update(camera.position);          // builds cells that came into reach, drops those that left
cover.Draw(renderer, camera);           // between BeginFrame and EndFrame
```

- A cell's plants come from rules (slope, height range, patch noise) and a hash of the cell's position and the layer's seed, so the same cell always holds the same plants, in whatever order cells are built. Every random number of a plant is drawn before any rule, so a plant rejected by a rule does not shift the ones after it.
- `Update` does nothing while the camera stays within one cell; crossing into a new one builds the cells that came into reach (a few, each in a fraction of a millisecond) and hands every living plant to a `Vegetation` inside, which then draws them with levels, fades, wind and shadows like anything else.
- `sink_meters` buries pebbles a little; `scale_min`/`scale_max` vary size; `CollectInstances` lists what exists (tests, tools).
- In `forest_scene`: grass (3 per m², in patches, every third of the 21 tuft variants, shadows only within 8 m), ferns, nettles, low shrubs and small moss rocks grow within 90 m of the player over the whole 6 x 6 km, with one layer per model variant; nothing of them is planted any more. Trees, big shrubs, boulders, stumps and trunks stay planted (`Vegetation`), because they have collisions and shadows from afar. Measured (release, headless, 200 frames, the same run with the radius cut to 1 m as the baseline): 1.1 to 1.6 ms per frame for about 16000 extra instances in reach, of which 4000 to 4500 in view. Each layer costs a draw call per pass, so a kind with many variants should use a few of them, or its variants should be merged into clumps.
- Not yet: a painted ground map saying where which layer grows (today only slope, height and noise), a trample map for paths through grass, and a fade-in when a cell appears nearer than the layer's farthest distance.

## Impostors

```cpp
poligon::Impostor pictures;
renderer.BakeImpostor(parts, part_count, 8, 128, pictures);   // 8 x 8 directions, 128 px each
poligon::SaveImpostor("pine.impostor", pictures);           // or LoadImpostor
const poligon::ImpostorHandle pine_card = renderer.CreateImpostor(pictures);
pine.levels.push_back({{}, 1700.0f});                       // a level drawn up to 1700 m ...
pine.levels.back().impostor = pine_card;                    // ... as the impostor
```

- `BakeImpostor` photographs the model with our own renderer, without perspective, from 8 x 8 directions spread over the half sphere above it ("hemi-octahedral" layout: the middle of the grid looks straight down, its edge from the horizon). Each picture holds what the surface is, not how it is lit: the base color with coverage, and the direction each pixel faces. In the game the impostor is lit by `lighting.hlsli` like everything else, so it follows the sun, the sky and the fog.
- Drawing: one card per instance, the size of the model, turned to the camera. For each pixel the four frames nearest to the direction of view are read (each at the point where the card pixel falls on that frame's picture plane) and blended by how near each direction is. Viewed from the side or from a hill the picture changes smoothly as the camera moves around.
- The instance may turn about the vertical and be scaled evenly; the baked normals turn with it.
- Files: `NAME.impostor` (frames, center, radius as text), `NAME_color.png`, `NAME_normal.png` (`poligon/impostor.h`).
- `impostor_baker MODEL OUT.impostor [--frames N] [--size PIXELS] [--keep-origin]` bakes one model file. The foot of the model is put at the origin as `forest_scene` does; `--keep-origin` keeps it where the file has it sideways (generated trees have their trunk there).
- One impostor of 8 x 8 frames of 128 px is two 1024 x 1024 images, 11 MB on the graphics card with mipmaps; compressed textures will cut that later.
- Limits: a card is flat, so its outline does not change when the camera moves only a little closer or sideways, and it does not hide the ground behind it as a solid crown would; meant for 100 m and more. The model's sphere is pictured at its full size in every frame, so a tall thin tree fills only part of each frame.

## In forest_scene

Pine, spruce, birch and oak have impostors in `poligon-assets/impostors` (baked with `impostor_baker ... --keep-origin`). Their levels: full mesh to 28 m, medium to 70 m, low to 150 m, impostor to 1700 m, each fading over the last tenth. Big trees now stand out to 1200 m from the start (about 55000 of them; undergrowth stays within 250 m, physics within 500 m), and the open wedge east of the start ends at 400 m, so the far forest shows in the panorama. Measured (headless, release, 150 frames): 7.7 ms from the brow of the hill with the forest to the horizon (9.9 ms with meshes out to 450 m and nothing beyond), 13.1 ms inside the forest.

## The trees of forest_scene

Pine, spruce, birch, oak, young pine and young spruce, all generated with `tools/assets/make_tree.mjs` from the recipes in `tools/assets/trees/` (see [rendering.md](rendering.md)), each with three levels made from the same tree. Twigs: Poly Haven `fir_tree_01` (spruce) and `pine_tree_01` (pine), EZ-Tree's aspen and oak twigs; bark: Poly Haven `fir_tree_01` (spruce), ambientCG `Bark014` (pine), EZ-Tree's birch and oak bark; all CC0 or shipped with EZ-Tree (MIT).

Triangles per level:

| | full | medium | low |
|---|---|---|---|
| pine 24 m | 20600 | 7900 | 2750 |
| spruce 20 m | 12000 | 4400 | 1900 |
| birch 17 m | 13400 | 5300 | 1900 |
| oak 18 m | 13600 | 5500 | 1900 |
| young pine 5 m | 12600 | 5000 | 1800 |
| young spruce 4 m | 6500 | 3700 | 640 |

The Poly Haven scans they replace had 33000 to 193000 triangles at full detail and up to 15500 at the lowest level. The roughness maps of the Poly Haven twigs are not used: with them the needles looked wet and reflected the blue sky.

## Making simpler levels of a downloaded tree

For models not generated by `make_tree.mjs`: `tools/assets/simplify_model.py` with the word `foliage`:

```
blender -b --factory-startup -P tools/assets/simplify_model.py -- INPUT OUTPUT.gltf 0.1 foliage
```

- Trunk and branches are reduced by collapsing triangles, as before.
- Leaves are treated differently. Collapsing leaf cards thins the crown until the tree looks bare. Instead a share of the cards is removed (always the same ones for the same file) and the rest are enlarged, so that the crown covers the same area. At a tenth of the triangles a pine keeps its outline.
- A mesh counts as leaves when all its materials take their transparency from a texture. Models whose leaves and bark share one mesh are collapsed whole (the Poly Haven jacaranda and island tree were such).
- The simpler levels use the materials of the full model, matched by name; `LoadModelFromFile(path, model, error, false)` reads such a file without its textures.
- Without `foliage` the script first welds corners that lie in the same place: some scans (Poly Haven's `boulder_01`) store every triangle with its own corners, and collapsing stops early on them.
- In `forest_scene` the boulder, the rock set, the stump, the fallen trunk (to a twelfth and a fiftieth of the triangles) and the shrub `shrub_02` (with `foliage`) have two simpler levels in `poligon-assets/lods`, used from 15 m and 45 m and drawn up to 150 m. A file holding several variants (the rock set holds six) is matched to its simpler files by node names.

## What it costs: measurements

`forest_scene`, about 3200 plants and rocks on 140 m by 140 m, GTX 980, 1280 x 720, every effect on, release build, time per frame including the graphics card's work:

| | ms per frame |
|---|---|
| before instancing and levels of detail | 34 |
| with them, shadow images 2048 | 23 to 25 |
| shadow images 1536 | 20 |
| shadow images 1024 (what `forest_scene` uses) | 16 |
| shadows off | 6.6 |

- Shadows are now by far the largest cost: every leaf card is drawn into four shadow images, and the cost follows the number of shadow pixels, not the number of triangles. Simpler meshes for shadows do not help much, because the enlarged cards cover the same area.
- A headless run only measures the whole frame if it ends by reading a frame back; `forest_scene` does. Without that the time is only that of handing the work to the card.
- On the Batorz land with about 21000 things placed (headless, release, 150 frames): 21.4 ms inside the forest and 21.0 ms at the start with the downloaded trees; 18.6 and 16.8 ms with the generated ones. Leaving out one kind at a time showed where the time went: the pines alone cost 5.4 ms, all trees together about 12 ms, rocks, boulders and the fallen trunk (scans with 40000 to 83000 triangles and no simpler levels) about 5 ms. With simpler levels for those (below) both views take 14.9 ms.

## Known limits

- No wind.
- Plants are placed by the caller; rules that follow terrain height, slope and ground type come with the terrain.
- Culling runs on the processor, plant by plant. Fine for tens of thousands of plants; a square kilometer of dense undergrowth will need it on the graphics card.
- Ways to make shadows cheaper that are not built yet: redrawing the far cascades only every few frames, and solid stand-in shapes for crowns in the far cascades.
