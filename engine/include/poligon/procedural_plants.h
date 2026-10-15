#pragma once

#include <cstdint>

#include "poligon/assets.h"
#include "poligon/model.h"
#include "poligon/renderer.h"

namespace poligon {

// Plants made by code, for kinds no free model was found for: water and shore plants. Each
// function returns a model holding a few different clumps side by side, with its own small
// textures, so that MakePlantKind (poligon/plants.h, with split_variants) gives one variant
// per clump. The same seed gives the same model.
//
// - Reeds (Phragmites): dense clumps of tall thin leaves, 1.6 to 2.6 m, some with a
//   feathery brown plume; at the shore and in shallow water.
// - Cattails (Typha): stiffer, wider leaves, 1.2 to 2 m, and stems with a brown cigar-shaped
//   head; in shallow water.
// - Water lilies (Nymphaea): round floating leaves with a notch, 10 to 20 cm across, and a few
//   white flowers; the model lies at height 0, to be placed on the water's surface.

Model MakeReedModel(std::uint32_t seed);
Model MakeCattailModel(std::uint32_t seed);
Model MakeWaterLilyModel(std::uint32_t seed);

// A model made in memory, on the graphics card as AssetLibrary::LoadModel would give it (no
// simpler levels, no impostor), so that MakePlantKind can take it.
LoadedModel UploadLoadedModel(Renderer& renderer, const Model& model);

} // namespace poligon
