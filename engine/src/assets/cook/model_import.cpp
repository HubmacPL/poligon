// Reading model files with Assimp (poligon/assets/model_import.h).
#include "poligon/assets/model_import.h"

#include <cstring>
#include <filesystem>
#include <map>
#include <set>

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "poligon/core/fatal_error.h"

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
                              const std::filesystem::path& model_directory, std::vector<std::string>& warnings, bool load_textures) {
    ModelMaterial material;
    material.name = source.GetName().C_Str();

    // glTF-style materials have a base color; older formats have a diffuse color.
    aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);
    if (source.Get(AI_MATKEY_BASE_COLOR, color) != aiReturn_SUCCESS) {
        source.Get(AI_MATKEY_COLOR_DIFFUSE, color);
    }
    material.base_color = {color.r, color.g, color.b, color.a};

    aiString alpha_mode;
    const bool file_states_alpha_mode = source.Get(AI_MATKEY_GLTF_ALPHAMODE, alpha_mode) == aiReturn_SUCCESS;
    if (file_states_alpha_mode) {
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
    if (load_textures) {
        LoadMaterialTexture(scene, source, model_directory, base_color_kinds, 2, material.name,
                            material.base_color_texture, warnings);
    }

    // Formats that cannot say "this material is a cut-out" (FBX, OBJ) still use textures
    // with transparency for fur, whiskers and leaves. If a good part of the texture is
    // transparent, treat the material as a double-sided cut-out; otherwise such parts show
    // as solid rectangles.
    if (!file_states_alpha_mode && material.base_color_texture.width > 0) {
        const std::vector<std::uint8_t>& pixels = material.base_color_texture.pixels;
        size_t transparent = 0;
        for (size_t i = 3; i < pixels.size(); i += 4) {
            transparent += pixels[i] < 128 ? 1 : 0;
        }
        if (transparent * 100 >= pixels.size() / 4) {  // at least 1% of the pixels
            material.alpha_cutoff = 0.5f;
            material.double_sided = true;
        }
    }

    // A cut-out material whose texture has no transparent pixel draws its whole squares, which
    // shows as leaves with a solid background. The usual cause: the texture is a JPEG (no
    // transparency possible) and the mask sits in a separate file the model does not mention.
    if (material.alpha_cutoff > 0.0f && material.base_color_texture.width > 0) {
        bool has_transparent_pixel = false;
        const std::vector<std::uint8_t>& pixels = material.base_color_texture.pixels;
        for (size_t i = 3; i < pixels.size() && !has_transparent_pixel; i += 4) {
            has_transparent_pixel = pixels[i] < 255;
        }
        if (!has_transparent_pixel) {
            warnings.push_back("material \"" + material.name +
                               "\" is a cut-out, but its texture has no transparent pixels; see "
                               "tools/image_tool merge-alpha and tools/assets/set_gltf_cutout.mjs");
        }
    }

    const aiTextureType normal_kinds[] = {aiTextureType_NORMALS};
    if (load_textures) {
        LoadMaterialTexture(scene, source, model_directory, normal_kinds, 1, material.name, material.normal_texture,
                            warnings);
    }

    // glTF packs roughness (green) and metallic (blue) into one image, and usually ambient
    // occlusion (red) into the same one. Occlusion is used only when it is that same image.
    const aiTextureType orm_kinds[] = {aiTextureType_GLTF_METALLIC_ROUGHNESS, aiTextureType_DIFFUSE_ROUGHNESS,
                                       aiTextureType_METALNESS};
    const std::string orm_path =
        load_textures ? LoadMaterialTexture(scene, source, model_directory, orm_kinds, 3, material.name,
                                            material.orm_texture, warnings)
                      : std::string();
    aiString occlusion_path;
    if (!orm_path.empty() && (source.GetTexture(aiTextureType_LIGHTMAP, 0, &occlusion_path) == aiReturn_SUCCESS ||
                              source.GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &occlusion_path) == aiReturn_SUCCESS)) {
        material.occlusion_in_orm = orm_path == occlusion_path.C_Str();
    }
    return material;
}
// True if this node is a bone of some mesh or has one below it. Fills `is_joint` for the
// whole subtree, keyed by node.
bool MarkJointNodes(const aiNode& node, const std::set<std::string>& bone_names,
                    std::map<const aiNode*, bool>& is_joint) {
    bool joint = bone_names.count(node.mName.C_Str()) > 0;
    for (unsigned int i = 0; i < node.mNumChildren; ++i) {
        joint |= MarkJointNodes(*node.mChildren[i], bone_names, is_joint);
    }
    is_joint[&node] = joint;
    return joint;
}

