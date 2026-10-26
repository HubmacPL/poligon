#pragma once

// Private to the asset libraries: the format of cooked files, which the cooker
// (poligon_assets_cook) writes and the library (poligon_assets) reads.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "poligon/assets/assets.h"
#include "poligon/core/binary_io.h"
#include "poligon/core/texture_compression.h"

namespace poligon {
namespace asset_format {

namespace fs = std::filesystem;

// Raise whenever cooked files change for the same sources: the cooked folder keeps this
// number in cook_version.txt, and a different number recooks everything.
// 2: animation clips stored built, as an ozz-animation archive, instead of as keys.
// 3: materials carry emission (a color, a strength and a fourth image).
constexpr int kCookVersion = 3;

// The first bytes of a cooked model file (.pmodel).
inline constexpr const char* kModelFileHeader = "poligon-model 2\n";

// Images per material in a cooked model, in this order: base color, normal map,
// occlusion-roughness-metallic, emission.
constexpr int kModelImageSlots = 4;

// --- Binary writing and reading for cooked files (poligon/core/binary_io.h) ---

using Writer = BinaryWriter;
using Reader = BinaryReader;

void WriteImage(Writer& writer, const CompressedImage& image);

bool ReadImage(Reader& reader, CompressedImage& image);

// Mesh data of a model (no textures): meshes, instances, material names and values.
void WriteModelData(Writer& writer, const Model& model);

bool ReadModelData(Reader& reader, Model& model);

std::uint64_t HashName(const std::string& name);

bool ReadWholeFile(const fs::path& path, std::vector<std::uint8_t>& bytes);

} // namespace asset_format
} // namespace poligon
