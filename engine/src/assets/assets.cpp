#include "poligon/assets/assets.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "poligon/render/impostor.h"

namespace poligon {

namespace {

namespace fs = std::filesystem;

// Raise whenever cooked files change for the same sources: the cooked folder keeps this
// number in cook_version.txt, and a different number recooks everything.
constexpr int kCookVersion = 1;

// --- Binary writing and reading for cooked files ---

class Writer {
public:
    template <typename T>
    void Raw(const T& value) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
        data.insert(data.end(), bytes, bytes + sizeof(T));
    }
    template <typename T>
    void Array(const std::vector<T>& values) {
        Raw(static_cast<std::uint32_t>(values.size()));
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(values.data());
        data.insert(data.end(), bytes, bytes + values.size() * sizeof(T));
    }
    void String(const std::string& text) {
        Raw(static_cast<std::uint32_t>(text.size()));
        data.insert(data.end(), text.begin(), text.end());
    }
    std::vector<std::uint8_t> data;
};

class Reader {
public:
    Reader(const std::uint8_t* begin, size_t size) : data_(begin), size_(size) {}
    template <typename T>
    bool Raw(T& value) {
        if (position_ + sizeof(T) > size_) {
            return Fail();
        }
        std::memcpy(&value, data_ + position_, sizeof(T));
        position_ += sizeof(T);
        return true;
    }
    template <typename T>
    bool Array(std::vector<T>& values) {
        std::uint32_t count = 0;
        if (!Raw(count) || position_ + static_cast<size_t>(count) * sizeof(T) > size_) {
            return Fail();
        }
        values.resize(count);
        std::memcpy(values.data(), data_ + position_, static_cast<size_t>(count) * sizeof(T));
        position_ += static_cast<size_t>(count) * sizeof(T);
        return true;
    }
    bool String(std::string& text) {
        std::uint32_t count = 0;
        if (!Raw(count) || position_ + count > size_) {
            return Fail();
        }
        text.assign(reinterpret_cast<const char*>(data_ + position_), count);
        position_ += count;
        return true;
    }
    bool ok() const { return ok_; }

private:
    bool Fail() {
        ok_ = false;
        return false;
    }
    const std::uint8_t* data_;
    size_t size_;
    size_t position_ = 0;
    bool ok_ = true;
};

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
    writer.Raw(static_cast<std::uint32_t>(model.animations.size()));
    for (const AnimationClip& clip : model.animations) {
        writer.String(clip.name);
        writer.Raw(clip.duration_seconds);
        writer.Raw(static_cast<std::uint32_t>(clip.channels.size()));
        for (const AnimationChannel& channel : clip.channels) {
            writer.Raw(static_cast<std::int32_t>(channel.joint));
            writer.Array(channel.translation_times);
            writer.Array(channel.translations);
            writer.Array(channel.rotation_times);
            writer.Array(channel.rotations);
            writer.Array(channel.scale_times);
            writer.Array(channel.scales);
        }
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
            !reader.Raw(occlusion_in_orm)) {
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
    if (!reader.Raw(count)) {
        return false;
    }
    model.animations.resize(count);
    for (AnimationClip& clip : model.animations) {
        std::uint32_t channel_count = 0;
        if (!reader.String(clip.name) || !reader.Raw(clip.duration_seconds) || !reader.Raw(channel_count)) {
            return false;
        }
        clip.channels.resize(channel_count);
        for (AnimationChannel& channel : clip.channels) {
            std::int32_t joint = 0;
            if (!reader.Raw(joint) || !reader.Array(channel.translation_times) || !reader.Array(channel.translations) ||
                !reader.Array(channel.rotation_times) || !reader.Array(channel.rotations) || !reader.Array(channel.scale_times) ||
                !reader.Array(channel.scales)) {
                return false;
            }
            channel.joint = joint;
        }
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

std::string CookedFileName(const AssetEntry& entry) {
    return entry.name + (entry.kind == AssetKind::Texture ? ".ptex" : ".pmodel");
}

bool ReadWholeFile(const fs::path& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool IsValidName(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.find("..") != std::string::npos) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '/' ||
               c == '.';
    });
}

// The texture format a model's image gets: cut-out colors keep their alpha.
CompressedImage CompressModelImage(const Image& image, TextureFormat format, bool is_color, float coverage_cutoff,
                                   int max_size) {
    TextureCompressionOptions options;
    options.format = (image.width % 4 == 0 && image.height % 4 == 0) ? format : TextureFormat::Rgba8;
    options.is_color = is_color;
    options.coverage_cutoff = coverage_cutoff;
    options.max_size = max_size;
    return CompressImage(image, options);
}

bool CookModel(const AssetEntry& entry, const fs::path& source_folder, const fs::path& output, std::string& error) {
    Model model;
    if (!LoadModelFromFile((source_folder / entry.source).string().c_str(), model, error)) {
        return false;
    }
    Writer writer;
    const std::string header = "poligon-model 1\n";
    writer.data.insert(writer.data.end(), header.begin(), header.end());
    WriteModelData(writer, model);
    // Each material's three images, compressed: color (BC1, or BC3 for cut-outs), normal
    // map (BC5: x and y), occlusion-roughness-metallic (BC1).
    for (const ModelMaterial& material : model.materials) {
        const Image* images[3] = {&material.base_color_texture, &material.normal_texture, &material.orm_texture};
        for (int slot = 0; slot < 3; ++slot) {
            const Image& image = *images[slot];
            writer.Raw(static_cast<std::uint8_t>(image.width > 0 ? 1 : 0));
            if (image.width == 0) {
                continue;
            }
            const TextureFormat format = slot == 0 ? (material.alpha_cutoff > 0.0f ? TextureFormat::Bc3 : TextureFormat::Bc1)
                                         : slot == 1 ? TextureFormat::Bc5
                                                     : TextureFormat::Bc1;
            WriteImage(writer, CompressModelImage(image, format, slot == 0, slot == 0 ? material.alpha_cutoff : 0.0f,
                                                  entry.texture_max_size));
        }
    }
    // Simpler levels: mesh data only.
    writer.Raw(static_cast<std::uint32_t>(entry.lods.size()));
    for (const std::string& lod : entry.lods) {
        Model simpler;
        if (!LoadModelFromFile((source_folder / lod).string().c_str(), simpler, error, false)) {
            error = lod + ": " + error;
            return false;
        }
        WriteModelData(writer, simpler);
    }
    // The impostor's two images, compressed with their alpha (BC3).
    writer.Raw(static_cast<std::uint8_t>(entry.impostor.empty() ? 0 : 1));
    if (!entry.impostor.empty()) {
        Impostor impostor;
        if (!LoadImpostor((source_folder / entry.impostor).string().c_str(), impostor, error)) {
            return false;
        }
        writer.Raw(static_cast<std::int32_t>(impostor.frames_per_side));
        writer.Raw(impostor.center);
        writer.Raw(impostor.radius);
        WriteImage(writer, CompressModelImage(impostor.color, TextureFormat::Bc3, true, 0.5f, 0));
        WriteImage(writer, CompressModelImage(impostor.normal, TextureFormat::Bc3, false, 0.0f, 0));
    }
    std::error_code ignored;
    fs::create_directories(output.parent_path(), ignored);
    std::ofstream file(output, std::ios::binary);
    file.write(reinterpret_cast<const char*>(writer.data.data()), static_cast<std::streamsize>(writer.data.size()));
    if (!file) {
        error = "cannot write " + output.string();
        return false;
    }
    return true;
}

bool CookTexture(const AssetEntry& entry, const fs::path& source_folder, const fs::path& output, std::string& error) {
    Image image;
    if (!LoadImageFromFile((source_folder / entry.source).string().c_str(), image, error)) {
        return false;
    }
    TextureCompressionOptions options;
    options.format = entry.format;
    options.is_color = entry.is_color;
    options.coverage_cutoff = entry.coverage_cutoff;
    options.max_size = entry.max_size;
    Image sized = image;
    while (options.max_size > 0 && (sized.width > options.max_size || sized.height > options.max_size)) {
        sized = MakeHalfSizeImage(sized, options.is_color);
    }
    if (options.format != TextureFormat::Rgba8 && (sized.width % 4 != 0 || sized.height % 4 != 0)) {
        error = "block compression needs a size that is a multiple of 4";
        return false;
    }
    std::error_code ignored;
    fs::create_directories(output.parent_path(), ignored);
    if (!SaveCompressedImage(output.string().c_str(), CompressImage(sized, options))) {
        error = "cannot write " + output.string();
        return false;
    }
    return true;
}

} // namespace

