// Loading cooked assets by name, from a cooked folder or an archive (AssetLibrary).
#include "poligon/assets/assets.h"

#include "poligon/render/impostor.h"
#include "../asset_format.h"

namespace poligon {

using namespace asset_format;

struct AssetLibrary::Impl {
    fs::path folder;
    // For an archive: name hash -> (offset, size), and the whole archive in memory.
    std::map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> archive_table;
    std::vector<std::uint8_t> archive;
    bool is_archive = false;
    std::map<std::string, CompressedImage> images;
    std::map<std::string, TextureHandle> textures;
    std::map<std::string, std::unique_ptr<LoadedModel>> models;
};

AssetLibrary::AssetLibrary() : impl_(std::make_unique<Impl>()) {}
AssetLibrary::~AssetLibrary() = default;

bool AssetLibrary::Open(const char* path, std::string& error) {
    std::error_code ec;
    impl_ = std::make_unique<Impl>();
    if (fs::is_directory(path, ec)) {
        impl_->folder = path;
        return true;
    }
    if (!ReadWholeFile(path, impl_->archive) || impl_->archive.size() < 12 ||
        std::memcmp(impl_->archive.data(), "POLIPAK1", 8) != 0) {
        error = std::string(path) + ": neither a cooked folder nor an archive";
        return false;
    }
    std::uint32_t count = 0;
    std::memcpy(&count, impl_->archive.data() + 8, 4);
    if (12 + static_cast<size_t>(count) * 24 > impl_->archive.size()) {
        error = std::string(path) + ": the archive's table is cut short";
        return false;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint64_t entry[3];
        std::memcpy(entry, impl_->archive.data() + 12 + static_cast<size_t>(i) * 24, 24);
        if (entry[1] + entry[2] > impl_->archive.size()) {
            error = std::string(path) + ": a file lies outside the archive";
            return false;
        }
        impl_->archive_table[entry[0]] = {entry[1], entry[2]};
    }
    impl_->is_archive = true;
    return true;
}

bool AssetLibrary::ReadFile(const std::string& file_name, std::vector<std::uint8_t>& bytes) const {
    if (impl_->is_archive) {
        const auto found = impl_->archive_table.find(HashName(file_name));
        if (found == impl_->archive_table.end()) {
            return false;
        }
        const std::uint8_t* begin = impl_->archive.data() + found->second.first;
        bytes.assign(begin, begin + found->second.second);
        return true;
    }
    return !impl_->folder.empty() && ReadWholeFile(impl_->folder / file_name, bytes);
}

bool AssetLibrary::Exists(const std::string& name) const {
    if (impl_->is_archive) {
        return impl_->archive_table.count(HashName(name + ".ptex")) > 0 || impl_->archive_table.count(HashName(name + ".pmodel")) > 0;
    }
    std::error_code ec;
    return fs::exists(impl_->folder / (name + ".ptex"), ec) || fs::exists(impl_->folder / (name + ".pmodel"), ec);
}

const CompressedImage* AssetLibrary::LoadImage(const std::string& name, std::string& error) {
    const auto found = impl_->images.find(name);
    if (found != impl_->images.end()) {
        return &found->second;
    }
    std::vector<std::uint8_t> bytes;
    if (!ReadFile(name + ".ptex", bytes)) {
        error = "no cooked texture \"" + name + "\"";
        return nullptr;
    }
    CompressedImage image;
    if (!LoadCompressedImageFromMemory(bytes.data(), bytes.size(), image, error)) {
        error = name + ": " + error;
        return nullptr;
    }
    return &(impl_->images[name] = std::move(image));
}

TextureHandle AssetLibrary::LoadTexture(Renderer& renderer, const std::string& name, std::string& error) {
    const auto found = impl_->textures.find(name);
    if (found != impl_->textures.end()) {
        return found->second;
    }
    const CompressedImage* image = LoadImage(name, error);
    if (image == nullptr) {
        return {};
    }
    const TextureHandle texture = renderer.CreateTexture(*image);
    impl_->images.erase(name);  // the pixels now live on the graphics card
    return impl_->textures[name] = texture;
}

const LoadedModel* AssetLibrary::LoadModel(Renderer& renderer, const std::string& name, std::string& error) {
    const auto found = impl_->models.find(name);
    if (found != impl_->models.end()) {
        return found->second.get();
    }
    std::vector<std::uint8_t> bytes;
    if (!ReadFile(name + ".pmodel", bytes)) {
        error = "no cooked model \"" + name + "\"";
        return nullptr;
    }
    const std::string header = kModelFileHeader;
    if (bytes.size() < header.size() || std::memcmp(bytes.data(), header.data(), header.size()) != 0) {
        error = name + ": not a poligon model file";
        return nullptr;
    }
    auto loaded = std::make_unique<LoadedModel>();
    Reader reader(bytes.data() + header.size(), bytes.size() - header.size());
    std::vector<std::uint8_t> animation_archive;
    if (!ReadModelData(reader, loaded->model) || !reader.Array(animation_archive)) {
        error = name + ": the model file is broken";
        return nullptr;
    }
    if (!animation_archive.empty()) {
        std::string animation_error;
        loaded->animations = AnimationSet::LoadArchive(loaded->model.skeleton, animation_archive, animation_error);
        if (loaded->animations == nullptr) {
            error = name + ": " + animation_error;
            return nullptr;
        }
    }
    std::vector<CompressedImage> images(loaded->model.materials.size() * kModelImageSlots);
    std::vector<bool> present(images.size(), false);
    for (size_t i = 0; i < images.size(); ++i) {
        std::uint8_t has = 0;
        if (!reader.Raw(has) || (has != 0 && !ReadImage(reader, images[i]))) {
            error = name + ": the model file is broken";
            return nullptr;
        }
        present[i] = has != 0;
    }
    std::uint32_t lod_count = 0;
    if (!reader.Raw(lod_count) || lod_count > 8) {
        error = name + ": the model file is broken";
        return nullptr;
    }
    loaded->lod_models.resize(lod_count);
    for (Model& lod : loaded->lod_models) {
        if (!ReadModelData(reader, lod)) {
            error = name + ": the model file is broken";
            return nullptr;
        }
    }
    std::uint8_t has_impostor = 0;
    Impostor impostor_shape;
    CompressedImage impostor_color;
    CompressedImage impostor_normal;
    if (!reader.Raw(has_impostor) ||
        (has_impostor != 0 && (!reader.Raw(impostor_shape.frames_per_side) || !reader.Raw(impostor_shape.center) ||
                               !reader.Raw(impostor_shape.radius) || !ReadImage(reader, impostor_color) ||
                               !ReadImage(reader, impostor_normal)))) {
        error = name + ": the model file is broken";
        return nullptr;
    }

    // To the graphics card: meshes and materials as UploadModel makes them, then the
    // compressed textures.
    loaded->render = UploadModel(renderer, loaded->model);
    for (size_t m = 0; m < loaded->render.materials.size(); ++m) {
        Material& material = loaded->render.materials[m];
        const size_t first = m * kModelImageSlots;
        if (present[first]) {
            material.texture = renderer.CreateTexture(images[first]);
        }
        if (present[first + 1]) {
            material.normal_texture = renderer.CreateTexture(images[first + 1]);
        }
        if (present[first + 2]) {
            material.orm_texture = renderer.CreateTexture(images[first + 2]);
        }
        if (present[first + 3]) {
            material.emissive_texture = renderer.CreateTexture(images[first + 3]);
        }
    }
    for (const Model& lod : loaded->lod_models) {
        std::vector<MeshHandle> meshes;
        for (const ModelMesh& mesh : lod.meshes) {
            meshes.push_back(renderer.CreateMesh(mesh.vertices.data(), static_cast<int>(mesh.vertices.size()), mesh.indices.data(),
                                                 static_cast<int>(mesh.indices.size())));
        }
        loaded->lod_meshes.push_back(std::move(meshes));
    }
    if (has_impostor != 0) {
        loaded->impostor = renderer.CreateImpostor(impostor_color, impostor_normal, impostor_shape.frames_per_side,
                                                   impostor_shape.center, impostor_shape.radius);
    }
    const LoadedModel* result = loaded.get();
    impl_->models[name] = std::move(loaded);
    return result;
}

} // namespace poligon
