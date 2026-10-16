#include "cloud_noise.h"

#include <algorithm>
#include <cmath>

namespace poligon {

namespace {

std::uint32_t Hash3(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed) {
    std::uint32_t h = seed * 0x9E3779B1u;
    h ^= static_cast<std::uint32_t>(x) * 0x85EBCA6Bu;
    h = (h << 13) | (h >> 19);
    h ^= static_cast<std::uint32_t>(y) * 0xC2B2AE35u;
    h = (h << 11) | (h >> 21);
    h ^= static_cast<std::uint32_t>(z) * 0x27D4EB2Fu;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

float ToUnit(std::uint32_t h) {
    return static_cast<float>(h >> 8) / 16777216.0f;
}

int Wrap(int value, int period) {
    const int r = value % period;
    return r < 0 ? r + period : r;
}

// Tileable gradient noise with `period` cells along each axis, about -1..1.
float PerlinNoise(float x, float y, float z, int period, std::uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float fz = z - static_cast<float>(iz);
    const auto gradient = [&](int cx, int cy, int cz, float dx, float dy, float dz) {
        const std::uint32_t h = Hash3(Wrap(ix + cx, period), Wrap(iy + cy, period), Wrap(iz + cz, period), seed) & 15u;
        // The twelve edge directions of a cube (and four repeated), as in Perlin's improved noise.
        const float u = h < 8 ? dx : dy;
        const float v = h < 4 ? dy : (h == 12 || h == 14 ? dx : dz);
        return ((h & 1u) ? -u : u) + ((h & 2u) ? -v : v);
    };
    const auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
    const float ux = fade(fx);
    const float uy = fade(fy);
    const float uz = fade(fz);
    float corners[8];
    for (int i = 0; i < 8; ++i) {
        const int cx = i & 1;
        const int cy = (i >> 1) & 1;
        const int cz = (i >> 2) & 1;
        corners[i] = gradient(cx, cy, cz, fx - static_cast<float>(cx), fy - static_cast<float>(cy), fz - static_cast<float>(cz));
    }
    const float x00 = corners[0] + (corners[1] - corners[0]) * ux;
    const float x10 = corners[2] + (corners[3] - corners[2]) * ux;
    const float x01 = corners[4] + (corners[5] - corners[4]) * ux;
    const float x11 = corners[6] + (corners[7] - corners[6]) * ux;
    const float y0 = x00 + (x10 - x00) * uy;
    const float y1 = x01 + (x11 - x01) * uy;
    return y0 + (y1 - y0) * uz;
}

// Tileable Worley (cell) noise: 1 at a feature point, falling to 0 at the cell size; one
// random point per cell of a `period`^3 grid.
float WorleyNoise(float x, float y, float z, int period, std::uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const int iz = static_cast<int>(std::floor(z));
    float nearest = 1.0e9f;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int cx = ix + dx;
                const int cy = iy + dy;
                const int cz = iz + dz;
                const std::uint32_t h = Hash3(Wrap(cx, period), Wrap(cy, period), Wrap(cz, period), seed);
                const float px = static_cast<float>(cx) + ToUnit(h);
                const float py = static_cast<float>(cy) + ToUnit(h * 0x2C1B3C6Du + 1u);
                const float pz = static_cast<float>(cz) + ToUnit(h * 0x297A2D39u + 2u);
                const float d = (px - x) * (px - x) + (py - y) * (py - y) + (pz - z) * (pz - z);
                nearest = std::min(nearest, d);
            }
        }
    }
    return std::clamp(1.0f - std::sqrt(nearest), 0.0f, 1.0f);
}

// Several octaves of Worley noise, each with twice the cells, weighted 0.625, 0.25, 0.125.
float WorleyFbm(float u, float v, float w, int cells, std::uint32_t seed) {
    return WorleyNoise(u * static_cast<float>(cells), v * static_cast<float>(cells), w * static_cast<float>(cells), cells, seed) * 0.625f +
           WorleyNoise(u * static_cast<float>(cells * 2), v * static_cast<float>(cells * 2), w * static_cast<float>(cells * 2), cells * 2,
                       seed + 1u) * 0.25f +
           WorleyNoise(u * static_cast<float>(cells * 4), v * static_cast<float>(cells * 4), w * static_cast<float>(cells * 4), cells * 4,
                       seed + 2u) * 0.125f;
}

