// The asset list, cooking sources into cooked files, and packing them into an archive
// (poligon/assets/cooking.h).
#include "poligon/assets/cooking.h"

#include "poligon/animation/animation_build.h"
#include "poligon/assets/model_import.h"
#include "poligon/render/impostor.h"
#include "../asset_format.h"

namespace poligon {

using namespace asset_format;

namespace {

std::string CookedFileName(const AssetEntry& entry) {
    return entry.name + (entry.kind == AssetKind::Texture ? ".ptex" : ".pmodel");
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
    const std::string header = kModelFileHeader;
    writer.data.insert(writer.data.end(), header.begin(), header.end());
    WriteModelData(writer, model);
    // The skeleton and clips, built by ozz-animation and stored as its archive, so that
    // loading needs no building (empty when the model has no clips).
    std::vector<std::uint8_t> animation_archive;
    if (!model.animations.empty() && !model.skeleton.joints.empty()) {
        animation_archive = BuildAnimationSet(model.skeleton, model.animations)->SaveArchive();
    }
    writer.Array(animation_archive);
    // Each material's four images, compressed: color (BC1, or BC3 for cut-outs), normal
    // map (BC5: x and y), occlusion-roughness-metallic (BC1), emission (BC1, a color).
    for (const ModelMaterial& material : model.materials) {
        const Image* images[kModelImageSlots] = {&material.base_color_texture, &material.normal_texture, &material.orm_texture,
                                                 &material.emissive_texture};
        for (int slot = 0; slot < kModelImageSlots; ++slot) {
            const Image& image = *images[slot];
            writer.Raw(static_cast<std::uint8_t>(image.width > 0 ? 1 : 0));
            if (image.width == 0) {
                continue;
            }
            const TextureFormat format = slot == 0 ? (material.alpha_cutoff > 0.0f ? TextureFormat::Bc3 : TextureFormat::Bc1)
                                         : slot == 1 ? TextureFormat::Bc5
                                                     : TextureFormat::Bc1;
            WriteImage(writer, CompressModelImage(image, format, slot == 0 || slot == 3, slot == 0 ? material.alpha_cutoff : 0.0f,
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

} // namespace poligon
