// Model data on the graphics card: uploading, drawing and freeing it (poligon/assets/model.h).
#include "poligon/assets/model.h"

#include "poligon/core/fatal_error.h"

namespace poligon {

std::int64_t Model::GetTriangleCount() const {
    std::int64_t count = 0;
    for (const ModelInstance& instance : instances) {
        count += static_cast<std::int64_t>(meshes[static_cast<size_t>(instance.mesh_index)].indices.size() / 3);
    }
    return count;
}

std::int64_t Model::GetVertexCount() const {
    std::int64_t count = 0;
    for (const ModelMesh& mesh : meshes) {
        count += static_cast<std::int64_t>(mesh.vertices.size());
    }
    return count;
}

RenderModel UploadModel(Renderer& renderer, const Model& model) {
    RenderModel result;
    for (const ModelMaterial& source : model.materials) {
        Material material;
        material.color = source.base_color;
        material.alpha_cutoff = source.alpha_cutoff;
        material.double_sided = source.double_sided;
        material.back_faces_keep_normal = source.back_faces_keep_normal;
        material.casts_ambient_occlusion = source.casts_ambient_occlusion;
        material.normal_up = source.normal_up;
        material.metallic = source.metallic;
        material.roughness = source.roughness;
        material.occlusion_in_orm = source.occlusion_in_orm;
        material.emissive = source.emissive;
        material.emissive_strength = source.emissive_strength;
        if (source.base_color_texture.width > 0) {
            TextureOptions color_options;
            color_options.coverage_cutoff = material.alpha_cutoff;
            material.texture = renderer.CreateTexture(source.base_color_texture, color_options);
        }
        // Normal maps and roughness/metallic images hold data, not colors.
        TextureOptions data_options;
        data_options.is_color = false;
        if (source.normal_texture.width > 0) {
            material.normal_texture = renderer.CreateTexture(source.normal_texture, data_options);
        }
        if (source.orm_texture.width > 0) {
            material.orm_texture = renderer.CreateTexture(source.orm_texture, data_options);
        }
        if (source.emissive_texture.width > 0) {
            material.emissive_texture = renderer.CreateTexture(source.emissive_texture);
        }
        result.materials.push_back(material);
    }
    for (const ModelMesh& mesh : model.meshes) {
        const int vertex_count = static_cast<int>(mesh.vertices.size());
        const int index_count = static_cast<int>(mesh.indices.size());
        if (mesh.skin.empty()) {
            result.meshes.push_back(renderer.CreateMesh(mesh.vertices.data(), vertex_count, mesh.indices.data(), index_count));
        } else {
            result.meshes.push_back(renderer.CreateSkinnedMesh(mesh.vertices.data(), mesh.skin.data(), vertex_count,
                                                               mesh.indices.data(), index_count));
        }
        result.mesh_material_index.push_back(mesh.material_index);
        result.mesh_is_skinned.push_back(mesh.skin.empty() ? 0 : 1);
    }
    result.instances = model.instances;
    result.rest_joint_matrices = ComputeJointMatrices(model.skeleton, GetRestPose(model.skeleton));
    return result;
}

void DestroyRenderModel(Renderer& renderer, RenderModel& model) {
    for (const MeshHandle mesh : model.meshes) {
        renderer.DestroyMesh(mesh);
    }
    for (const Material& material : model.materials) {
        for (const TextureHandle texture : {material.texture, material.normal_texture, material.orm_texture, material.emissive_texture}) {
            if (texture.id != 0) {
                renderer.DestroyTexture(texture);
            }
        }
    }
    model = RenderModel{};
}

void DrawModel(Renderer& renderer, const RenderModel& model, const glm::mat4& transform, std::uint64_t object_id) {
    DrawModel(renderer, model, transform, model.rest_joint_matrices, object_id);
}

void DrawModel(Renderer& renderer, const RenderModel& model, const glm::mat4& transform,
               const std::vector<glm::mat4>& joint_matrices, std::uint64_t object_id) {
    for (const ModelInstance& instance : model.instances) {
        const size_t mesh_index = static_cast<size_t>(instance.mesh_index);
        const Material& material = model.materials[static_cast<size_t>(model.mesh_material_index[mesh_index])];
        if (model.mesh_is_skinned[mesh_index] != 0) {
            if (joint_matrices.size() != model.rest_joint_matrices.size()) {
                FatalError("DrawModel: the pose must have one matrix per joint of the model's skeleton");
            }
            // The joints place a skinned mesh; the node it hangs on in the file does not.
            renderer.DrawSkinnedMesh(model.meshes[mesh_index], transform, material, joint_matrices.data(),
                                     static_cast<int>(joint_matrices.size()), object_id);
        } else {
            renderer.DrawMesh(model.meshes[mesh_index], transform * instance.transform, material, object_id);
        }
    }
}

} // namespace poligon
