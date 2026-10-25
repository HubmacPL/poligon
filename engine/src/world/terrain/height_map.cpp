#include "poligon/world/height_map.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "poligon/core/fatal_error.h"

namespace poligon {

namespace {

// A number in 0..1 for every grid point and seed, the same on every machine.
float HashToUnit(int x, int z, std::uint32_t seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(z) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h >> 8) / 16777216.0f;
}

// Smooth noise: random values at whole coordinates, blended in between.
float ValueNoise(float x, float z, std::uint32_t seed) {
    const float floor_x = std::floor(x);
    const float floor_z = std::floor(z);
    const int ix = static_cast<int>(floor_x);
    const int iz = static_cast<int>(floor_z);
    float fx = x - floor_x;
    float fz = z - floor_z;
    // Ease in and out, so that the grid of random values does not show as creases.
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);
    const float a = HashToUnit(ix, iz, seed);
    const float b = HashToUnit(ix + 1, iz, seed);
    const float c = HashToUnit(ix, iz + 1, seed);
    const float d = HashToUnit(ix + 1, iz + 1, seed);
    return glm::mix(glm::mix(a, b, fx), glm::mix(c, d, fx), fz);
}

template <typename T>
T ReadLittleEndian(const std::vector<unsigned char>& bytes, size_t offset) {
    T value{};
    if (offset + sizeof(T) <= bytes.size()) {
        std::memcpy(&value, &bytes[offset], sizeof(T));
    }
    return value;
}

} // namespace

float HeightMap::GetHeight(float x, float z) const {
    if (IsEmpty()) {
        return 0.0f;
    }
    // Grid coordinates of the whole map, so that a window gives the same numbers.
    const float gx = glm::clamp(x / spacing, static_cast<float>(first_x), static_cast<float>(first_x + width - 1));
    const float gz = glm::clamp(z / spacing, static_cast<float>(first_z), static_cast<float>(first_z + depth - 1));
    const int ix = glm::min(static_cast<int>(gx), first_x + width - 2);
    const int iz = glm::min(static_cast<int>(gz), first_z + depth - 2);
    const float fx = gx - static_cast<float>(ix);
    const float fz = gz - static_cast<float>(iz);
    const float* row = &heights[static_cast<size_t>(iz - first_z) * static_cast<size_t>(width) + static_cast<size_t>(ix - first_x)];
    const float* next_row = row + width;
    return glm::mix(glm::mix(row[0], row[1], fx), glm::mix(next_row[0], next_row[1], fx), fz);
}

glm::vec3 HeightMap::GetNormal(float x, float z) const {
    const float step = spacing;
    const float slope_x = (GetHeight(x + step, z) - GetHeight(x - step, z)) / (2.0f * step);
    const float slope_z = (GetHeight(x, z + step) - GetHeight(x, z - step)) / (2.0f * step);
    return glm::normalize(glm::vec3(-slope_x, 1.0f, -slope_z));
}

float HeightMap::GetSlopeDegrees(float x, float z) const {
    return glm::degrees(std::acos(glm::clamp(GetNormal(x, z).y, 0.0f, 1.0f)));
}

void HeightMap::GetHeightRange(float& lowest, float& highest) const {
    lowest = 0.0f;
    highest = 0.0f;
    if (heights.empty()) {
        return;
    }
    lowest = heights[0];
    highest = heights[0];
    for (const float height : heights) {
        lowest = glm::min(lowest, height);
        highest = glm::max(highest, height);
    }
}

