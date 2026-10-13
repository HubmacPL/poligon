#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "poligon/image.h"
#include "poligon/math.h"
#include "poligon/renderer.h"

namespace poligon {

// A 3D model loaded from a file, held in ordinary memory. Loading needs no graphics card,
// so models can be inspected and tested without one; UploadModel hands the data to a Renderer.
//
// Files are read with the Assimp library, which understands glTF/GLB, FBX, OBJ, DAE and about
// forty other formats. Whatever the file used, the loaded data follows Poligon's conventions:
// right-handed, +Y up, counter-clockwise front faces, texture coordinate (0, 0) at the top-left.
// Static geometry only for now: skeletons and animations in the file are ignored.

struct ModelMaterial {
    std::string name;
    Color base_color;  // linear
    // See Material in renderer.h. Set from the file's alpha mode: "mask" gives the file's
    // cutoff, "blend" (true transparency, not supported yet) is drawn as a cutout at 0.5.
    float alpha_cutoff = 0.0f;
    bool double_sided = false;
    // The base color texture, already decoded. Empty (width 0) when the material has none
    // or when the image could not be loaded (the reason is then in Model::warnings).
    Image base_color_texture;

    // The rest of the glTF material; see Material in renderer.h. Images are empty when absent.
    float metallic = 0.0f;
    float roughness = 0.8f;
    Image normal_texture;
    Image orm_texture;  // red: occlusion (if occlusion_in_orm), green: roughness, blue: metallic
    bool occlusion_in_orm = false;
};

// One piece of geometry with a single material. Vertices are in the mesh's own space.
struct ModelMesh {
    std::string name;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    int material_index = 0;
};

// Says "draw this mesh here". A model file is a tree of named nodes with transforms; the tree
// is flattened on load, so each instance carries its final transform within the model.
struct ModelInstance {
    std::string node_name;
    int mesh_index = 0;
    glm::mat4 transform;
};

struct Model {
    std::vector<ModelMesh> meshes;
    std::vector<ModelMaterial> materials;
    std::vector<ModelInstance> instances;
    // Axis-aligned box around all instances, in model space.
    glm::vec3 bounds_min;
    glm::vec3 bounds_max;
    // Things that loaded but not fully, for example a missing texture file.
    std::vector<std::string> warnings;

    std::int64_t GetTriangleCount() const;
    std::int64_t GetVertexCount() const;
};

// Returns false and fills `error` if the file is missing, not a known model format, or holds no triangles.
bool LoadModelFromFile(const char* path, Model& model, std::string& error);

// A model on the graphics card, ready to draw.
struct RenderModel {
    std::vector<MeshHandle> meshes;       // parallel to Model::meshes
    std::vector<Material> materials;      // parallel to Model::materials
    std::vector<int> mesh_material_index; // parallel to Model::meshes
    std::vector<ModelInstance> instances;
};

// Copies meshes and textures to the graphics card. Call outside BeginFrame/EndFrame.
RenderModel UploadModel(Renderer& renderer, const Model& model);

// Draws every instance of the model. `transform` places the whole model in the world.
void DrawModel(Renderer& renderer, const RenderModel& model, const glm::mat4& transform);

} // namespace poligon
