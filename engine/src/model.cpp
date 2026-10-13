#include "poligon/model.h"

#include <cstring>
#include <filesystem>

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace poligon {

namespace {

// Assimp matrices are row-major (a1..a4 is the first row); GLM is indexed [column][row].
glm::mat4 ToGlm(const aiMatrix4x4& m) {
    glm::mat4 result;
    result[0] = {m.a1, m.b1, m.c1, m.d1};
    result[1] = {m.a2, m.b2, m.c2, m.d2};
    result[2] = {m.a3, m.b3, m.c3, m.d3};
    result[3] = {m.a4, m.b4, m.c4, m.d4};
    return result;
}

// Loads one texture of a material, from inside the model file or from a file next to it.
// Tries the given texture kinds in order and uses the first the material has. Returns the
// texture's path as written in the model file, or "" when the material has none of them.
std::string LoadMaterialTexture(const aiScene& scene, const aiMaterial& source,
                                const std::filesystem::path& model_directory, const aiTextureType* kinds,
                                int kind_count, const std::string& material_name, Image& image,
                                std::vector<std::string>& warnings) {
    aiString texture_path;
    bool found = false;
    for (int i = 0; i < kind_count && !found; ++i) {
        found = source.GetTexture(kinds[i], 0, &texture_path) == aiReturn_SUCCESS;
    }
    if (!found) {
        return "";
    }

    std::string error;
    if (const aiTexture* embedded = scene.GetEmbeddedTexture(texture_path.C_Str())) {
        if (embedded->mHeight == 0) {
            // A whole PNG or JPEG file stored in the model; mWidth is its size in bytes.
            if (!LoadImageFromMemory(embedded->pcData, embedded->mWidth, image, error)) {
                warnings.push_back("material \"" + material_name + "\": embedded texture: " + error);
            }
        } else {
            // Raw pixels, stored as blue, green, red, alpha.
            image.width = static_cast<int>(embedded->mWidth);
            image.height = static_cast<int>(embedded->mHeight);
            image.pixels.resize(static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4);
            for (size_t i = 0; i < static_cast<size_t>(image.width) * static_cast<size_t>(image.height); ++i) {
                const aiTexel& texel = embedded->pcData[i];
                image.pixels[i * 4 + 0] = texel.r;
                image.pixels[i * 4 + 1] = texel.g;
                image.pixels[i * 4 + 2] = texel.b;
                image.pixels[i * 4 + 3] = texel.a;
            }
        }
        return texture_path.C_Str();
    }

    const std::filesystem::path file = model_directory / std::filesystem::path(texture_path.C_Str());
    if (!LoadImageFromFile(file.string().c_str(), image, error)) {
        warnings.push_back("material \"" + material_name + "\": " + error);
    }
    return texture_path.C_Str();
}

ModelMaterial ConvertMaterial(const aiScene& scene, const aiMaterial& source,
                              const std::filesystem::path& model_directory, std::vector<std::string>& warnings) {
    ModelMaterial material;
    material.name = source.GetName().C_Str();

    // glTF-style materials have a base color; older formats have a diffuse color.
    aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);
    if (source.Get(AI_MATKEY_BASE_COLOR, color) != aiReturn_SUCCESS) {
        source.Get(AI_MATKEY_COLOR_DIFFUSE, color);
    }
    material.base_color = {color.r, color.g, color.b, color.a};

    aiString alpha_mode;
    if (source.Get(AI_MATKEY_GLTF_ALPHAMODE, alpha_mode) == aiReturn_SUCCESS) {
        if (std::strcmp(alpha_mode.C_Str(), "MASK") == 0) {
            material.alpha_cutoff = 0.5f;
            source.Get(AI_MATKEY_GLTF_ALPHACUTOFF, material.alpha_cutoff);
        } else if (std::strcmp(alpha_mode.C_Str(), "BLEND") == 0) {
            material.alpha_cutoff = 0.5f;
        }
    }
    bool two_sided = false;
    if (source.Get(AI_MATKEY_TWOSIDED, two_sided) == aiReturn_SUCCESS) {
        material.double_sided = two_sided;
    }

    // Formats without these values (OBJ, for example) keep the defaults: a matte non-metal.
    source.Get(AI_MATKEY_METALLIC_FACTOR, material.metallic);
    source.Get(AI_MATKEY_ROUGHNESS_FACTOR, material.roughness);

    const aiTextureType base_color_kinds[] = {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE};
    LoadMaterialTexture(scene, source, model_directory, base_color_kinds, 2, material.name,
                        material.base_color_texture, warnings);

    const aiTextureType normal_kinds[] = {aiTextureType_NORMALS};
    LoadMaterialTexture(scene, source, model_directory, normal_kinds, 1, material.name, material.normal_texture,
                        warnings);

    // glTF packs roughness (green) and metallic (blue) into one image, and usually ambient
    // occlusion (red) into the same one. Occlusion is used only when it is that same image.
    const aiTextureType orm_kinds[] = {aiTextureType_GLTF_METALLIC_ROUGHNESS, aiTextureType_DIFFUSE_ROUGHNESS,
                                       aiTextureType_METALNESS};
    const std::string orm_path = LoadMaterialTexture(scene, source, model_directory, orm_kinds, 3, material.name,
                                                     material.orm_texture, warnings);
    aiString occlusion_path;
    if (!orm_path.empty() && (source.GetTexture(aiTextureType_LIGHTMAP, 0, &occlusion_path) == aiReturn_SUCCESS ||
                              source.GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &occlusion_path) == aiReturn_SUCCESS)) {
        material.occlusion_in_orm = orm_path == occlusion_path.C_Str();
    }
    return material;
}
ModelMesh ConvertMesh(const aiMesh& source) {
    ModelMesh mesh;
    mesh.name = source.mName.C_Str();
    mesh.material_index = static_cast<int>(source.mMaterialIndex);

    mesh.vertices.resize(source.mNumVertices);
    for (unsigned int i = 0; i < source.mNumVertices; ++i) {
        MeshVertex& vertex = mesh.vertices[i];
        vertex.position = {source.mVertices[i].x, source.mVertices[i].y, source.mVertices[i].z};
        if (source.HasNormals()) {
            vertex.normal = {source.mNormals[i].x, source.mNormals[i].y, source.mNormals[i].z};
        }
        if (source.HasTextureCoords(0)) {
            vertex.uv = {source.mTextureCoords[0][i].x, source.mTextureCoords[0][i].y};
        }
    }

    mesh.indices.reserve(static_cast<size_t>(source.mNumFaces) * 3);
    for (unsigned int i = 0; i < source.mNumFaces; ++i) {
        const aiFace& face = source.mFaces[i];
        if (face.mNumIndices == 3) {
            mesh.indices.push_back(face.mIndices[0]);
            mesh.indices.push_back(face.mIndices[1]);
            mesh.indices.push_back(face.mIndices[2]);
        }
    }
    return mesh;
}

