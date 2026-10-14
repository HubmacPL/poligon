# Terrain

The shape of the land as a height map, where it comes from, and how real elevation data becomes game land.

Headers: `poligon/height_map.h`, `poligon/terrain.h`. Tool: `terrain_tool`. Example: `examples/terrain`. Tests: `tests/height_map_test.cpp`, `tests/terrain_render_test.cpp`.

**State:** land is drawn to the horizon with levels of detail, textured with two mixed ground textures and a steep-slope color, lit and shadowed like everything else, and physics collides with it. Not yet: a painted map of where which ground lies, more than two textures, smoothing of the change between patch sizes, plants placed by rules.

## Drawing

```cpp
poligon::TerrainConfig config;
config.origin = {-3000.0f, -280.0f, -3000.0f};       // world position of the map's first point
config.material.layers[0] = {grass_color, grass_normal, 3.0f};   // texture, normal map, meters per repeat
config.material.layers[1] = {litter_color, litter_normal, 2.5f};
poligon::Terrain terrain(renderer, height_map, config);
// every frame, between BeginFrame and EndFrame:
terrain.Draw(camera);
```

- The land is not one mesh. It is drawn as square **patches** from a tree of squares: the whole map is one square; a square nearer to the camera than its size times `TerrainConfig::detail` is split into four, down to the smallest size (64 cells of the height map). Under the camera there is a vertex per map point; a hillside kilometers away is a few coarse squares. A 6 x 6 km map with a point every meter is 36 million points and is drawn with a few hundred patches.
- Every patch is the same flat grid mesh of 64 x 64 cells. The vertex shader (`terrain.vert.hlsl`) lifts each vertex to the land's height, read from an image that holds the whole map on the graphics card (16 bits per point: 72 MB for 6000 x 6000). No mesh is ever built from the map.
- Where a fine patch meets a coarse one, their edges do not match exactly. Each patch has a **skirt**, a rim hanging down, that hides the gaps.
- The direction the ground faces is worked out per pixel from the height image (`terrain.frag.hlsl`), so distant coarse patches are shaded as finely as near ones.
- Look (`TerrainMaterial`): two ground textures mixed in large irregular patches of noise; each is read at its own size and seven times larger, and the larger reading takes over with distance, which hides the repetition; slopes between `steep_start_degrees` and `steep_end_degrees` fade to `steep_color`.
- Lighting is shared with meshes (`lighting.hlsli`): sun, shadows in all four cascades, sky light, ambient occlusion, fog.
- `Terrain::GetHeight(x, z)`, `GetNormal`, `GetSlopeDegrees` answer for world positions; `GetStats()` counts patches by size.
- Under `Terrain` is `Renderer::CreateTerrain` / `DrawTerrain(terrain, material, patches, count)`; all patches of a call cost one draw call per pass and each is culled on its own.

## Physics

- `PhysicsWorld::AddHeightField(heights, size, spacing, origin)` makes ground from a square grid of heights. A walking character, bodies and rays treat it like any other ground.
- For a large map, give physics only the part where things happen (`CropHeightMap`): `forest_scene` uses 1 x 1 km around the player's start, with fences at its edge.

## In forest_scene

`forest_scene` loads `poligon-assets/terrain/batorz/batorz_6km_1m.heights` (hills from noise if the file is missing). The world's origin is the top of the nearest hill west of the village, 70 m above the valley; the player starts there looking east. About 21000 plants and rocks stand within 250 m of the start, none on slopes over 33 degrees, with a clearing on the top and an open wedge to the east for the view. Measured on the GTX 980: 18 to 20 ms per frame.

## Height maps

- A `HeightMap` is a grid of heights with the same distance (`spacing`, meters) between neighboring points in both directions. x runs along a row, z from row to row; row 0 is the north edge of real data, so north is -Z, as elsewhere in the engine.
- `GetHeight(x, z)` blends the four points around a place; `GetNormal`, `GetSlopeDegrees` (0 level, 45 as steep as it is far), `GetHeightRange`.
- `MakeNoiseHeightMap(width, depth, spacing, seed, hill_size, hill_height)` makes rolling hills that are the same for the same arguments.
- Files: `SaveHeightMap` / `LoadHeightMap` use Poligon's own format, a line of text (`poligon-heightmap 1 WIDTH DEPTH SPACING`) followed by 32-bit floats. A 6000 x 6000 map is 137 MB and loads in a fraction of a second.

## Real elevation data

Source: Poland's Geoportal (GUGiK), the elevation model from airborne laser scanning, one height per meter, bare ground without trees and buildings. The Geoportal says it is free of charge and may be used for any purpose.

- Service (WCS 2.0.1): `https://mapy.geoportal.gov.pl/wss/service/PZGIK/NMT/GRID1/WCS/DigitalTerrainModelFormatTIFF`, coverage `DTM_PL-KRON86-NH_TIFF`.
- Coordinates are in the Polish grid EPSG:2180. In a request, `SUBSET=x(...)` is the **easting** and `SUBSET=y(...)` the **northing**; the other way round gives a file full of zeros and no error.
- `SCALEFACTOR=0.5` returns every second meter.
- The answer is a TIFF with one uncompressed 32-bit float per pixel. `LoadHeightMapFromTiff` reads exactly that and nothing else; it is not a general TIFF reader.
- Ask for pieces of at most 3 x 3 km. One request for 12 x 12 km came back with its eastern half empty.
- A few points come back as 0 ("no data"). `FillMissingHeights` gives them the average of their neighbors.

## The tool

```
terrain_tool join OUT.heights SPACING COLUMNS ROWS FOLDER PREFIX   join tiles PREFIX_c<column>_r<row>.tif, fill pinholes
terrain_tool rescale IN.heights OUT.heights HORIZONTAL VERTICAL    real land -> game land
terrain_tool preview IN.heights OUT.png                            a picture of the relief, lit from the north-west
terrain_tool info IN.heights                                       size, height range, how steep the land is
```

`rescale` with HORIZONTAL 0.5 halves all distances and keeps the heights: a 12 km square becomes 6 km and every slope gets steeper (45 degrees becomes 63). The lowest point is moved to height 0.

## The first real map: Batorz

In `poligon-assets/terrain/batorz` (outside the repository):

- `tiles/`: 16 tiles of 3 x 3 km, one height every 2 m, 137 MB, downloaded on 2026-10-14. The square is 12 x 12 km around 50.861443 N, 22.496135 E (EPSG:2180: east 739948 to 751948, north 332549 to 344549).
- `batorz_real_2m.heights`: the tiles joined, 6000 x 6000 points; heights 218.8 to 320.7 m; 21 pinholes filled.
- `tiles_1m/` and `batorz_6km_1m.heights`: the middle 6 x 6 km with a height every meter (4 tiles, 137 MB, downloaded on 2026-10-14; 6000 x 6000 points, heights 226.0 to 316.1 m, no pinholes). **This is the map the game uses, at scale 1:1.**
- `batorz_6km_2m.heights`: the same area cut from the 2 m data; a spare.
- `batorz_preview.png`: the relief: a river valley through the middle and loess gullies on both sides.

How steep it is, from 40000 places:

| | under 10 degrees | 10 to 25 | 25 to 45 | over 45 |
|---|---|---|---|---|
| real | 83.6% | 13.9% | 2.4% | 0.1% |
| squeezed to 0.7 | 72.3% | 22.6% | 4.4% | 0.7% |
| squeezed to 0.5 | 59.4% | 30.6% | 7.8% | 2.1% |

The walking character climbs up to 50 degrees, so at 0.5 about a fiftieth of the land is walls: the sides of the gullies.
