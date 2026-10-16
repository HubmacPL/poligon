#pragma once

#include <cstdint>

#include "poligon/assets/assets.h"
#include "poligon/assets/model.h"
#include "poligon/render/renderer.h"

namespace poligon {

// Plants made by code, for kinds no free model was found for: water and shore plants. Each
// function returns a model holding a few different clumps side by side, with its own small
// textures, so that MakePlantKind (poligon/world/plants.h, with split_variants) gives one variant
// per clump. The same seed gives the same model.
//
// - Reeds (Phragmites): dense clumps of tall thin leaves, 1.6 to 2.6 m, some with a
//   feathery brown plume; at the shore and in shallow water.
// - Cattails (Typha): stiffer, wider leaves, 1.2 to 2 m, and stems with a brown cigar-shaped
//   head; in shallow water.
// - Water lilies (Nymphaea): round floating leaves with a notch, 10 to 20 cm across, and a few
//   white flowers; the model lies at height 0, to be placed on the water's surface.

Model MakeReedModel(std::uint32_t seed);
// Grass: `tufts` different tufts side by side (2 m apart), each of blades_min to blades_max
// real blades (strips narrowing to a point, three pieces each), height_min to height_max
// meters tall, leaning and curving outwards, in eight slightly different greens with dry
// tips. About 65 triangles a tuft. Short lawn grass: 0.12 to 0.3 m; meadow grass: 0.4 to 0.8.
Model MakeGrassModel(std::uint32_t seed, int tufts, float height_min, float height_max, int blades_min, int blades_max);
// Meadow flowers: three clumps of thin stems with two leaves each and a five-petaled flower,
// white, yellow and purple, 0.25 to 0.55 m tall.
Model MakeFlowerModel(std::uint32_t seed);
Model MakeCattailModel(std::uint32_t seed);
Model MakeWaterLilyModel(std::uint32_t seed);

// A model made in memory, on the graphics card as AssetLibrary::LoadModel would give it (no
// simpler levels, no impostor), so that MakePlantKind can take it.
LoadedModel UploadLoadedModel(Renderer& renderer, const Model& model);

} // namespace poligon