float Remap(float value, float old_low, float old_high, float new_low, float new_high) {
    return new_low + (value - old_low) / (old_high - old_low) * (new_high - new_low);
}

std::uint8_t ToByte(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

} // namespace

std::vector<std::uint8_t> MakeCloudShapeNoise(int size, std::uint32_t seed) {
    std::vector<std::uint8_t> bytes(static_cast<size_t>(size) * static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                const float w = (static_cast<float>(z) + 0.5f) / static_cast<float>(size);
                // Perlin fbm, made billowy by Worley noise ("Perlin-Worley").
                float perlin = 0.0f;
                float amplitude = 1.0f;
                float total = 0.0f;
                for (int octave = 0; octave < 4; ++octave) {
                    const int cells = 4 << octave;
                    perlin += PerlinNoise(u * static_cast<float>(cells), v * static_cast<float>(cells), w * static_cast<float>(cells),
                                          cells, seed + static_cast<std::uint32_t>(octave)) *
                              amplitude;
                    total += amplitude;
                    amplitude *= 0.5f;
                }
                perlin = std::clamp(perlin / total * 0.5f + 0.5f, 0.0f, 1.0f);
                const float worley = WorleyFbm(u, v, w, 4, seed + 10u);
                const float perlin_worley = std::clamp(Remap(perlin, worley - 1.0f, 1.0f, 0.0f, 1.0f), 0.0f, 1.0f);
                std::uint8_t* voxel = &bytes[((static_cast<size_t>(z) * static_cast<size_t>(size) + static_cast<size_t>(y)) *
                                                  static_cast<size_t>(size) +
                                              static_cast<size_t>(x)) * 4];
                voxel[0] = ToByte(perlin_worley);
                voxel[1] = ToByte(WorleyFbm(u, v, w, 4, seed + 20u));
                voxel[2] = ToByte(WorleyFbm(u, v, w, 8, seed + 30u));
                voxel[3] = ToByte(WorleyFbm(u, v, w, 16, seed + 40u));
            }
        }
    }
    return bytes;
}

std::vector<std::uint8_t> MakeCloudDetailNoise(int size, std::uint32_t seed) {
    std::vector<std::uint8_t> bytes(static_cast<size_t>(size) * static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                const float w = (static_cast<float>(z) + 0.5f) / static_cast<float>(size);
                std::uint8_t* voxel = &bytes[((static_cast<size_t>(z) * static_cast<size_t>(size) + static_cast<size_t>(y)) *
                                                  static_cast<size_t>(size) +
                                              static_cast<size_t>(x)) * 4];
                voxel[0] = ToByte(WorleyFbm(u, v, w, 2, seed + 50u));
                voxel[1] = ToByte(WorleyFbm(u, v, w, 4, seed + 60u));
                voxel[2] = ToByte(WorleyFbm(u, v, w, 8, seed + 70u));
                voxel[3] = 255;
            }
        }
    }
    return bytes;
}

std::vector<std::uint8_t> MakeCloudWeatherMap(int size, std::uint32_t seed) {
    std::vector<std::uint8_t> bytes(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            float coverage = 0.0f;
            float type = 0.0f;
            float amplitude = 1.0f;
            float total = 0.0f;
            for (int octave = 0; octave < 4; ++octave) {
                const int cells = 3 << octave;
                coverage += PerlinNoise(u * static_cast<float>(cells), v * static_cast<float>(cells), 0.5f, cells,
                                        seed + 80u + static_cast<std::uint32_t>(octave)) *
                            amplitude;
                type += PerlinNoise(u * static_cast<float>(cells), v * static_cast<float>(cells), 1.5f, cells,
                                    seed + 90u + static_cast<std::uint32_t>(octave)) *
                        amplitude;
                total += amplitude;
                amplitude *= 0.5f;
            }
            std::uint8_t* pixel = &bytes[(static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 4];
            pixel[0] = ToByte(coverage / total * 0.9f + 0.5f);
            pixel[1] = ToByte(type / total * 1.2f + 0.5f);
            pixel[2] = 0;
            pixel[3] = 255;
        }
    }
    return bytes;
}

} // namespace poligon
