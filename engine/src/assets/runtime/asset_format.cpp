// Writing and reading the parts of cooked files (see asset_format.h).
#include "../asset_format.h"

namespace poligon {
namespace asset_format {

void WriteImage(Writer& writer, const CompressedImage& image) {
    writer.Raw(static_cast<std::uint8_t>(image.format));
    writer.Raw(static_cast<std::uint8_t>(image.is_color ? 1 : 0));
    writer.Raw(static_cast<std::int32_t>(image.width));
    writer.Raw(static_cast<std::int32_t>(image.height));
    writer.Raw(static_cast<std::uint32_t>(image.levels.size()));
    for (const std::vector<std::uint8_t>& level : image.levels) {
        writer.Array(level);
    }
}

bool ReadImage(Reader& reader, CompressedImage& image) {
    std::uint8_t format = 0;
    std::uint8_t is_color = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::uint32_t level_count = 0;
    if (!reader.Raw(format) || !reader.Raw(is_color) || !reader.Raw(width) || !reader.Raw(height) || !reader.Raw(level_count) ||
        format > static_cast<std::uint8_t>(TextureFormat::Bc5) || level_count > 16) {
        return false;
    }
    image.format = static_cast<TextureFormat>(format);
    image.is_color = is_color != 0;
    image.width = width;
    image.height = height;
    image.levels.resize(level_count);
    for (std::vector<std::uint8_t>& level : image.levels) {
        if (!reader.Array(level)) {
            return false;
        }
    }
    return true;
}

// Mesh data of a model (no textures): meshes, instances, material names and values.
void WriteModelData(Writer& writer, const Model& model) {
    writer.Raw(static_cast<std::uint32_t>(model.materials.size()));
    for (const ModelMaterial& material : model.materials) {
        writer.String(material.name);
        writer.Raw(material.base_color);
        writer.Raw(material.alpha_cutoff);
        writer.Raw(static_cast<std::uint8_t>(material.double_sided ? 1 : 0));
        writer.Raw(material.metallic);
        writer.Raw(material.roughness);
        writer.Raw(static_cast<std::uint8_t>(material.occlusion_in_orm ? 1 : 0));
        writer.Raw(material.emissive);
        writer.Raw(material.emissive_strength);
    }
    writer.Raw(static_cast<std::uint32_t>(model.meshes.size()));
    for (const ModelMesh& mesh : model.meshes) {
        writer.String(mesh.name);
        writer.Raw(static_cast<std::int32_t>(mesh.material_index));
        writer.Array(mesh.vertices);
        writer.Array(mesh.indices);
        writer.Array(mesh.skin);
    }
    writer.Raw(static_cast<std::uint32_t>(model.instances.size()));
    for (const ModelInstance& instance : model.instances) {
        writer.String(instance.node_name);
        writer.Raw(static_cast<std::int32_t>(instance.mesh_index));
        writer.Raw(instance.transform);
    }
    writer.Raw(static_cast<std::uint32_t>(model.skeleton.joints.size()));
    for (const Joint& joint : model.skeleton.joints) {
        writer.String(joint.name);
        writer.Raw(static_cast<std::int32_t>(joint.parent));
        writer.Raw(joint.rest.translation);
        writer.Raw(joint.rest.rotation);
        writer.Raw(joint.rest.scale);
        writer.Raw(joint.inverse_bind);
    }
    writer.Raw(model.bounds_min);
    writer.Raw(model.bounds_max);
}

bool ReadModelData(Reader& reader, Model& model) {
    std::uint32_t count = 0;
    if (!reader.Raw(count)) {
        return false;
    }
    model.materials.resize(count);
    for (ModelMaterial& material : model.materials) {
        std::uint8_t double_sided = 0;
        std::uint8_t occlusion_in_orm = 0;
        if (!reader.String(material.name) || !reader.Raw(material.base_color) || !reader.Raw(material.alpha_cutoff) ||
            !reader.Raw(double_sided) || !reader.Raw(material.metallic) || !reader.Raw(material.roughness) ||
            !reader.Raw(occlusion_in_orm) || !reader.Raw(material.emissive) || !reader.Raw(material.emissive_strength)) {
            return false;
        }
        material.double_sided = double_sided != 0;
        material.occlusion_in_orm = occlusion_in_orm != 0;
    }
    if (!reader.Raw(count)) {
        return false;
    }
    model.meshes.resize(count);
    for (ModelMesh& mesh : model.meshes) {
        std::int32_t material_index = 0;
        if (!reader.String(mesh.name) || !reader.Raw(material_index) || !reader.Array(mesh.vertices) || !reader.Array(mesh.indices) ||
            !reader.Array(mesh.skin)) {
            return false;
        }
        mesh.material_index = material_index;
        if (material_index < 0 || static_cast<size_t>(material_index) >= model.materials.size()) {
            return false;
        }
    }
    if (!reader.Raw(count)) {
        return false;
    }
    model.instances.resize(count);
    for (ModelInstance& instance : model.instances) {
        std::int32_t mesh_index = 0;
        if (!reader.String(instance.node_name) || !reader.Raw(mesh_index) || !reader.Raw(instance.transform) || mesh_index < 0 ||
            static_cast<size_t>(mesh_index) >= model.meshes.size()) {
            return false;
        }
        instance.mesh_index = mesh_index;
    }
    if (!reader.Raw(count)) {
        return false;
    }
    model.skeleton.joints.resize(count);
    for (Joint& joint : model.skeleton.joints) {
        std::int32_t parent = 0;
        if (!reader.String(joint.name) || !reader.Raw(parent) || !reader.Raw(joint.rest.translation) ||
            !reader.Raw(joint.rest.rotation) || !reader.Raw(joint.rest.scale) || !reader.Raw(joint.inverse_bind)) {
            return false;
        }
        joint.parent = parent;
    }
    return reader.Raw(model.bounds_min) && reader.Raw(model.bounds_max);
}

std::uint64_t HashName(const std::string& name) {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (const char c : name) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 0x100000001B3ull;
    }
    return hash;
}

bool ReadWholeFile(const fs::path& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

} // namespace asset_format
} // namespace poligon
