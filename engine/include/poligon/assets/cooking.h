#pragma once

#include <string>
#include <vector>

#include "poligon/assets/assets.h"

namespace poligon {

// The tool side of the asset system, in the library poligon_assets_cook (linked by
// poligon_engine): the asset list, cooking sources into cooked files, packing them into an
// archive. A game only loads (poligon/assets/assets.h).

// The asset list, in the style of the render settings file:
//
//     poligon-assets 1
//
//     # A ground texture.
//     texture ground/forest_floor
//     source polyhaven/textures/forrest_ground_01/forrest_ground_01_diff_2k.jpg
//     format bc1
//
//     # A tree with two simpler levels and a baked impostor.
//     model trees/pine
//     source generated/pine/pine.gltf
//     lod lods/pine/lod1/pine.gltf
//     lod lods/pine/lod2/pine.gltf
//     impostor impostors/pine.impostor
//
// A line "texture NAME" or "model NAME" starts an asset; the lines after it, up to the next
// such line, set it. Paths are relative to the source folder. Names use / and no spaces.

enum class AssetKind { Texture, Model };

struct AssetEntry {
    AssetKind kind = AssetKind::Texture;
    std::string name;
    std::string source;
    // --- Texture ---
    TextureFormat format = TextureFormat::Bc1;
    bool is_color = true;
    float coverage_cutoff = 0.0f;  // see TextureOptions::coverage_cutoff
    int max_size = 0;              // 0 = keep the source's size
    // --- Model ---
    // Simpler levels of detail, most detailed first: files whose nodes are named like the
    // full model's (a simpler level of node "Leaves" replaces node "Leaves").
    std::vector<std::string> lods;
    // A baked impostor file (impostor_baker), drawn as the farthest level.
    std::string impostor;
    // Largest size of the model's textures (0 = keep).
    int texture_max_size = 0;
    // The line in the list file where the asset starts, for error messages.
    int line = 0;
};

struct AssetList {
    std::vector<AssetEntry> entries;
    const AssetEntry* Find(const std::string& name) const;
};

// Reads an asset list. An unknown word, a missing source or a name used twice is an error
// that names the line; nothing is returned then.
bool LoadAssetList(const char* path, AssetList& list, std::string& error);
bool ParseAssetList(const std::string& text, AssetList& list, std::string& error);

// --- Cooking ---

struct CookReport {
    int cooked = 0;    // assets written
    int up_to_date = 0;  // assets whose cooked file was newer than their sources
    std::vector<std::string> errors;
};

// Cooks every asset of the list whose cooked file is missing or older than its sources
// (or the list itself). `source_folder` holds the sources, `cooked_folder` receives one
// file per asset, named after it ("trees/pine" becomes trees/pine.pmodel). A failed asset
// is reported and the others go on.
CookReport CookAssets(const AssetList& list, const char* list_path, const char* source_folder, const char* cooked_folder);

// Packs a cooked folder into one archive file: a table of hashed names (FNV-1a, 64 bits)
// and the files' bytes, not encrypted. AssetLibrary reads it like the folder.
bool PackAssets(const char* cooked_folder, const char* archive_path, std::string& error);

} // namespace poligon
