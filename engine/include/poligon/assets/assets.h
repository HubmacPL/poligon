#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "poligon/assets/model.h"
#include "poligon/render/renderer.h"
#include "poligon/core/texture_compression.h"

namespace poligon {

// Assets: every texture and model a game uses has a name in one list, the asset list (a
// text file kept in the repository), with its source file and settings. The cooker
// (CookAssets, tools/asset_cooker) turns the sources into files that load fast: textures
// block-compressed with all their mipmaps, models with their meshes, simpler levels,
// materials and compressed textures in one file. A game then loads everything by name
// through one AssetLibrary, from the cooked folder or from an archive made of it.
//
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

// --- Loading ---

// A model loaded from the cooked files: its render model, the model data (meshes,
// instances, skeleton, clips; vertices are kept for physics and placement), the simpler
// levels and the impostor.
struct LoadedModel {
    // A cooked model has no clips here (model.animations is empty): they come built, in
    // `animations`.
    Model model;
    RenderModel render;
    // The skeleton and clips ready to play, loaded from the cooked ozz-animation archive
    // without building; null when the model has no clips. Shared by every character of the model.
    std::shared_ptr<const AnimationSet> animations;
    // One entry per simpler level: its model data (vertices only, no textures) and meshes
    // on the graphics card. Its materials are the full model's, matched by name.
    std::vector<Model> lod_models;
    std::vector<std::vector<MeshHandle>> lod_meshes;
    ImpostorHandle impostor;
};

class AssetLibrary {
public:
    AssetLibrary();
    ~AssetLibrary();
    AssetLibrary(const AssetLibrary&) = delete;
    AssetLibrary& operator=(const AssetLibrary&) = delete;

    // Opens cooked assets: a folder made by CookAssets, or an archive made by PackAssets.
    bool Open(const char* path, std::string& error);
    bool Exists(const std::string& name) const;

    // Loads by name; a second call gives the same object. Return an empty handle / nullptr
    // and set `error` when the asset is missing or broken.
    TextureHandle LoadTexture(Renderer& renderer, const std::string& name, std::string& error);
    const CompressedImage* LoadImage(const std::string& name, std::string& error);
    const LoadedModel* LoadModel(Renderer& renderer, const std::string& name, std::string& error);

    // The raw bytes of a cooked file ("trees/pine.pmodel").
    bool ReadFile(const std::string& file_name, std::vector<std::uint8_t>& bytes) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace poligon
