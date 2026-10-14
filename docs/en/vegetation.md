# Vegetation

Drawing plants, rocks and other things that stand around in great numbers: instancing, levels of detail, cells.

Headers: `poligon/vegetation.h`, `poligon/renderer.h` (`DrawMeshInstances`). Example: `examples/vegetation`. Tests: `tests/vegetation_render_test.cpp`, the instancing part of `tests/scene_render_test.cpp`.

**State: the foundation.** Done: instancing, levels of detail chosen by distance, cells, separate handling of shadows, a tool that makes simpler levels of trees. Not yet: fading between levels, flat pictures for the farthest trees (impostors), wind, placing plants by rules of the terrain.

## Terms

- **Instancing**: one mesh drawn many times in a single draw call, each copy with its own transform. A forest of a thousand pines of one kind costs a handful of draw calls, not thousands.
- **Level of detail** (LOD): the same plant as several meshes, from the full one for up close to simple ones for far away.
- **Cell**: a square piece of the world (32 m by default) that holds its own plants. Whole cells can be skipped or handed over without looking at single plants.
- **Leaf card**: a small flat piece with a picture of a twig, cut out by transparency. Tree crowns are made of thousands of them.

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
- `Draw` goes through the cells in a fixed order. A cell with nothing in range is skipped; a cell lying wholly within one level is handed over in one piece; only cells that straddle a boundary are looked at plant by plant. Then each species and level becomes one `DrawMeshInstances` call per part.
- `SetDistanceScale(detail, shadow)` multiplies all level distances and shadow distances, for tuning with sliders.
- `GetStats()`: plants per level, cells skipped, batches.
- Meshes and textures belong to the caller; the system only holds handles and transforms.

## Making simpler levels of a tree

`tools/assets/simplify_model.py` with the word `foliage`:

```
blender -b --factory-startup -P tools/assets/simplify_model.py -- INPUT OUTPUT.gltf 0.1 foliage
```

- Trunk and branches are reduced by collapsing triangles, as before.
- Leaves are treated differently. Collapsing leaf cards thins the crown until the tree looks bare. Instead a share of the cards is removed (always the same ones for the same file) and the rest are enlarged, so that the crown covers the same area. At a tenth of the triangles a pine keeps its outline.
- A mesh counts as leaves when all its materials take their transparency from a texture. Models whose leaves and bark share one mesh are collapsed whole (the jacaranda and the island tree at the moment).
- The simpler levels use the materials of the full model, matched by name; `LoadModelFromFile(path, model, error, false)` reads such a file without its textures.

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

## Known limits

- A plant changes level in one frame; there is no fade yet, so the change can be seen when walking.
- No impostors yet: the farthest level is still a mesh.
- No wind.
- Plants are placed by the caller; rules that follow terrain height, slope and ground type come with the terrain.
- Culling runs on the processor, plant by plant. Fine for tens of thousands of plants; a square kilometer of dense undergrowth will need it on the graphics card.
- Ways to make shadows cheaper that are not built yet: redrawing the far cascades only every few frames, and solid stand-in shapes for crowns in the far cascades.