const AssetEntry* AssetList::Find(const std::string& name) const {
    for (const AssetEntry& entry : entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

bool ParseAssetList(const std::string& text, AssetList& list, std::string& error) {
    AssetList parsed;
    std::istringstream stream(text);
    std::string line;
    int line_number = 0;
    std::set<std::string> names;
    const auto fail = [&](const std::string& message) {
        error = "line " + std::to_string(line_number) + ": " + message;
        return false;
    };
    bool header_seen = false;
    while (std::getline(stream, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream words(line);
        std::string key;
        if (!(words >> key) || key[0] == '#') {
            continue;
        }
        if (!header_seen) {
            int version = 0;
            if (key != "poligon-assets" || !(words >> version) || version != 1) {
                return fail("the file must start with \"poligon-assets 1\"");
            }
            header_seen = true;
            continue;
        }
        std::string value;
        words >> value;
        std::string extra;
        if (key == "texture" || key == "model") {
            if (value.empty() || !IsValidName(value)) {
                return fail("\"" + key + "\" needs a name of letters, digits, _ - . and /");
            }
            if (!names.insert(value).second) {
                return fail("the name \"" + value + "\" is used twice");
            }
            AssetEntry entry;
            entry.kind = key == "texture" ? AssetKind::Texture : AssetKind::Model;
            entry.name = value;
            entry.line = line_number;
            parsed.entries.push_back(entry);
            continue;
        }
        if (parsed.entries.empty()) {
            return fail("\"" + key + "\" before any texture or model");
        }
        AssetEntry& entry = parsed.entries.back();
        const bool is_texture = entry.kind == AssetKind::Texture;
        if (value.empty()) {
            return fail("\"" + key + "\" needs a value");
        }
        if (words >> extra) {
            return fail("\"" + key + "\" takes one value");
        }
        char* end = nullptr;
        if (key == "source") {
            entry.source = value;
        } else if (key == "format" && is_texture) {
            if (!ParseTextureFormat(value.c_str(), entry.format)) {
                return fail("unknown format \"" + value + "\" (rgba8, bc1, bc3, bc4, bc5)");
            }
        } else if (key == "color" && is_texture && (value == "0" || value == "1")) {
            entry.is_color = value == "1";
        } else if (key == "cutout" && is_texture) {
            entry.coverage_cutoff = std::strtof(value.c_str(), &end);
            if (*end != '\0' || entry.coverage_cutoff < 0.0f || entry.coverage_cutoff >= 1.0f) {
                return fail("\"cutout\" needs a number from 0 to 1");
            }
        } else if (key == "max_size") {
            const long size = std::strtol(value.c_str(), &end, 10);
            if (*end != '\0' || size < 0 || size > 16384) {
                return fail("\"max_size\" needs a whole number up to 16384");
            }
            (is_texture ? entry.max_size : entry.texture_max_size) = static_cast<int>(size);
        } else if (key == "lod" && !is_texture) {
            entry.lods.push_back(value);
        } else if (key == "impostor" && !is_texture) {
            entry.impostor = value;
        } else {
            return fail("unknown setting \"" + key + "\" for a " + (is_texture ? "texture" : "model"));
        }
    }
    if (!header_seen) {
        error = "the file is empty";
        return false;
    }
    for (const AssetEntry& entry : parsed.entries) {
        if (entry.source.empty()) {
            error = "line " + std::to_string(entry.line) + ": \"" + entry.name + "\" has no source";
            return false;
        }
    }
    list = std::move(parsed);
    return true;
}

bool LoadAssetList(const char* path, AssetList& list, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = std::string("cannot open ") + path;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!ParseAssetList(text, list, error)) {
        error = std::string(path) + ": " + error;
        return false;
    }
    return true;
}

CookReport CookAssets(const AssetList& list, const char* list_path, const char* source_folder, const char* cooked_folder) {
    CookReport report;
    std::error_code ec;
    const fs::file_time_type list_time = list_path != nullptr ? fs::last_write_time(list_path, ec) : fs::file_time_type::min();
    const fs::path stamp = fs::path(cooked_folder) / "cook_version.txt";
    std::string stamped;
    {
        std::ifstream stamp_file(stamp);
        std::getline(stamp_file, stamped);
    }
    const bool same_cooker = stamped == std::to_string(kCookVersion);
    for (const AssetEntry& entry : list.entries) {
        const fs::path output = fs::path(cooked_folder) / CookedFileName(entry);
        // Up to date when the cooked file is newer than the list and every source.
        bool fresh = same_cooker && fs::exists(output, ec);
        if (fresh) {
            const fs::file_time_type cooked_time = fs::last_write_time(output, ec);
            std::vector<std::string> sources = {entry.source};
            sources.insert(sources.end(), entry.lods.begin(), entry.lods.end());
            if (!entry.impostor.empty()) {
                sources.push_back(entry.impostor);
            }
            fresh = list_path == nullptr || cooked_time > list_time;
            for (const std::string& source : sources) {
                const fs::path path = fs::path(source_folder) / source;
                fresh = fresh && fs::exists(path, ec) && cooked_time > fs::last_write_time(path, ec);
            }
        }
        if (fresh) {
            ++report.up_to_date;
            continue;
        }
        std::string error;
        const bool ok = entry.kind == AssetKind::Texture ? CookTexture(entry, source_folder, output, error)
                                                         : CookModel(entry, source_folder, output, error);
        if (ok) {
            ++report.cooked;
        } else {
            report.errors.push_back(entry.name + ": " + error);
        }
    }
    fs::create_directories(cooked_folder, ec);
    std::ofstream(stamp) << kCookVersion << "\n";
    return report;
}

bool PackAssets(const char* cooked_folder, const char* archive_path, std::string& error) {
    std::vector<std::pair<std::string, fs::path>> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(cooked_folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file()) {
            files.emplace_back(fs::relative(it->path(), cooked_folder, ec).generic_string(), it->path());
        }
    }
    if (ec) {
        error = std::string("cannot read ") + cooked_folder;
        return false;
    }
    std::sort(files.begin(), files.end());
    // Header, then a table of (name hash, offset, size), then the files.
    std::ofstream archive(archive_path, std::ios::binary);
    if (!archive) {
        error = std::string("cannot write ") + archive_path;
        return false;
    }
    const char magic[8] = {'P', 'O', 'L', 'I', 'P', 'A', 'K', '1'};
    archive.write(magic, 8);
    const std::uint32_t count = static_cast<std::uint32_t>(files.size());
    archive.write(reinterpret_cast<const char*>(&count), 4);
    std::uint64_t offset = 12 + static_cast<std::uint64_t>(count) * 24;
    std::vector<std::vector<std::uint8_t>> contents(files.size());
    std::set<std::uint64_t> hashes;
    for (size_t i = 0; i < files.size(); ++i) {
        if (!ReadWholeFile(files[i].second, contents[i])) {
            error = "cannot read " + files[i].second.string();
            return false;
        }
        const std::uint64_t hash = HashName(files[i].first);
        if (!hashes.insert(hash).second) {
            error = "two names give the same hash: " + files[i].first;
            return false;
        }
        const std::uint64_t size = contents[i].size();
        archive.write(reinterpret_cast<const char*>(&hash), 8);
        archive.write(reinterpret_cast<const char*>(&offset), 8);
        archive.write(reinterpret_cast<const char*>(&size), 8);
        offset += size;
    }
    for (const std::vector<std::uint8_t>& content : contents) {
        archive.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
    }
    if (!archive) {
        error = std::string("cannot write ") + archive_path;
        return false;
    }
    return true;
}

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
    const std::string header = "poligon-model 1\n";
    if (bytes.size() < header.size() || std::memcmp(bytes.data(), header.data(), header.size()) != 0) {
        error = name + ": not a poligon model file";
        return nullptr;
    }
    auto loaded = std::make_unique<LoadedModel>();
    Reader reader(bytes.data() + header.size(), bytes.size() - header.size());
    if (!ReadModelData(reader, loaded->model)) {
        error = name + ": the model file is broken";
        return nullptr;
    }
    std::vector<CompressedImage> images(loaded->model.materials.size() * 3);
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
        if (present[m * 3]) {
            material.texture = renderer.CreateTexture(images[m * 3]);
        }
        if (present[m * 3 + 1]) {
            material.normal_texture = renderer.CreateTexture(images[m * 3 + 1]);
        }
        if (present[m * 3 + 2]) {
            material.orm_texture = renderer.CreateTexture(images[m * 3 + 2]);
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
