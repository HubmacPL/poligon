# Assets and levels

How a game names, prepares and loads its textures and models, how plants are made from models, and the level file.

Headers: `poligon/assets/assets.h` (loading), `poligon/assets/cooking.h` (the list, cooking, packing; tool side), `poligon/core/texture_compression.h`, `poligon/world/plants.h`, `poligon/world/level.h`. Tool: `asset_cooker`. Example: `examples/assets/assets`. Test: `tests/assets/assets_test.cpp`.

**State (decided on 2026-10-14, built from 2026-10-15 to 2026-10-17, all of it):** one list names every asset with its source and settings; the cooker turns sources into fast-loading files with compressed textures; a game loads by name through one library, from the cooked folder or from an archive; plants are made from model assets by the engine; a level file holds the land's settings, the player's start, things put by hand and the plant rules ([vegetation.md](vegetation.md)). Animation clips are cooked built, as an ozz-animation archive. The data folder was split into its source and cooked halves on 2026-10-16 (see below).

## The data folder

Game data lives outside the repository, in `poligon-assets` next to the project folder, in two halves:

- `source/`: what the asset list's `source` lines point at, relative to it. Downloads (`polyhaven/`, `ambientcg/`, `sketchfab/`, `khronos/`), files made by our tools (`generated/` trees, `lods/` simpler levels, `impostors/`, `prepared/` repaired textures) and the elevation data (`terrain/`).
- `cooked/`: what the cooker writes and the game loads. It can be deleted at any time; the next start cooks it again.

Next to them are work folders that are not game data: `previews/` and `thumbnails/` (pictures for judging), `tools/` (EZ-Tree and three.js for make_tree.mjs), `superseded/` (old versions kept for comparison). The generated land of `forest_scene` is a cache in `build/terrain_cache`.

## The asset list

A text file in the repository, in the style of the render settings file (`tools/forest_scene/assets.txt` is the example):

```
poligon-assets 1

texture ground/forest_floor_color
source polyhaven/textures/forrest_ground_01/forrest_ground_01_diff_2k.jpg
format bc1

model trees/pine
source generated/pine/pine.gltf
lod lods/pine/lod1/pine.gltf
lod lods/pine/lod2/pine.gltf
impostor impostors/pine.impostor
```

- A line `texture NAME` or `model NAME` starts an asset; the lines after it set it. Names are made of letters, digits, `_ - . /` and cannot leave the folder; paths are relative to the source folder.
- Texture settings: `source`, `format` (`bc1` color, `bc3` color with transparency, `bc4` one channel, `bc5` normal map, `rgba8` no compression), `color 0|1` (sRGB colors or data), `cutout` (the alpha cutoff, for coverage-preserving mipmaps), `max_size` (larger images are halved until they fit).
- Model settings: `source` (any format Assimp reads), `lod` (simpler levels, most detailed first; their nodes are matched to the full model's by name), `impostor` (a file baked by `impostor_baker`), `max_size` (largest size of the model's textures).
- An unknown word, a missing source, a name used twice or a setting of the wrong kind is an error that names the line (`LoadAssetList`, `ParseAssetList`).

## Cooking