// Adds the marked nodes to the skeleton, parents before children.
void CollectJoints(const aiNode& node, int parent, const std::map<const aiNode*, bool>& is_joint, Skeleton& skeleton) {
    if (!is_joint.at(&node)) {
        return;
    }
    aiVector3D scaling;
    aiQuaternion rotation;
    aiVector3D position;
    node.mTransformation.Decompose(scaling, rotation, position);
    Joint joint;
    joint.name = node.mName.C_Str();
    joint.parent = parent;
    joint.rest.translation = {position.x, position.y, position.z};
    joint.rest.rotation = glm::quat(rotation.w, rotation.x, rotation.y, rotation.z);
    joint.rest.scale = {scaling.x, scaling.y, scaling.z};
    const int index = static_cast<int>(skeleton.joints.size());
    skeleton.joints.push_back(joint);
    for (unsigned int i = 0; i < node.mNumChildren; ++i) {
        CollectJoints(*node.mChildren[i], index, is_joint, skeleton);
    }
}

// Reads which joints each vertex follows. Keeps the four strongest per vertex and makes
// their weights add up to 1.
void ConvertSkin(const aiMesh& source, ModelMesh& mesh, Skeleton& skeleton, std::vector<std::string>& warnings) {
    mesh.skin.assign(mesh.vertices.size(), SkinVertex{});
    for (unsigned int b = 0; b < source.mNumBones; ++b) {
        const aiBone& bone = *source.mBones[b];
        const int joint = skeleton.FindJoint(bone.mName.C_Str());
        if (joint < 0) {
            continue;
        }
        skeleton.joints[static_cast<size_t>(joint)].inverse_bind = ToGlm(bone.mOffsetMatrix);
        for (unsigned int w = 0; w < bone.mNumWeights; ++w) {
            const aiVertexWeight& weight = bone.mWeights[w];
            if (weight.mVertexId >= mesh.skin.size() || weight.mWeight <= 0.0f) {
                continue;
            }
            // Put it in the weakest slot if it is stronger than what is there.
            SkinVertex& skin = mesh.skin[weight.mVertexId];
            int weakest = 0;
            for (int slot = 1; slot < 4; ++slot) {
                if (skin.weights[slot] < skin.weights[weakest]) {
                    weakest = slot;
                }
            }
            if (weight.mWeight > skin.weights[weakest]) {
                skin.joints[weakest] = static_cast<std::uint8_t>(joint);
                skin.weights[weakest] = weight.mWeight;
            }
        }
    }
    int loose_vertices = 0;
    for (SkinVertex& skin : mesh.skin) {
        const float sum = skin.weights[0] + skin.weights[1] + skin.weights[2] + skin.weights[3];
        if (sum <= 0.0f) {
            // Follows no joint: tie it to the root, so that it at least stays in place.
            skin.weights[0] = 1.0f;
            ++loose_vertices;
        } else {
            for (float& weight : skin.weights) {
                weight /= sum;
            }
        }
    }
    if (loose_vertices > 0) {
        warnings.push_back("mesh \"" + mesh.name + "\": " + std::to_string(loose_vertices) +
                           " vertices follow no joint and were tied to the root");
    }
}

