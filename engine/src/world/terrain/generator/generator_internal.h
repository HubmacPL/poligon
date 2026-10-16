#pragma once

// Private to the terrain generator (engine/src/world/terrain/generator): what its stages share.
// The public API is poligon/world/terrain_generator.h. The stages, in the order
// GenerateTerrain runs them:
//   large_scale.cpp  stage 1: uplift and stream-power erosion on a coarse grid
//   fine_scale.cpp   stage 2: the land at full resolution, thermal and droplet erosion
//   maps.cpp         stage 3: lakes, flow, water distance, moisture, sediment and rock maps

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <queue>
#include <sstream>

#include "poligon/core/fatal_error.h"
#include "poligon/world/terrain_generator.h"

namespace poligon {
namespace terrain_generator_internal {


// Bump when the generator's output changes for the same settings: cache files are named
// by a hash that includes it.
constexpr std::uint32_t kGeneratorVersion = 3;

// --- Random numbers and noise of our own, so that results never depend on a library ---

inline std::uint64_t SplitMix64(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

class Random {
public:
    explicit Random(std::uint64_t seed) : state_(seed) {}
    std::uint64_t Next() { return SplitMix64(state_); }
    // Uniform in [0, 1).
    double Uniform() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }

private:
    std::uint64_t state_;
};

inline std::uint32_t HashPoint(std::int32_t x, std::int32_t z, std::uint32_t seed) {
    std::uint32_t h = seed ^ 0x27D4EB2Du;
    h ^= static_cast<std::uint32_t>(x) * 0x85EBCA6Bu;
    h = (h << 13) | (h >> 19);
    h ^= static_cast<std::uint32_t>(z) * 0xC2B2AE35u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

// Gradient noise (Perlin's kind) with eight fixed gradient directions; about -1 to 1.
inline double GradientNoise(double x, double z, std::uint32_t seed) {
    static const double kGradients[8][2] = {{1.0, 0.0},   {-1.0, 0.0},  {0.0, 1.0},     {0.0, -1.0},
                                            {0.7071, 0.7071}, {-0.7071, 0.7071}, {0.7071, -0.7071}, {-0.7071, -0.7071}};
    const double fx = std::floor(x);
    const double fz = std::floor(z);
    const std::int32_t ix = static_cast<std::int32_t>(fx);
    const std::int32_t iz = static_cast<std::int32_t>(fz);
    const double dx = x - fx;
    const double dz = z - fz;
    const auto corner = [&](std::int32_t cx, std::int32_t cz, double ox, double oz) {
        const double* g = kGradients[HashPoint(ix + cx, iz + cz, seed) & 7u];
        return g[0] * (dx - ox) + g[1] * (dz - oz);
    };
    const double ux = dx * dx * dx * (dx * (dx * 6.0 - 15.0) + 10.0);
    const double uz = dz * dz * dz * (dz * (dz * 6.0 - 15.0) + 10.0);
    const double a = corner(0, 0, 0.0, 0.0) + (corner(1, 0, 1.0, 0.0) - corner(0, 0, 0.0, 0.0)) * ux;
    const double b = corner(0, 1, 0.0, 1.0) + (corner(1, 1, 1.0, 1.0) - corner(0, 1, 0.0, 1.0)) * ux;
    return (a + (b - a) * uz) * 1.4;
}

// Several layers of noise, each half as large and half as strong; about -1 to 1.
inline double Fbm(double x, double z, std::uint32_t seed, int octaves) {
    double sum = 0.0;
    double amplitude = 1.0;
    double total = 0.0;
    for (int octave = 0; octave < octaves; ++octave) {
        sum += GradientNoise(x, z, seed + static_cast<std::uint32_t>(octave) * 977u) * amplitude;
        total += amplitude;
        amplitude *= 0.5;
        x *= 2.0;
        z *= 2.0;
    }
    return sum / total;
}

inline double SmoothStep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// --- Grids and flow routing ---

constexpr int kNeighborX[8] = {1, -1, 0, 0, 1, -1, 1, -1};
constexpr int kNeighborZ[8] = {0, 0, 1, -1, 1, 1, -1, -1};
constexpr double kNeighborDistance[8] = {1.0, 1.0, 1.0, 1.0, 1.4142135623730951, 1.4142135623730951,
                                         1.4142135623730951, 1.4142135623730951};

inline bool IsOutletPoint(int x, int z, int width, int depth, int outlet_edges) {
    return ((outlet_edges & 1) && z == 0) || ((outlet_edges & 2) && x == width - 1) ||
           ((outlet_edges & 4) && z == depth - 1) || ((outlet_edges & 8) && x == 0);
}

// Priority-Flood (Barnes, Lehman and Mulla 2014, with their queue for flat pits): starting
// from the outlet edges, takes the lowest point not yet reached and raises each new
// neighbor to at least its height. Afterwards `filled` holds the land with every hollow
// filled to its rim. With `epsilon` each filled point is raised by the smallest step
// above the one it was reached from, so that water on a lake still has a way to flow.
template <typename T>
void PriorityFlood(const std::vector<T>& heights, int width, int depth, int outlet_edges, bool epsilon,
                   std::vector<T>& filled) {
    const size_t count = heights.size();
    filled = heights;
    std::vector<std::uint8_t> closed(count, 0);
    struct Item {
        T height;
        std::uint32_t index;
    };
    const auto later = [](const Item& a, const Item& b) {
        return a.height > b.height || (a.height == b.height && a.index > b.index);
    };
    std::priority_queue<Item, std::vector<Item>, decltype(later)> open(later);
    std::vector<std::uint32_t> pits;
    size_t pit_head = 0;
    for (int z = 0; z < depth; ++z) {
        for (int x = 0; x < width; ++x) {
            if (IsOutletPoint(x, z, width, depth, outlet_edges)) {
                const std::uint32_t i = static_cast<std::uint32_t>(z * width + x);
                closed[i] = 1;
                open.push({heights[i], i});
            }
        }
    }
    while (true) {
        std::uint32_t current = 0;
        if (pit_head < pits.size()) {
            current = pits[pit_head++];
            if (pit_head == pits.size()) {
                pits.clear();
                pit_head = 0;
            }
        } else if (!open.empty()) {
            current = open.top().index;
            open.pop();
        } else {
            break;
        }
        const int cx = static_cast<int>(current % static_cast<std::uint32_t>(width));
        const int cz = static_cast<int>(current / static_cast<std::uint32_t>(width));
        const T level = epsilon ? std::nextafter(filled[current], std::numeric_limits<T>::infinity()) : filled[current];
        for (int n = 0; n < 8; ++n) {
            const int nx = cx + kNeighborX[n];
            const int nz = cz + kNeighborZ[n];
            if (nx < 0 || nz < 0 || nx >= width || nz >= depth) {
                continue;
            }
            const std::uint32_t neighbor = static_cast<std::uint32_t>(nz * width + nx);
            if (closed[neighbor] != 0) {
                continue;
            }
            closed[neighbor] = 1;
            if (heights[neighbor] <= level) {
                filled[neighbor] = level;
                pits.push_back(neighbor);
            } else {
                open.push({heights[neighbor], neighbor});
            }
        }
    }
}

// Where water flows from each point: the neighbor with the steepest drop on `surface`
// (which must have no hollows), or the point itself on an outlet edge. Also gives an
// order in which every point comes after the point it flows to (Braun and Willett 2013).
struct FlowNetwork {
    std::vector<std::uint32_t> receiver;
    std::vector<float> distance;  // grid steps to the receiver
    std::vector<std::uint32_t> order;
};

template <typename T>
inline void BuildFlowNetwork(const std::vector<T>& surface, int width, int depth, int outlet_edges, FlowNetwork& network) {
    const size_t count = surface.size();
    network.receiver.resize(count);
    network.distance.assign(count, 1.0f);
    for (int z = 0; z < depth; ++z) {
        for (int x = 0; x < width; ++x) {
            const std::uint32_t i = static_cast<std::uint32_t>(z * width + x);
            network.receiver[i] = i;
            if (IsOutletPoint(x, z, width, depth, outlet_edges)) {
                continue;
            }
            double steepest = 0.0;
            for (int n = 0; n < 8; ++n) {
                const int nx = x + kNeighborX[n];
                const int nz = z + kNeighborZ[n];
                if (nx < 0 || nz < 0 || nx >= width || nz >= depth) {
                    continue;
                }
                const std::uint32_t neighbor = static_cast<std::uint32_t>(nz * width + nx);
                const double drop = (static_cast<double>(surface[i]) - static_cast<double>(surface[neighbor])) / kNeighborDistance[n];
                if (drop > steepest) {
                    steepest = drop;
                    network.receiver[i] = neighbor;
                    network.distance[i] = static_cast<float>(kNeighborDistance[n]);
                }
            }
        }
    }
    // Donor lists, then a walk from every outlet up its tree.
    std::vector<std::uint32_t> donor_start(count + 1, 0);
    for (size_t i = 0; i < count; ++i) {
        if (network.receiver[i] != i) {
            ++donor_start[network.receiver[i] + 1];
        }
    }
    for (size_t i = 0; i < count; ++i) {
        donor_start[i + 1] += donor_start[i];
    }
    std::vector<std::uint32_t> donors(donor_start[count]);
    std::vector<std::uint32_t> fill(donor_start.begin(), donor_start.end() - 1);
    for (size_t i = 0; i < count; ++i) {
        if (network.receiver[i] != i) {
            donors[fill[network.receiver[i]]++] = static_cast<std::uint32_t>(i);
        }
    }
    network.order.clear();
    network.order.reserve(count);
    std::vector<std::uint32_t> to_visit;
    for (size_t root = 0; root < count; ++root) {
        if (network.receiver[root] != root) {
            continue;
        }
        to_visit.push_back(static_cast<std::uint32_t>(root));
        while (!to_visit.empty()) {
            const std::uint32_t k = to_visit.back();
            to_visit.pop_back();
            network.order.push_back(k);
            for (std::uint32_t d = donor_start[k]; d < donor_start[k + 1]; ++d) {
                to_visit.push_back(donors[d]);
            }
        }
    }
}

// --- What the stages hand on ---

struct CoarseResult {
    int size = 0;  // points per side
    std::vector<double> heights;
    std::vector<double> sediment;  // meters laid down in lakes
};

struct FineState {
    int width = 0;
    std::vector<float> heights;
    // Collected on the map grid while the fine processes run.
    std::vector<float> deposited;
    std::vector<float> eroded;
};

// --- The stages ---

void SimulateLargeScale(const TerrainGeneratorSettings& s, CoarseResult& result);

double CatmullRom(double p0, double p1, double p2, double p3, double t);

// The coarse land at a fine point, interpolated smoothly (Catmull-Rom in both directions).
double SampleCoarse(const CoarseResult& coarse, double gx, double gz);

void BuildFineLand(const TerrainGeneratorSettings& s, const CoarseResult& coarse, FineState& fine);

int MapIndexOf(const TerrainGeneratorSettings& s, int map_width, double x, double z);

// Loose material slides to lower neighbors wherever the slope is steeper than it can stand.
void ThermalErosion(const TerrainGeneratorSettings& s, int map_width, int iterations, FineState& fine);

// Raindrops (Beyer 2015): each runs downhill with some inertia for a few dozen steps,
// takes up material while it speeds up and has room for more, and drops it where it slows
// down or climbs. Many of them cut gullies and fill hollows.
void DropletErosion(const TerrainGeneratorSettings& s, int map_width, FineState& fine);

void BuildMaps(const TerrainGeneratorSettings& s, const CoarseResult& coarse, FineState& fine, TerrainMaps& maps);

// --- Used by the public functions too ---

void ValidateSettings(const TerrainGeneratorSettings& s);

template <typename T>
void HashBytes(std::uint64_t& hash, const T* data, size_t count) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
    for (size_t i = 0; i < count * sizeof(T); ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }
}


} // namespace terrain_generator_internal
} // namespace poligon
