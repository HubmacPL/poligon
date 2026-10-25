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
// The asset list, cooking and packing are the tool side: poligon/assets/cooking.h.

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

// One mesh of a cooked model with its simpler levels, for Renderer::DrawMesh or
// DrawSkinnedMesh with levels of detail (an animal, a building far away): level 0 is the mesh
// that model.model.instances[instance] draws, then each simpler level's mesh of the node with
// the same name. `distances` says where each level ends, full model first, one per level
// (levels without a distance are left out; an empty list gives an empty group); a simpler
// level without that node is left out and the level before it reaches as far. Draw it with the instance's transform and the material of
// its mesh (model.render.mesh_material_index). Skinned simpler levels use the full model's
// skeleton: their joints are matched by name when the model loads.
MeshLodGroup MakeMeshLodGroup(const LoadedModel& model, int instance, const std::vector<float>& distances);

} // namespace poligon
