#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "poligon/animation/animation.h"
#include "poligon/core/image.h"
#include "poligon/core/math.h"
#include "poligon/render/renderer.h"

namespace poligon {

// A 3D model loaded from a file, held in ordinary memory. Loading needs no graphics card,
// so models can be inspected and tested without one; UploadModel hands the data to a Renderer.
//
// Files are read with the Assimp library, which understands glTF/GLB, FBX, OBJ, DAE and about
// forty other formats. Whatever the file used, the loaded data follows Poligon's conventions:
// right-handed, +Y up, counter-clockwise front faces, texture coordinate (0, 0) at the top-left.
// A file's skeleton and animation clips are loaded too (see poligon/animation/animation.h).

struct ModelMaterial {
    std::string name;
    Color base_color;  // linear
    // See Material in renderer.h. Set from the file's alpha mode: "mask" gives the file's
    // cutoff, "blend" (true transparency, not supported yet) is drawn as a cutout at 0.5.
    float alpha_cutoff = 0.0f;
    bool double_sided = false;
    // See Material in renderer.h. Not part of model files; set by code (grass made by code).
    bool back_faces_keep_normal = false;
    bool casts_ambient_occlusion = true;
    float normal_up = 0.0f;
    // The base color texture, already decoded. Empty (width 0) when the material has none
    // or when the image could not be loaded (the reason is then in Model::warnings).
    Image base_color_texture;

    // The rest of the glTF material; see Material in renderer.h. Images are empty when absent.
    float metallic = 0.0f;
    float roughness = 0.8f;
    Image normal_texture;
    Image orm_texture;  // red: occlusion (if occlusion_in_orm), green: roughness, blue: metallic
    bool occlusion_in_orm = false;
    // Light the surface gives off (glTF emissiveFactor, KHR_materials_emissive_strength and
    // emissiveTexture; the emissive color of other formats). Black when the file has none.
    Color emissive{0.0f, 0.0f, 0.0f, 1.0f};
    float emissive_strength = 1.0f;
    Image emissive_texture;
};

// One piece of geometry with a single material. Vertices are in the mesh's own space.
struct ModelMesh {
    std::string name;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    int material_index = 0;
    // For a mesh that bends with the model's skeleton: one entry per vertex, saying which
    // joints it follows. Empty for a rigid mesh.
    std::vector<SkinVertex> skin;
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
    // Joints of the file's skeleton, and its animation clips. Empty when the file has no
    // skinned mesh. Joints are the bones plus every node above them in the file's tree.
    Skeleton skeleton;
    std::vector<AnimationClip> animations;

    // Axis-aligned box around all instances, in model space (skinned meshes in their rest pose).
    glm::vec3 bounds_min;
    glm::vec3 bounds_max;
    // Things that loaded but not fully, for example a missing texture file.
    std::vector<std::string> warnings;

    std::int64_t GetTriangleCount() const;
    std::int64_t GetVertexCount() const;
};


// A model on the graphics card, ready to draw.
struct RenderModel {
    std::vector<MeshHandle> meshes;       // parallel to Model::meshes
    std::vector<Material> materials;      // parallel to Model::materials
    std::vector<int> mesh_material_index; // parallel to Model::meshes
    std::vector<char> mesh_is_skinned;    // parallel to Model::meshes, 1 = bends with the skeleton
    std::vector<ModelInstance> instances;
    // Joint matrices of the rest pose, used when a skinned model is drawn without a pose.
    std::vector<glm::mat4> rest_joint_matrices;
};

// Copies meshes and textures to the graphics card. Call outside BeginFrame/EndFrame.
RenderModel UploadModel(Renderer& renderer, const Model& model);

// Frees the model's meshes and textures on the graphics card and empties `model`.
void DestroyRenderModel(Renderer& renderer, RenderModel& model);

// Draws every instance of the model. `transform` places the whole model in the world.
// A skinned model is drawn in its rest pose. `object_id` as in Renderer::DrawMesh: nonzero
// for a model that moves, so that temporal anti-aliasing follows it.
void DrawModel(Renderer& renderer, const RenderModel& model, const glm::mat4& transform, std::uint64_t object_id = 0);
// Draws a skinned model in a pose: `joint_matrices` from ComputeJointMatrices, one per joint
// of Model::skeleton. Rigid meshes of the model are drawn as usual.
void DrawModel(Renderer& renderer, const RenderModel& model, const glm::mat4& transform,
               const std::vector<glm::mat4>& joint_matrices, std::uint64_t object_id = 0);

} // namespace poligon
