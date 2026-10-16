#pragma once

#include <string>

#include "poligon/assets/model.h"

namespace poligon {

// Reading model files with Assimp: any of the forty-odd formats it knows. Part of the
// tool-side library poligon_assets_cook (linked by poligon_engine): the cooker, the viewer
// and tools read files; a game loads cooked models (poligon/assets/assets.h).
// Returns false and fills `error` if the file is missing, not a known model format, or holds no triangles.
// load_textures = false skips reading and decoding the images, which is most of the loading
// time; for files of which only the meshes are wanted (simpler levels of detail that share
// the textures of the full model). Materials then have no textures.
bool LoadModelFromFile(const char* path, Model& model, std::string& error, bool load_textures = true);

} // namespace poligon