- `CookAssets(list, list_path, source_folder, cooked_folder)` writes one file per asset, named after it (`trees/pine` becomes `trees/pine.pmodel`, `ground/forest_floor_color` becomes `ground/forest_floor_color.ptex`). An asset is cooked again only when its cooked file is missing or older than the list or any of its sources, or when the cooker itself changed (`kCookVersion` in `cook_version.txt`). A failed asset is reported and the others go on. `forest_scene` cooks on start: 31 assets in 15 s the first time, nothing after that.
- `asset_cooker LIST SOURCE_FOLDER COOKED_FOLDER [--level LEVEL [--terrain-cache FOLDER]] [--pack ARCHIVE]` does the same from the command line; `--level` also cooks the streamed world the level names (`CookLevelWorld`, see [streaming.md](streaming.md)): the land is generated (kept in the terrain cache), its ground layer weights made from the level's `ground` blocks, and both cut into patch files `NAME/X_Z.ppatch` with a description `NAME.pworld`. It is cooked again only when the generator's settings, the ground layers or the patch size change.
- Textures (`.ptex`, `SaveCompressedImage`): block-compressed with stb_dxt (public domain / MIT) with every mipmap, computed as `Renderer::CreateTexture` computes them (coverage-preserving for cut-outs); block-compressed chains end at 4 x 4, because Direct3D 12 cannot copy smaller levels of them. Sizes must be multiples of 4.
- Models (`.pmodel`): meshes, instances, materials, the skeleton, the animation clips built by ozz-animation and stored as its archive (`AnimationSet::SaveArchive`; a game loads them ready to play, without building, as `LoadedModel::animations`, and `model.animations` stays empty), each material's values (emission included) and four images compressed (color BC1, or BC3 for cut-outs; normal map BC5; occlusion-roughness-metallic BC1; emission BC1), the simpler levels' meshes and the impostor's two pictures (BC3). Normal maps keep x and y only; shaders rebuild z (`mesh.frag.hlsl`, `terrain.frag.hlsl`), which works for uncompressed maps too.
- Sizes: the 31 assets of `forest_scene` take 225 MB cooked. Compressed ground layers made 2048-pixel layer textures affordable again, and the frame got faster (15.3 to 13.9 ms at the start in high), because the card reads less memory.
- `PackAssets(cooked_folder, archive)` packs the folder into one file for a released game: `POLIPAK1`, a table of (FNV-1a 64-bit hash of the name, offset, size), then the files. Names are not stored and nothing is encrypted (the author's choice of 2026-10-14: it keeps casual hands out, as theHunter's archives do). It copies the files one by one, and `AssetLibrary` reads only the table when it opens an archive and each file when it is asked for (since 2026-10-26; before, the whole archive was held in memory, which a streamed world's hundreds of megabytes of patches rule out). Reads may come from several threads at once.

## Loading by name

```cpp
poligon::AssetLibrary library;
library.Open("../poligon-assets/cooked", error);  // a cooked folder or an archive
const poligon::LoadedModel* pine = library.LoadModel(renderer, "trees/pine", error);
poligon::TextureHandle floor = library.LoadTexture(renderer, "ground/forest_floor_color", error);
const poligon::CompressedImage* layer = library.LoadImage("ground/litter_color", error);  // e.g. for texture arrays
```

- A second load of the same name gives the same object. A missing or broken asset returns nothing and sets `error`.
- `LoadedModel` holds the model data (vertices for physics and placement, skeleton, clips), its `RenderModel`, the simpler levels' meshes (skinned ones too, posed by the full model's skeleton) and the impostor. `MakeMeshLodGroup(loaded, instance, distances)` turns one mesh and its simpler levels into a `MeshLodGroup` for `Renderer::DrawMesh` or `DrawSkinnedMesh` (see rendering.md).
- `Renderer::CreateTexture(const CompressedImage&)` and `CreateTextureArray(const CompressedImage*, count)` take cooked images as they are; `CreateImpostor` has an overload for compressed pictures.

## Plants from models

`MakePlantKind(model, settings, vegetation)` (`poligon/world/plants.h`) turns a loaded model into a kind of plant, rock or dead wood and adds its variants to a `Vegetation`:

- `split_variants`: a file that holds a row of different plants gives one variant per group of meshes whose boxes overlap seen from above (trunk and leaves of one tree stay together); each is moved so that the middle of its foot is at the origin, unless `keep_origin`.
- Levels: the full model, each simpler level (meshes of nodes named like the variant's), then the impostor, at `level_distances`; a level the asset lacks is skipped and the one before it reaches its distance; without an impostor the last mesh level reaches `distance_without_impostor`.
- `wind_bend` and `wind_flutter` go to the materials (flutter to cut-out ones only); `shadow_distance` and `shadow_level_bias` to the species.
- `AddPlantCollider(physics, kind, variant, foot, turn, scale)` adds what blocks the player: `PlantCollider::Trunk` (an upright cylinder of `trunk_radius`, at most 8 m tall), `Block` (a box a little smaller than the variant) or nothing.

This moved the variant grouping, the search for simpler levels and impostors by folder, and the choice of collider out of `forest_scene`; the planting loop became the plant rules (`poligon/world/plant_rules.h`).

## The level file

```
poligon-level 1

terrain.seed 1               # any TerrainGeneratorSettings by name
terrain.relief 110
render_settings look.txt     # optional, relative to this file
player_start lake            # or: player_start X Z TURN (meters on the map, degrees)
object rocks/boulder 3538 5112 40 2.2    # asset, map x, z, turn, scale
world worlds/forest          # optional: the land is streamed from this cooked world
world.patch_size 512
world.background_spacing 64

ground ground/forest_floor   # a ground layer: its textures NAME_color and NAME_normal
tile 3                       # meters after which they repeat
strength 0.35
moisture 0 0.45 0.15         # conditions on the maps, as in plant rules
patch 45 0.45
```

- `LoadLevel`, `ParseLevel`, `SaveLevel` (`poligon/world/level.h`). Unknown words and bad values are errors that name the line; lines whose first word another reader takes (for example the plant rules) are kept in `Level::other_lines`.
- `player_start lake` uses `FindLakeViewpoint` (`poligon/world/terrain_generator.h`): gentle ground 150 to 500 m from the largest lake, as high above it as possible with its middle in view.
- `SetTerrainGeneratorSetting` / `GetTerrainGeneratorSettings` read and write generator settings by name; `terrain_tool generate` uses the same names.
- Ground layers (`ParseGroundLayers`, `poligon/world/terrain.h`) are blocks like plant rules, at most eight, numbered in the order of the file; the two readers pass over each other's blocks. They choose where each ground texture covers the land (`TerrainLayerRule`); a streamed world bakes their weights into its patches.
- `forest_scene` reads `tools/forest_scene/forest.level`: the default land, the start above the lake, three boulders put by hand, 7 ground layers, 18 plant rules, and the streamed world it is cooked into (`worlds/forest`).
