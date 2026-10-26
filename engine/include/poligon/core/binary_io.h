#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace poligon {

// Writing and reading our own binary files (cooked assets, land patches): plain values as
// their bytes (little-endian, as the machine has them), arrays and strings with their
// length first. Files are made and read by the same engine on the same kind of machine.

class BinaryWriter {
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

// Reads what a BinaryWriter wrote. Every call returns false (and ok() turns false) when the
// data ends too early.
class BinaryReader {
public:
    BinaryReader(const std::uint8_t* begin, size_t size) : data_(begin), size_(size) {}
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
        if (count > 0) {
            std::memcpy(values.data(), data_ + position_, static_cast<size_t>(count) * sizeof(T));
        }
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
    // Checks that the data starts with `header` (a line of text) and moves past it.
    bool Header(const char* header) {
        const size_t length = std::strlen(header);
        if (position_ + length > size_ || std::memcmp(data_ + position_, header, length) != 0) {
            return Fail();
        }
        position_ += length;
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

} // namespace poligon