AnimationClip ConvertAnimation(const aiAnimation& source, const Skeleton& skeleton, bool& has_foreign_channels) {
    // Key times are in "ticks"; a file that does not say how long a tick is means 1/25 s.
    const double ticks_per_second = source.mTicksPerSecond > 0.0 ? source.mTicksPerSecond : 25.0;
    const auto to_seconds = [ticks_per_second](double ticks) { return static_cast<float>(ticks / ticks_per_second); };

    AnimationClip clip;
    clip.name = source.mName.C_Str();
    clip.duration_seconds = to_seconds(source.mDuration);
    for (unsigned int c = 0; c < source.mNumChannels; ++c) {
        const aiNodeAnim& node = *source.mChannels[c];
        const int joint = skeleton.FindJoint(node.mNodeName.C_Str());
        if (joint < 0) {
            has_foreign_channels = true;
            continue;
        }
        AnimationChannel channel;
        channel.joint = joint;
        for (unsigned int k = 0; k < node.mNumPositionKeys; ++k) {
            const aiVectorKey& key = node.mPositionKeys[k];
            channel.translation_times.push_back(to_seconds(key.mTime));
            channel.translations.push_back({key.mValue.x, key.mValue.y, key.mValue.z});
        }
        for (unsigned int k = 0; k < node.mNumRotationKeys; ++k) {
            const aiQuatKey& key = node.mRotationKeys[k];
            channel.rotation_times.push_back(to_seconds(key.mTime));
            channel.rotations.push_back(glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z));
        }
        for (unsigned int k = 0; k < node.mNumScalingKeys; ++k) {
            const aiVectorKey& key = node.mScalingKeys[k];
            channel.scale_times.push_back(to_seconds(key.mTime));
            channel.scales.push_back({key.mValue.x, key.mValue.y, key.mValue.z});
        }
        clip.channels.push_back(std::move(channel));
    }
    return clip;
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
    const std::vector<glm::mat4> rest_matrices = ComputeJointMatrices(model.skeleton, GetRestPose(model.skeleton));
    bool first = true;
    for (const ModelInstance& instance : model.instances) {
        const ModelMesh& mesh = model.meshes[static_cast<size_t>(instance.mesh_index)];
        for (size_t v = 0; v < mesh.vertices.size(); ++v) {
            const glm::vec4 position(mesh.vertices[v].position, 1.0f);
            glm::vec3 point;
            if (mesh.skin.empty()) {
                point = glm::vec3(instance.transform * position);
            } else {
                // A skinned vertex is placed by its joints, not by the node the mesh hangs on.
                point = glm::vec3(0.0f);
                for (int slot = 0; slot < 4; ++slot) {
                    point += mesh.skin[v].weights[slot] * glm::vec3(rest_matrices[mesh.skin[v].joints[slot]] * position);
                }
            }
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

bool LoadModelFromFile(const char* path, Model& model, std::string& error, bool load_textures) {
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
                  | aiProcess_LimitBoneWeights // at most four joints per vertex
                  | aiProcess_FlipUVs);        // Assimp's (0, 0) is bottom-left, ours is top-left
    if (scene == nullptr || scene->mRootNode == nullptr) {
        error = std::string("cannot load model ") + path + ": " + importer.GetErrorString();
        return false;
    }

    const std::filesystem::path model_directory = std::filesystem::path(path).parent_path();
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        model.materials.push_back(
            ConvertMaterial(*scene, *scene->mMaterials[i], model_directory, model.warnings, load_textures));
    }
    if (model.materials.empty()) {
        model.materials.push_back(ModelMaterial{});
    }

    // The skeleton: every node that is a bone of some mesh, plus the nodes above those.
    std::set<std::string> bone_names;
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        for (unsigned int b = 0; b < scene->mMeshes[i]->mNumBones; ++b) {
            bone_names.insert(scene->mMeshes[i]->mBones[b]->mName.C_Str());
        }
    }
    if (!bone_names.empty()) {
        std::map<const aiNode*, bool> is_joint;
        MarkJointNodes(*scene->mRootNode, bone_names, is_joint);
        CollectJoints(*scene->mRootNode, -1, is_joint, model.skeleton);
        if (model.skeleton.joints.size() > 256) {
            model.warnings.push_back("the skeleton has " + std::to_string(model.skeleton.joints.size()) +
                                     " joints, more than the 256 supported; the model is loaded without it");
            model.skeleton = Skeleton{};
        }
    }

    // Maps Assimp's mesh numbers to ours; -1 for meshes without triangles.
    std::vector<int> mesh_map(scene->mNumMeshes, -1);
    for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
        ModelMesh mesh = ConvertMesh(*scene->mMeshes[i]);
        if (mesh.indices.empty() || mesh.vertices.empty()) {
            continue;
        }
        if (scene->mMeshes[i]->mNumBones > 0 && !model.skeleton.joints.empty()) {
            ConvertSkin(*scene->mMeshes[i], mesh, model.skeleton, model.warnings);
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
    if (!model.skeleton.joints.empty()) {
        bool has_foreign_channels = false;
        for (unsigned int i = 0; i < scene->mNumAnimations; ++i) {
            model.animations.push_back(ConvertAnimation(*scene->mAnimations[i], model.skeleton, has_foreign_channels));
        }
        if (has_foreign_channels) {
            model.warnings.push_back("some animations move nodes outside the skeleton; those parts are ignored");
        }
    } else if (scene->mNumAnimations > 0) {
        model.warnings.push_back("the file has animations but no skinned mesh; animations of rigid parts are not loaded");
    }

    ComputeBounds(model);
    error.clear();
    return true;
}

} // namespace poligon