HeightMap MakeNoiseHeightMap(int width, int depth, float spacing, std::uint32_t seed, float hill_size, float hill_height) {
    if (width < 2 || depth < 2 || spacing <= 0.0f || hill_size <= 0.0f) {
        FatalError("MakeNoiseHeightMap: needs at least 2 x 2 points and sizes above zero");
    }
    HeightMap map;
    map.width = width;
    map.depth = depth;
    map.spacing = spacing;
    map.heights.resize(static_cast<size_t>(width) * static_cast<size_t>(depth));
    // Five layers: each half as large and half as tall as the one before. The large ones
    // make hills and valleys, the small ones bumps.
    const int layer_count = 5;
    float total_weight = 0.0f;
    for (int layer = 0; layer < layer_count; ++layer) {
        total_weight += 1.0f / static_cast<float>(1 << layer);
    }
    for (int iz = 0; iz < depth; ++iz) {
        for (int ix = 0; ix < width; ++ix) {
            const float x = static_cast<float>(ix) * spacing / hill_size;
            const float z = static_cast<float>(iz) * spacing / hill_size;
            float sum = 0.0f;
            for (int layer = 0; layer < layer_count; ++layer) {
                const float frequency = static_cast<float>(1 << layer);
                sum += ValueNoise(x * frequency, z * frequency, seed + static_cast<std::uint32_t>(layer) * 101u) / frequency;
            }
            map.heights[static_cast<size_t>(iz) * static_cast<size_t>(width) + static_cast<size_t>(ix)] =
                sum / total_weight * hill_height;
        }
    }
    return map;
}

bool LoadHeightMapFromTiff(const char* path, float spacing, HeightMap& map, int& missing_count, std::string& error) {
    map = HeightMap{};
    missing_count = 0;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = std::string("cannot open ") + path;
        return false;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto fail = [&error, path](const char* what) {
        error = std::string(path) + ": " + what;
        return false;
    };
    // "II" = numbers stored lowest byte first, then the number 42.
    if (bytes.size() < 8 || bytes[0] != 'I' || bytes[1] != 'I' || ReadLittleEndian<std::uint16_t>(bytes, 2) != 42) {
        return fail("not a little-endian TIFF file");
    }

    // The file's directory: a list of numbered entries, each a value or the place of a list of values.
    struct Entry {
        std::uint16_t type = 0;
        std::uint32_t count = 0;
        size_t value_offset = 0;  // where the entry's values are in the file
    };
    const auto read_values = [&bytes](const Entry& entry) {
        std::vector<std::uint32_t> values;
        for (std::uint32_t i = 0; i < entry.count; ++i) {
            if (entry.type == 3) {
                values.push_back(ReadLittleEndian<std::uint16_t>(bytes, entry.value_offset + i * 2u));
            } else {
                values.push_back(ReadLittleEndian<std::uint32_t>(bytes, entry.value_offset + i * 4u));
            }
        }
        return values;
    };
    Entry entries[340];
    const size_t directory = ReadLittleEndian<std::uint32_t>(bytes, 4);
    const std::uint16_t entry_count = ReadLittleEndian<std::uint16_t>(bytes, directory);
    for (std::uint16_t i = 0; i < entry_count; ++i) {
        const size_t at = directory + 2u + static_cast<size_t>(i) * 12u;
        const std::uint16_t tag = ReadLittleEndian<std::uint16_t>(bytes, at);
        if (tag >= 340) {
            continue;
        }
        Entry entry;
        entry.type = ReadLittleEndian<std::uint16_t>(bytes, at + 2);
        entry.count = ReadLittleEndian<std::uint32_t>(bytes, at + 4);
        if (entry.type != 3 && entry.type != 4) {
            continue;  // not a whole number; nothing this reader needs
        }
        // Up to four bytes of values sit in the entry itself, more are stored elsewhere.
        const size_t value_size = entry.type == 3 ? 2u : 4u;
        entry.value_offset = entry.count * value_size <= 4u ? at + 8 : ReadLittleEndian<std::uint32_t>(bytes, at + 8);
        entries[tag] = entry;
    }
    const auto single = [&entries, &read_values](int tag, std::uint32_t fallback) {
        const std::vector<std::uint32_t> values = read_values(entries[tag]);
        return values.empty() ? fallback : values[0];
    };

    const std::uint32_t width = single(256, 0);
    const std::uint32_t depth = single(257, 0);
    if (width < 2 || depth < 2 || width > 100000 || depth > 100000) {
        return fail("the image size is missing or unreasonable");
    }
    if (single(258, 0) != 32 || single(339, 0) != 3 || single(277, 1) != 1) {
        return fail("expected one 32-bit floating-point value per pixel");
    }
    if (single(259, 1) != 1) {
        return fail("the file is compressed; only uncompressed files are supported");
    }
    const std::vector<std::uint32_t> strip_offsets = read_values(entries[273]);
    const std::vector<std::uint32_t> strip_sizes = read_values(entries[279]);
    if (strip_offsets.empty() || strip_offsets.size() != strip_sizes.size()) {
        return fail("the file is not stored in strips");
    }

    map.width = static_cast<int>(width);
    map.depth = static_cast<int>(depth);
    map.spacing = spacing;
    map.heights.resize(static_cast<size_t>(width) * static_cast<size_t>(depth));
    size_t filled = 0;
    for (size_t strip = 0; strip < strip_offsets.size(); ++strip) {
        const size_t value_count = strip_sizes[strip] / 4u;
        if (static_cast<size_t>(strip_offsets[strip]) + strip_sizes[strip] > bytes.size() || filled + value_count > map.heights.size()) {
            return fail("the file is cut short or its strips do not add up to the image");
        }
        std::memcpy(&map.heights[filled], &bytes[strip_offsets[strip]], value_count * 4u);
        filled += value_count;
    }
    if (filled != map.heights.size()) {
        return fail("the strips hold fewer pixels than the image has");
    }
    for (const float height : map.heights) {
        if (height == 0.0f || height < -1000.0f || std::isnan(height)) {
            ++missing_count;
        }
    }
    error.clear();
    return true;
}

