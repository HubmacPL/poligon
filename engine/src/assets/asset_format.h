#pragma once

// Private to the asset libraries: the format of cooked files, which the cooker
// (poligon_assets_cook) writes and the library (poligon_assets) reads.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "poligon/assets/assets.h"
#include "poligon/core/texture_compression.h"

namespace poligon {
namespace asset_format {

namespace fs = std::filesystem;

// Raise whenever cooked files change for the same sources: the cooked folder keeps this
// number in cook_version.txt, and a different number recooks everything.
// 2: animation clips stored built, as an ozz-animation archive, instead of as keys.
constexpr int kCookVersion = 2;

// The first bytes of a cooked model file (.pmodel).
inline constexpr const char* kModelFileHeader = "poligon-model 1\n";

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

void WriteImage(Writer& writer, const CompressedImage& image);

bool ReadImage(Reader& reader, CompressedImage& image);

// Mesh data of a model (no textures): meshes, instances, material names and values.
void WriteModelData(Writer& writer, const Model& model);

bool ReadModelData(Reader& reader, Model& model);

std::uint64_t HashName(const std::string& name);

bool ReadWholeFile(const fs::path& path, std::vector<std::uint8_t>& bytes);

} // namespace asset_format
} // namespace poligon