// Walks the node tree, multiplying transforms on the way down, and records where each mesh is drawn.
void CollectInstances(const aiNode& node, const glm::mat4& parent_transform, const std::vector<int>& mesh_map,
                      std::vector<ModelInstance>& instances) {
    const glm::mat4 transform = parent_transform * ToGlm(node.mTransformation);
    for (unsigned int i = 0; i < node.mNumMeshes; ++i) {
        const int mesh_index = mesh_map[node.mMeshes[i]];
        if (mesh_index >= 0) {
            ModelInstance instance;
            instance.node_name = node.mName.C_Str();
            instance.mesh_index = mesh_index;
            instance.transform = transform;
            instances.push_back(instance);
        }
    }
    for (unsigned int i = 0; i < node.mNumChildren; ++i) {
        CollectInstances(*node.mChildren[i], transform, mesh_map, instances);
    }
}

void ComputeBounds(Model& model) {
    bool first = true;
    for (const ModelInstance& instance : model.instances) {
        for (const MeshVertex& vertex : model.meshes[static_cast<size_t>(instance.mesh_index)].vertices) {
            const glm::vec3 point = glm::vec3(instance.transform * glm::vec4(vertex.position, 1.0f));
            if (first) {
                model.bounds_min = point;
                model.bounds_max = point;
                first = false;
            } else {
                model.bounds_min = glm::min(model.bounds_min, point);
                model.bounds_max = glm::max(model.bounds_max, point);
            }
        }
    }
}

} // namespace

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