int FillMissingHeights(HeightMap& map) {
    const auto is_missing = [](float height) { return height == 0.0f || height < -1000.0f || std::isnan(height); };
    std::vector<size_t> holes;
    for (size_t i = 0; i < map.heights.size(); ++i) {
        if (is_missing(map.heights[i])) {
            holes.push_back(i);
        }
    }
    const int filled_total = static_cast<int>(holes.size());
    // Each round fills the holes that touch known heights, so large holes close from their rim.
    while (!holes.empty()) {
        std::vector<size_t> remaining;
        std::vector<float> new_heights;
        std::vector<size_t> filled;
        for (const size_t index : holes) {
            const int ix = static_cast<int>(index % static_cast<size_t>(map.width));
            const int iz = static_cast<int>(index / static_cast<size_t>(map.width));
            float sum = 0.0f;
            int count = 0;
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = ix + dx;
                    const int nz = iz + dz;
                    if (nx < 0 || nz < 0 || nx >= map.width || nz >= map.depth) {
                        continue;
                    }
                    const float neighbor = map.heights[static_cast<size_t>(nz) * static_cast<size_t>(map.width) + static_cast<size_t>(nx)];
                    if (!is_missing(neighbor)) {
                        sum += neighbor;
                        ++count;
                    }
                }
            }
            if (count > 0) {
                filled.push_back(index);
                new_heights.push_back(sum / static_cast<float>(count));
            } else {
                remaining.push_back(index);
            }
        }
        if (filled.empty()) {
            break;  // the whole map is without data
        }
        for (size_t i = 0; i < filled.size(); ++i) {
            map.heights[filled[i]] = new_heights[i];
        }
        holes = std::move(remaining);
    }
    return filled_total;
}

bool SaveHeightMap(const char* path, const HeightMap& map) {
    std::FILE* file = nullptr;
    if (map.IsEmpty() || fopen_s(&file, path, "wb") != 0 || file == nullptr) {
        return false;
    }
    std::fprintf(file, "poligon-heightmap 1 %d %d %.9g\n", map.width, map.depth, static_cast<double>(map.spacing));
    const size_t written = std::fwrite(map.heights.data(), sizeof(float), map.heights.size(), file);
    return std::fclose(file) == 0 && written == map.heights.size();
}

