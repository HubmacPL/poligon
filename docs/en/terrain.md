# Terrain

The shape of the land as a height map, where it comes from, and how real elevation data becomes game land.

Headers: `poligon/height_map.h`. Tool: `terrain_tool`. Test: `tests/height_map_test.cpp`.

**State: the data side.** Height maps can be made, read, joined, repaired, rescaled and looked at. Drawing the terrain, texturing it, colliding with it and planting on it are the next steps.

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
- `batorz_game_1m.heights`: squeezed to half, 6 x 6 km with a point every meter, heights 0 to 101.9 m.
- `batorz_preview.png`: the relief: a river valley through the middle and loess gullies on both sides.

How steep it is, from 40000 places:

| | under 10 degrees | 10 to 25 | 25 to 45 | over 45 |
|---|---|---|---|---|
| real | 83.6% | 13.9% | 2.4% | 0.1% |
| squeezed to 0.7 | 72.3% | 22.6% | 4.4% | 0.7% |
| squeezed to 0.5 | 59.4% | 30.6% | 7.8% | 2.1% |

The walking character climbs up to 50 degrees, so at 0.5 about a fiftieth of the land is walls: the sides of the gullies.