bool LoadModelFromFile(const char* path, Model& model, std::string& error) {
    model = Model{};

    Assimp::Importer importer;
    // Points and lines are dropped; only triangles are kept.
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
    const aiScene* scene = importer.ReadFile(
        path, aiProcess_Triangulate            // polygons become triangles
                  | aiProcess_SortByPType      // needed to drop points and lines (see above)
                  | aiProcess_GenSmoothNormals // only for meshes that have no normals
                  | aiProcess_JoinIdenticalVertices
                  | aiProcess_ImproveCacheLocality
                  | aiProcess_FlipUVs);        // Assimp's (0, 0) is bottom-left, ours is top-left
    if (scene == nullptr || scene->mRootNode == nullptr) {
        error = std::string("cannot load model ") + path + ": " + importer.GetErrorString();
        return false;
    }

    const std::filesystem::path model_directory = std::filesystem::path(path).parent_path();
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        model.materials.push_back(ConvertMaterial(*scene, *scene->mMaterials[i], model_directory, model.warnings));
    }
    if (model.materials.empty()) {
        model.materials.push_back(ModelMaterial{});
    }

    // Maps Assimp's mesh numbers to ours; -1 for meshes without triangles.
    std::vector<int> mesh_map(scene->mNumMeshes, -1);
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        ModelMesh mesh = ConvertMesh(*scene->mMeshes[i]);
        if (mesh.indices.empty() || mesh.vertices.empty()) {
            continue;
        }
        if (mesh.material_index < 0 || mesh.material_index >= static_cast<int>(model.materials.size())) {
            mesh.material_index = 0;
        }
        mesh_map[i] = static_cast<int>(model.meshes.size());
        model.meshes.push_back(std::move(mesh));
    }

    CollectInstances(*scene->mRootNode, glm::mat4(1.0f), mesh_map, model.instances);
    if (model.instances.empty()) {
        error = std::string("model ") + path + " contains no triangles";
        return false;
    }
    if (scene->mNumAnimations > 0) {
        model.warnings.push_back("the file contains animations, which are not loaded yet");
    }

    ComputeBounds(model);
    error.clear();
    return true;
}

RenderModel UploadModel(Renderer& renderer, const Model& model) {
    RenderModel result;
    for (const ModelMaterial& source : model.materials) {
        Material material;
        material.color = source.base_color;
        material.alpha_cutoff = source.alpha_cutoff;
        material.double_sided = source.double_sided;
        material.metallic = source.metallic;
        material.roughness = source.roughness;
        material.occlusion_in_orm = source.occlusion_in_orm;
        if (source.base_color_texture.width > 0) {
            material.texture = renderer.CreateTexture(source.base_color_texture);
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
        result.materials.push_back(material);
    }
    for (const ModelMesh& mesh : model.meshes) {
        result.meshes.push_back(renderer.CreateMesh(mesh.vertices.data(), static_cast<int>(mesh.vertices.size()),
                                                    mesh.indices.data(), static_cast<int>(mesh.indices.size())));
        result.mesh_material_index.push_back(mesh.material_index);
    }
    result.instances = model.instances;
    return result;
}

void DrawModel(Renderer& renderer, const RenderModel& model, const glm::mat4& transform) {
    for (const ModelInstance& instance : model.instances) {
        const size_t mesh_index = static_cast<size_t>(instance.mesh_index);
        renderer.DrawMesh(model.meshes[mesh_index], transform * instance.transform,
                          model.materials[static_cast<size_t>(model.mesh_material_index[mesh_index])]);
    }
}

} // namespace poligon