bool LoadHeightMap(const char* path, HeightMap& map, std::string& error) {
    map = HeightMap{};
    std::FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || file == nullptr) {
        error = std::string("cannot open ") + path;
        return false;
    }
    char line[128];
    int version = 0;
    int width = 0;
    int depth = 0;
    float spacing = 0.0f;
    const bool header_ok = std::fgets(line, sizeof(line), file) != nullptr &&
                           sscanf_s(line, "poligon-heightmap %d %d %d %f", &version, &width, &depth, &spacing) == 4 &&
                           version == 1 && width >= 2 && depth >= 2 && width <= 100000 && depth <= 100000 && spacing > 0.0f;
    if (!header_ok) {
        std::fclose(file);
        error = std::string(path) + ": not a Poligon height map file";
        return false;
    }
    map.width = width;
    map.depth = depth;
    map.spacing = spacing;
    map.heights.resize(static_cast<size_t>(width) * static_cast<size_t>(depth));
    const size_t read = std::fread(map.heights.data(), sizeof(float), map.heights.size(), file);
    std::fclose(file);
    if (read != map.heights.size()) {
        map = HeightMap{};
        error = std::string(path) + ": the file is cut short";
        return false;
    }
    error.clear();
    return true;
}

void PasteHeightMap(HeightMap& map, const HeightMap& tile, int ix, int iz) {
    for (int row = 0; row < tile.depth; ++row) {
        const int target_row = iz + row;
        if (target_row < 0 || target_row >= map.depth) {
            continue;
        }
        const int first = glm::max(0, -ix);
        const int last = glm::min(tile.width, map.width - ix);
        if (last <= first) {
            continue;
        }
        std::memcpy(&map.heights[static_cast<size_t>(target_row) * static_cast<size_t>(map.width) + static_cast<size_t>(ix + first)],
                    &tile.heights[static_cast<size_t>(row) * static_cast<size_t>(tile.width) + static_cast<size_t>(first)],
                    static_cast<size_t>(last - first) * sizeof(float));
    }
}

HeightMap CropHeightMap(const HeightMap& map, int ix, int iz, int width, int depth) {
    if (ix < 0 || iz < 0 || width < 2 || depth < 2 || ix + width > map.width || iz + depth > map.depth) {
        FatalError("CropHeightMap: the part must be at least 2 x 2 points and lie inside the map");
    }
    HeightMap part;
    part.width = width;
    part.depth = depth;
    part.spacing = map.spacing;
    part.heights.resize(static_cast<size_t>(width) * static_cast<size_t>(depth));
    for (int row = 0; row < depth; ++row) {
        std::memcpy(&part.heights[static_cast<size_t>(row) * static_cast<size_t>(width)],
                    &map.heights[static_cast<size_t>(iz + row) * static_cast<size_t>(map.width) + static_cast<size_t>(ix)],
                    static_cast<size_t>(width) * sizeof(float));
    }
    return part;
}

HeightMap CutHeightMap(const HeightMap& map, int ix, int iz, int width, int depth) {
    HeightMap part = CropHeightMap(map, ix, iz, width, depth);
    part.first_x = map.first_x + ix;
    part.first_z = map.first_z + iz;
    return part;
}

void RescaleHeightMap(HeightMap& map, float horizontal_scale, float vertical_scale) {
    if (horizontal_scale <= 0.0f || vertical_scale <= 0.0f) {
        FatalError("RescaleHeightMap: the scales must be above zero");
    }
    float lowest = 0.0f;
    float highest = 0.0f;
    map.GetHeightRange(lowest, highest);
    for (float& height : map.heights) {
        height = (height - lowest) * vertical_scale;
    }
    map.spacing *= horizontal_scale;
}

} // namespace poligon
