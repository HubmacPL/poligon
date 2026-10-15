#include "poligon/terrain_generator.h"

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

#include "poligon/fatal_error.h"

namespace poligon {
namespace {

// Bump when the generator's output changes for the same settings: cache files are named
// by a hash that includes it.
constexpr std::uint32_t kGeneratorVersion = 2;

// --- Random numbers and noise of our own, so that results never depend on a library ---

std::uint64_t SplitMix64(std::uint64_t& state) {
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

std::uint32_t HashPoint(std::int32_t x, std::int32_t z, std::uint32_t seed) {
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
double GradientNoise(double x, double z, std::uint32_t seed) {
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
double Fbm(double x, double z, std::uint32_t seed, int octaves) {
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

double SmoothStep(double edge0, double edge1, double x) {
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// --- Grids and flow routing ---

constexpr int kNeighborX[8] = {1, -1, 0, 0, 1, -1, 1, -1};
constexpr int kNeighborZ[8] = {0, 0, 1, -1, 1, 1, -1, -1};
constexpr double kNeighborDistance[8] = {1.0, 1.0, 1.0, 1.0, 1.4142135623730951, 1.4142135623730951,
                                         1.4142135623730951, 1.4142135623730951};

bool IsOutletPoint(int x, int z, int width, int depth, int outlet_edges) {
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
void BuildFlowNetwork(const std::vector<T>& surface, int width, int depth, int outlet_edges, FlowNetwork& network) {
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

// --- Stage 1: large scale ---

struct CoarseResult {
    int size = 0;  // points per side
    std::vector<double> heights;
    std::vector<double> sediment;  // meters laid down in lakes
};

void SimulateLargeScale(const TerrainGeneratorSettings& s, CoarseResult& result) {
    const int n = static_cast<int>(std::lround(s.size / s.coarse_spacing)) + 1;
    const size_t count = static_cast<size_t>(n) * static_cast<size_t>(n);
    const double dx = s.coarse_spacing;
    const double cell_area = dx * dx;
    const std::uint32_t seed = static_cast<std::uint32_t>(s.seed ^ (s.seed >> 32));
    Random random(s.seed * 0x9E3779B97F4A7C15ull + 1u);

    // Uplift: most land rises at 55 to 100% of the highest rate, in broad patches; late in
    // the simulation the basins slow down by up to `basin_sinking` times the highest rate.
    struct Basin {
        double x, z, radius;
    };
    std::vector<Basin> basins;
    for (int b = 0; b < s.basin_count; ++b) {
        Basin basin;
        basin.x = s.size * (0.18 + 0.64 * random.Uniform());
        basin.z = s.size * (0.18 + 0.64 * random.Uniform());
        basin.radius = s.basin_radius * (0.7 + 0.6 * random.Uniform());
        basins.push_back(basin);
    }
    std::vector<double> uplift(count);
    std::vector<double> basin_uplift(count);
    std::vector<double> heights(count);
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            const double wx = x * dx;
            const double wz = z * dx;
            const double rate = std::clamp(0.775 + 0.36 * Fbm(wx / 2200.0, wz / 2200.0, seed + 11u, 3), 0.55, 1.0);
            double sinking = 0.0;
            for (size_t b = 0; b < basins.size(); ++b) {
                const Basin& basin = basins[b];
                // An irregular outline: the radius changes with noise around the middle.
                const double radius =
                    basin.radius * (1.0 + 0.45 * Fbm(wx / 350.0, wz / 350.0, seed + 31u + static_cast<std::uint32_t>(b) * 7u, 3));
                const double r2 = ((wx - basin.x) * (wx - basin.x) + (wz - basin.z) * (wz - basin.z)) / (radius * radius);
                if (r2 < 1.0) {
                    sinking = std::max(sinking, (1.0 - r2) * (1.0 - r2));
                }
            }
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(n) + static_cast<size_t>(x);
            uplift[i] = rate * s.uplift;
            basin_uplift[i] = (rate - sinking * s.basin_sinking) * s.uplift;
            // The older land: rolling hills, which rivers then cut into.
            heights[i] = s.initial_relief * (0.5 + 0.5 * Fbm(wx / s.initial_hill_size, wz / s.initial_hill_size, seed + 23u, 5)) +
                         0.05 * random.Uniform();
        }
    }
    // Outlet edges are where the land continues beyond the map: they keep the old hills'
    // heights and rise with the slowest rate, so that rivers leave through their low points
    // without a cliff forming along the edge.
    std::vector<std::uint32_t> outlets;
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            if (IsOutletPoint(x, z, n, n, s.outlet_edges)) {
                outlets.push_back(static_cast<std::uint32_t>(z * n + x));
            }
        }
    }
    const double edge_uplift = 0.55 * s.uplift;

    std::vector<double> sediment(count, 0.0);
    std::vector<double> filled;
    std::vector<double> area(count);
    std::vector<double> before(count);
    std::vector<double> flux(count);
    std::vector<double> next(count);
    FlowNetwork network;
    const double dt = s.years_per_step;
    const double diffusion_limit = 0.2 * cell_area / std::max(static_cast<double>(s.hillslope_diffusion), 1e-12);
    const int diffusion_steps = std::max(1, static_cast<int>(std::ceil(dt / diffusion_limit)));
    const double diffusion_dt = dt / diffusion_steps;

    const int basin_step = static_cast<int>(std::lround(s.basin_start * s.erosion_steps));
    for (int step = 0; step < s.erosion_steps; ++step) {
        if (step == basin_step) {
            uplift.swap(basin_uplift);
        }
        for (const std::uint32_t i : outlets) {
            heights[i] += edge_uplift * dt;
        }
        PriorityFlood(heights, n, n, s.outlet_edges, true, filled);
        BuildFlowNetwork(filled, n, n, s.outlet_edges, network);

        // Drained area: each point's own area plus everything upstream.
        std::fill(area.begin(), area.end(), cell_area);
        for (size_t k = count; k-- > 0;) {
            const std::uint32_t i = network.order[k];
            if (network.receiver[i] != i) {
                area[network.receiver[i]] += area[i];
            }
        }

        // Uplift and river erosion, solved implicitly from the outlets upstream: with the
        // slope exponent 1 the new height follows from the receiver's new height alone.
        before = heights;
        for (const std::uint32_t i : network.order) {
            const std::uint32_t r = network.receiver[i];
            if (r == i) {
                continue;  // outlets stay at base level
            }
            const double raised = heights[i] + uplift[i] * dt;
            const double receiver_height = heights[r];
            if (raised <= receiver_height) {
                heights[i] = raised;  // under a lake: no river cuts here
                continue;
            }
            const double factor = s.erodibility * dt * std::pow(area[i], static_cast<double>(s.area_exponent)) /
                                  (network.distance[i] * dx);
            heights[i] = (raised + factor * receiver_height) / (1.0 + factor);
        }

        // Eroded material travels downstream and settles in the first lake it reaches,
        // up to the lake's rim.
        std::fill(flux.begin(), flux.end(), 0.0);
        for (size_t k = count; k-- > 0;) {
            const std::uint32_t i = network.order[k];
            const std::uint32_t r = network.receiver[i];
            if (r == i) {
                continue;
            }
            const double eroded = std::max(before[i] + uplift[i] * dt - heights[i], 0.0) * cell_area;
            double carried = flux[i] + eroded;
            const double room = (filled[i] - heights[i]) * cell_area;
            if (room > 0.0 && carried > 0.0) {
                const double laid = std::min(room, carried);
                heights[i] += laid / cell_area;
                sediment[i] += laid / cell_area;
                carried -= laid;
            }
            flux[r] += carried;
        }

        // Hillslope creep: diffusion with closed edges, outlets held at base level.
        for (int sub = 0; sub < diffusion_steps; ++sub) {
            const double k = s.hillslope_diffusion * diffusion_dt / cell_area;
            for (int z = 0; z < n; ++z) {
                for (int x = 0; x < n; ++x) {
                    const size_t i = static_cast<size_t>(z) * static_cast<size_t>(n) + static_cast<size_t>(x);
                    if (IsOutletPoint(x, z, n, n, s.outlet_edges)) {
                        next[i] = heights[i];
                        continue;
                    }
                    const double h = heights[i];
                    const double west = x > 0 ? heights[i - 1] : h;
                    const double east = x < n - 1 ? heights[i + 1] : h;
                    const double north = z > 0 ? heights[i - static_cast<size_t>(n)] : h;
                    const double south = z < n - 1 ? heights[i + static_cast<size_t>(n)] : h;
                    next[i] = h + k * (west + east + north + south - 4.0 * h);
                }
            }
            heights.swap(next);
        }
    }

    // Scale to the wanted relief, lowest point at 0.
    double lowest = heights[0];
    double highest = heights[0];
    for (const double h : heights) {
        lowest = std::min(lowest, h);
        highest = std::max(highest, h);
    }
    const double scale = s.relief / std::max(highest - lowest, 1e-9);
    for (size_t i = 0; i < count; ++i) {
        heights[i] = (heights[i] - lowest) * scale;
        sediment[i] *= scale;
    }
    result.size = n;
    result.heights = std::move(heights);
    result.sediment = std::move(sediment);
}

// --- Stage 2: fine scale ---

double CatmullRom(double p0, double p1, double p2, double p3, double t) {
    return p1 + 0.5 * t * (p2 - p0 + t * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + t * (3.0 * (p1 - p2) + p3 - p0)));
}

// The coarse land at a fine point, interpolated smoothly (Catmull-Rom in both directions).
double SampleCoarse(const CoarseResult& coarse, double gx, double gz) {
    const int n = coarse.size;
    const int ix = std::clamp(static_cast<int>(std::floor(gx)), 0, n - 2);
    const int iz = std::clamp(static_cast<int>(std::floor(gz)), 0, n - 2);
    const double tx = gx - ix;
    const double tz = gz - iz;
    double rows[4];
    for (int j = 0; j < 4; ++j) {
        const int z = std::clamp(iz - 1 + j, 0, n - 1);
        const double* row = &coarse.heights[static_cast<size_t>(z) * static_cast<size_t>(n)];
        rows[j] = CatmullRom(row[std::max(ix - 1, 0)], row[ix], row[ix + 1], row[std::min(ix + 2, n - 1)], tx);
    }
    return CatmullRom(rows[0], rows[1], rows[2], rows[3], tz);
}

struct FineState {
    int width = 0;
    std::vector<float> heights;
    // Collected on the map grid while the fine processes run.
    std::vector<float> deposited;
    std::vector<float> eroded;
};

void BuildFineLand(const TerrainGeneratorSettings& s, const CoarseResult& coarse, FineState& fine) {
    const int width = static_cast<int>(std::lround(s.size / s.spacing)) + 1;
    fine.width = width;
    fine.heights.resize(static_cast<size_t>(width) * static_cast<size_t>(width));
    const double to_coarse = s.spacing / s.coarse_spacing;
    const std::uint32_t seed = static_cast<std::uint32_t>(s.seed ^ (s.seed >> 32)) + 101u;
    for (int z = 0; z < width; ++z) {
        for (int x = 0; x < width; ++x) {
            const double gx = x * to_coarse;
            const double gz = z * to_coarse;
            const double h = SampleCoarse(coarse, gx, gz);
            // Bumps, smaller on flat ground (valley floors, lake beds) than on slopes.
            const double slope_x = SampleCoarse(coarse, gx + 0.5, gz) - SampleCoarse(coarse, gx - 0.5, gz);
            const double slope_z = SampleCoarse(coarse, gx, gz + 0.5) - SampleCoarse(coarse, gx, gz - 0.5);
            const double slope = std::sqrt(slope_x * slope_x + slope_z * slope_z) / s.coarse_spacing;
            const double wx = x * s.spacing;
            const double wz = z * s.spacing;
            const double bumps = Fbm(wx / 70.0, wz / 70.0, seed, 5) * (0.35 + 0.65 * SmoothStep(0.02, 0.15, slope));
            fine.heights[static_cast<size_t>(z) * static_cast<size_t>(width) + static_cast<size_t>(x)] =
                static_cast<float>(h + bumps * s.detail_height);
        }
    }
}

int MapIndexOf(const TerrainGeneratorSettings& s, int map_width, double x, double z) {
    const int ix = std::clamp(static_cast<int>(std::lround(x / s.map_spacing)), 0, map_width - 1);
    const int iz = std::clamp(static_cast<int>(std::lround(z / s.map_spacing)), 0, map_width - 1);
    return iz * map_width + ix;
}

// Loose material slides to lower neighbors wherever the slope is steeper than it can stand.
void ThermalErosion(const TerrainGeneratorSettings& s, int map_width, int iterations, FineState& fine) {
    const int width = fine.width;
    const float limit = static_cast<float>(std::tan(s.talus_degrees * 3.14159265358979 / 180.0) * s.spacing);
    std::vector<float> change(fine.heights.size());
    for (int iteration = 0; iteration < iterations; ++iteration) {
        std::fill(change.begin(), change.end(), 0.0f);
        for (int z = 0; z < width; ++z) {
            for (int x = 0; x < width; ++x) {
                const size_t i = static_cast<size_t>(z) * static_cast<size_t>(width) + static_cast<size_t>(x);
                // Each pair of neighbors once: to the east and to the south.
                for (int pair = 0; pair < 2; ++pair) {
                    if ((pair == 0 && x == width - 1) || (pair == 1 && z == width - 1)) {
                        continue;
                    }
                    const size_t j = pair == 0 ? i + 1 : i + static_cast<size_t>(width);
                    const float difference = fine.heights[i] - fine.heights[j];
                    const float excess = std::fabs(difference) - limit;
                    if (excess <= 0.0f) {
                        continue;
                    }
                    // A quarter of the excess per pair: four pairs can never move more than it.
                    const float moved = 0.125f * excess;
                    if (difference > 0.0f) {
                        change[i] -= moved;
                        change[j] += moved;
                    } else {
                        change[i] += moved;
                        change[j] -= moved;
                    }
                }
            }
        }
        for (size_t i = 0; i < change.size(); ++i) {
            fine.heights[i] += change[i];
        }
        if (iteration == iterations - 1 || iteration % 4 == 0) {
            for (int z = 0; z < width; ++z) {
                for (int x = 0; x < width; ++x) {
                    const float c = change[static_cast<size_t>(z) * static_cast<size_t>(width) + static_cast<size_t>(x)];
                    if (c > 0.0f) {
                        fine.deposited[static_cast<size_t>(MapIndexOf(s, map_width, x * s.spacing, z * s.spacing))] +=
                            c * 0.25f;
                    }
                }
            }
        }
    }
}

// Raindrops (Beyer 2015): each runs downhill with some inertia for a few dozen steps,
// takes up material while it speeds up and has room for more, and drops it where it slows
// down or climbs. Many of them cut gullies and fill hollows.
void DropletErosion(const TerrainGeneratorSettings& s, int map_width, FineState& fine) {
    const int width = fine.width;
    const double land = (width - 1) * static_cast<double>(s.spacing);
    const long long droplet_count = static_cast<long long>(s.droplets_per_square_meter * land * land);
    const float inertia = 0.05f;
    const float capacity_factor = 1.0f * s.droplet_strength;
    const float min_capacity = 0.001f;
    const float erode_speed = 0.12f * s.droplet_strength;
    const float deposit_speed = 0.3f;
    const float evaporation = 0.02f;
    const float gravity = 4.0f;
    // Limits that keep the process from feeding on itself: a drop cannot run faster than
    // this many grid steps' worth, nor cut more than this many meters in one step (without
    // them a steep wall gives big drops, big drops cut deep, and a pit hundreds of meters
    // deep can grow).
    const float max_speed = 8.0f;
    const float max_cut = 0.02f * s.droplet_strength;
    // Erosion fades out over this many points from the map's edge, where the edge rows
    // themselves cannot be cut and would otherwise stand as a wall.
    const float edge_fade = 32.0f;
    const int max_steps = s.droplet_steps;
    const int radius = 2;
    // The cut is spread over the points within `radius`, weighted by closeness.
    struct BrushTap {
        int dx, dz;
        float weight;
    };
    std::vector<BrushTap> brush;
    float brush_total = 0.0f;
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const float d = std::sqrt(static_cast<float>(dx * dx + dz * dz));
            if (d <= static_cast<float>(radius)) {
                brush.push_back({dx, dz, 1.0f - d / (static_cast<float>(radius) + 0.5f)});
                brush_total += brush.back().weight;
            }
        }
    }
    for (BrushTap& tap : brush) {
        tap.weight /= brush_total;
    }

    std::vector<float>& h = fine.heights;
    const auto height_and_gradient = [&](float px, float pz, float& height, float& gx, float& gz) {
        const int ix = static_cast<int>(px);
        const int iz = static_cast<int>(pz);
        const float fx = px - static_cast<float>(ix);
        const float fz = pz - static_cast<float>(iz);
        const size_t i = static_cast<size_t>(iz) * static_cast<size_t>(width) + static_cast<size_t>(ix);
        const float a = h[i];
        const float b = h[i + 1];
        const float c = h[i + static_cast<size_t>(width)];
        const float d = h[i + static_cast<size_t>(width) + 1];
        gx = (b - a) * (1.0f - fz) + (d - c) * fz;
        gz = (c - a) * (1.0f - fx) + (d - b) * fx;
        height = a * (1.0f - fx) * (1.0f - fz) + b * fx * (1.0f - fz) + c * (1.0f - fx) * fz + d * fx * fz;
    };

    Random random(s.seed * 0xD1B54A32D192ED03ull + 7u);
    const float edge = static_cast<float>(width - 1 - radius - 1);
    for (long long droplet = 0; droplet < droplet_count; ++droplet) {
        float px = static_cast<float>(radius + 1 + random.Uniform() * (edge - radius - 1));
        float pz = static_cast<float>(radius + 1 + random.Uniform() * (edge - radius - 1));
        float dir_x = 0.0f;
        float dir_z = 0.0f;
        float speed = 1.0f;
        float water = 1.0f;
        float carried = 0.0f;
        for (int step = 0; step < max_steps; ++step) {
            const int ix = static_cast<int>(px);
            const int iz = static_cast<int>(pz);
            const float fx = px - static_cast<float>(ix);
            const float fz = pz - static_cast<float>(iz);
            float height = 0.0f;
            float gx = 0.0f;
            float gz = 0.0f;
            height_and_gradient(px, pz, height, gx, gz);
            dir_x = dir_x * inertia - gx * (1.0f - inertia);
            dir_z = dir_z * inertia - gz * (1.0f - inertia);
            const float length = std::sqrt(dir_x * dir_x + dir_z * dir_z);
            if (length < 1e-6f) {
                break;
            }
            dir_x /= length;
            dir_z /= length;
            px += dir_x;
            pz += dir_z;
            if (px < static_cast<float>(radius + 1) || pz < static_cast<float>(radius + 1) || px >= edge || pz >= edge) {
                break;
            }
            float new_height = 0.0f;
            float unused_x = 0.0f;
            float unused_z = 0.0f;
            height_and_gradient(px, pz, new_height, unused_x, unused_z);
            const float drop = new_height - height;  // negative going down
            const float capacity = std::max(-drop * speed * water * capacity_factor, min_capacity);
            const size_t i = static_cast<size_t>(iz) * static_cast<size_t>(width) + static_cast<size_t>(ix);
            const size_t map_i = static_cast<size_t>(MapIndexOf(s, map_width, ix * s.spacing, iz * s.spacing));
            if (carried > capacity || drop > 0.0f) {
                // Climbing: fill the hollow behind (never above where it came from);
                // otherwise drop part of the excess. Spread over the four corners.
                const float laid = drop > 0.0f ? std::min(drop, carried) : (carried - capacity) * deposit_speed;
                carried -= laid;
                h[i] += laid * (1.0f - fx) * (1.0f - fz);
                h[i + 1] += laid * fx * (1.0f - fz);
                h[i + static_cast<size_t>(width)] += laid * (1.0f - fx) * fz;
                h[i + static_cast<size_t>(width) + 1] += laid * fx * fz;
                fine.deposited[map_i] += laid * 0.25f;
            } else {
                // Never cut deeper than the drop, or the drop would dig pits.
                const float from_edge = static_cast<float>(std::min(std::min(ix, iz), std::min(width - 1 - ix, width - 1 - iz)));
                const float edge_share = std::clamp((from_edge - static_cast<float>(radius + 1)) / edge_fade, 0.0f, 1.0f);
                const float cut = std::min(std::min((capacity - carried) * erode_speed, -drop), max_cut) * edge_share;
                for (const BrushTap& tap : brush) {
                    const size_t j = static_cast<size_t>(iz + tap.dz) * static_cast<size_t>(width) + static_cast<size_t>(ix + tap.dx);
                    const float taken = cut * tap.weight;
                    h[j] -= taken;
                    carried += taken;
                }
                fine.eroded[map_i] += cut * 0.25f;
            }
            speed = std::min(std::sqrt(std::max(speed * speed - drop * gravity, 0.0f)), max_speed);
            water *= 1.0f - evaporation;
        }
    }
}

// --- Stage 3: maps ---

void BuildMaps(const TerrainGeneratorSettings& s, const CoarseResult& coarse, FineState& fine, TerrainMaps& maps) {
    const int mw = maps.map_width;
    const size_t count = static_cast<size_t>(mw) * static_cast<size_t>(mw);
    const int step = static_cast<int>(std::lround(s.map_spacing / s.spacing));
    const float cell_area = s.map_spacing * s.map_spacing;
    std::vector<float> heights(count);
    for (int z = 0; z < mw; ++z) {
        for (int x = 0; x < mw; ++x) {
            heights[static_cast<size_t>(z) * static_cast<size_t>(mw) + static_cast<size_t>(x)] =
                fine.heights[static_cast<size_t>(z * step) * static_cast<size_t>(fine.width) + static_cast<size_t>(x * step)];
        }
    }

    // Lakes: hollows filled to their rims, kept where large and deep enough.
    std::vector<float> filled;
    PriorityFlood(heights, mw, mw, s.outlet_edges, false, filled);
    maps.water_level.assign(count, TerrainMaps::kNoWater);
    std::vector<std::int32_t> label(count, -1);
    std::vector<std::uint32_t> stack;
    std::vector<std::uint32_t> members;
    maps.lakes.clear();
    for (size_t start = 0; start < count; ++start) {
        if (label[start] != -1 || filled[start] - heights[start] < 0.01f) {
            continue;
        }
        members.clear();
        stack.push_back(static_cast<std::uint32_t>(start));
        label[start] = 0;
        double sum_x = 0.0;
        double sum_z = 0.0;
        float deepest = 0.0f;
        while (!stack.empty()) {
            const std::uint32_t i = stack.back();
            stack.pop_back();
            members.push_back(i);
            const int x = static_cast<int>(i % static_cast<std::uint32_t>(mw));
            const int z = static_cast<int>(i / static_cast<std::uint32_t>(mw));
            sum_x += x;
            sum_z += z;
            deepest = std::max(deepest, filled[i] - heights[i]);
            for (int n = 0; n < 8; ++n) {
                const int nx = x + kNeighborX[n];
                const int nz = z + kNeighborZ[n];
                if (nx < 0 || nz < 0 || nx >= mw || nz >= mw) {
                    continue;
                }
                const std::uint32_t j = static_cast<std::uint32_t>(nz * mw + nx);
                if (label[j] == -1 && filled[j] - heights[j] >= 0.01f) {
                    label[j] = 0;
                    stack.push_back(j);
                }
            }
        }
        const float area = static_cast<float>(members.size()) * cell_area;
        if (area < s.min_lake_area || deepest < s.min_lake_depth) {
            continue;
        }
        Lake lake;
        lake.center = glm::vec2(static_cast<float>(sum_x / static_cast<double>(members.size())) * s.map_spacing,
                                static_cast<float>(sum_z / static_cast<double>(members.size())) * s.map_spacing);
        lake.level = filled[members[0]];
        lake.area = area;
        lake.max_depth = deepest;
        maps.lakes.push_back(lake);
        for (const std::uint32_t i : members) {
            maps.water_level[i] = filled[i];
        }
    }
    // The level reaches one point past the shore, so that the shore follows the finer heights.
    std::vector<float> level = maps.water_level;
    for (int z = 0; z < mw; ++z) {
        for (int x = 0; x < mw; ++x) {
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(mw) + static_cast<size_t>(x);
            if (maps.water_level[i] != TerrainMaps::kNoWater) {
                continue;
            }
            for (int n = 0; n < 8; ++n) {
                const int nx = x + kNeighborX[n];
                const int nz = z + kNeighborZ[n];
                if (nx >= 0 && nz >= 0 && nx < mw && nz < mw) {
                    const float neighbor = maps.water_level[static_cast<size_t>(nz) * static_cast<size_t>(mw) + static_cast<size_t>(nx)];
                    if (neighbor != TerrainMaps::kNoWater) {
                        level[i] = std::max(level[i], neighbor);
                    }
                }
            }
        }
    }
    maps.water_level = std::move(level);

    // Flow: drained area, routed over the filled land so that it passes through lakes.
    std::vector<float> routed;
    PriorityFlood(heights, mw, mw, s.outlet_edges, true, routed);
    FlowNetwork network;
    BuildFlowNetwork(routed, mw, mw, s.outlet_edges, network);
    maps.flow.assign(count, cell_area);
    for (size_t k = count; k-- > 0;) {
        const std::uint32_t i = network.order[k];
        if (network.receiver[i] != i) {
            maps.flow[network.receiver[i]] += maps.flow[i];
        }
    }

    // Laid-down material is spread over its neighbors (two passes of a 3 x 3 average), so
    // that the map shows deposits rather than the single paths that laid them.
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<float> smoothed(count);
        for (int z = 0; z < mw; ++z) {
            for (int x = 0; x < mw; ++x) {
                float sum = 0.0f;
                int taps = 0;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx;
                        const int nz = z + dz;
                        if (nx >= 0 && nz >= 0 && nx < mw && nz < mw) {
                            sum += fine.deposited[static_cast<size_t>(nz) * static_cast<size_t>(mw) + static_cast<size_t>(nx)];
                            ++taps;
                        }
                    }
                }
                smoothed[static_cast<size_t>(z) * static_cast<size_t>(mw) + static_cast<size_t>(x)] = sum / static_cast<float>(taps);
            }
        }
        fine.deposited.swap(smoothed);
    }

    // Distance to lake water (chamfer distance, two passes).
    maps.water_distance.assign(count, 1000.0f);
    for (size_t i = 0; i < count; ++i) {
        if (maps.water_level[i] != TerrainMaps::kNoWater && maps.water_level[i] > heights[i]) {
            maps.water_distance[i] = 0.0f;
        }
    }
    const float straight = s.map_spacing;
    const float diagonal = s.map_spacing * 1.41421356f;
    std::vector<float>& d = maps.water_distance;
    for (int z = 0; z < mw; ++z) {
        for (int x = 0; x < mw; ++x) {
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(mw) + static_cast<size_t>(x);
            if (x > 0) d[i] = std::min(d[i], d[i - 1] + straight);
            if (z > 0) {
                d[i] = std::min(d[i], d[i - static_cast<size_t>(mw)] + straight);
                if (x > 0) d[i] = std::min(d[i], d[i - static_cast<size_t>(mw) - 1] + diagonal);
                if (x < mw - 1) d[i] = std::min(d[i], d[i - static_cast<size_t>(mw) + 1] + diagonal);
            }
        }
    }
    for (int z = mw - 1; z >= 0; --z) {
        for (int x = mw - 1; x >= 0; --x) {
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(mw) + static_cast<size_t>(x);
            if (x < mw - 1) d[i] = std::min(d[i], d[i + 1] + straight);
            if (z < mw - 1) {
                d[i] = std::min(d[i], d[i + static_cast<size_t>(mw)] + straight);
                if (x < mw - 1) d[i] = std::min(d[i], d[i + static_cast<size_t>(mw) + 1] + diagonal);
                if (x > 0) d[i] = std::min(d[i], d[i + static_cast<size_t>(mw) - 1] + diagonal);
            }
        }
    }

    // Sediment: lake fill from the large scale plus what slides and raindrops laid down.
    // Rock: steep slopes without loose cover, and places raindrops scoured.
    // Moisture: wetness index ln(a / tan(slope)) (high where much water gathers on flat
    // ground), raised near lakes.
    maps.sediment.assign(count, 0.0f);
    maps.rock.assign(count, 0.0f);
    maps.moisture.assign(count, 0.0f);
    const double to_coarse = s.map_spacing / s.coarse_spacing;
    const int n = coarse.size;
    for (int z = 0; z < mw; ++z) {
        for (int x = 0; x < mw; ++x) {
            const size_t i = static_cast<size_t>(z) * static_cast<size_t>(mw) + static_cast<size_t>(x);
            const int cx = std::clamp(static_cast<int>(std::lround(x * to_coarse)), 0, n - 1);
            const int cz = std::clamp(static_cast<int>(std::lround(z * to_coarse)), 0, n - 1);
            const float lake_fill = static_cast<float>(coarse.sediment[static_cast<size_t>(cz) * static_cast<size_t>(n) + static_cast<size_t>(cx)]);
            maps.sediment[i] = lake_fill + fine.deposited[i];

            const float west = heights[i - (x > 0 ? 1 : 0)];
            const float east = heights[i + (x < mw - 1 ? 1 : 0)];
            const float north = heights[i - (z > 0 ? static_cast<size_t>(mw) : 0)];
            const float south = heights[i + (z < mw - 1 ? static_cast<size_t>(mw) : 0)];
            const float slope = std::sqrt((east - west) * (east - west) + (south - north) * (south - north)) / (2.0f * s.map_spacing);
            const float slope_degrees = std::atan(slope) * 57.2957795f;

            const float cover = std::clamp(maps.sediment[i] / 0.3f, 0.0f, 1.0f);
            const float steep = static_cast<float>(SmoothStep(30.0, 46.0, slope_degrees));
            const float scoured = static_cast<float>(SmoothStep(0.05, 0.4, fine.eroded[i]));
            maps.rock[i] = std::clamp(std::max(steep, scoured * 0.6f) * (1.0f - cover), 0.0f, 1.0f);

            const float specific_area = maps.flow[i] / s.map_spacing;
            const float wetness = std::log(specific_area / std::max(slope, 0.002f));
            const float from_wetness = static_cast<float>(SmoothStep(5.0, 13.0, wetness));
            const float from_water = std::exp(-maps.water_distance[i] / 25.0f);
            maps.moisture[i] = maps.water_distance[i] == 0.0f ? 1.0f : std::clamp(std::max(from_wetness, from_water), 0.0f, 1.0f);
        }
    }
}

void ValidateSettings(const TerrainGeneratorSettings& s) {
    const auto multiple = [&](float value) {
        const double ratio = value / s.spacing;
        return value >= s.spacing && std::fabs(ratio - std::round(ratio)) < 1e-4;
    };
    if (s.spacing <= 0.0f || s.size < 64.0f * s.coarse_spacing || !multiple(s.coarse_spacing) || !multiple(s.map_spacing)) {
        FatalError("GenerateTerrain: needs spacing > 0, coarse and map spacing as whole multiples of it, and a size of at least 64 coarse steps");
    }
    const auto divides = [&](float value) {
        const double steps = s.size / value;
        return std::fabs(steps - std::round(steps)) < 1e-4;
    };
    if (!divides(s.spacing) || !divides(s.coarse_spacing) || !divides(s.map_spacing)) {
        FatalError("GenerateTerrain: the size must be a whole number of every spacing");
    }
    if ((s.outlet_edges & 15) == 0) {
        FatalError("GenerateTerrain: at least one outlet edge is needed, or rivers would have nowhere to go");
    }
    if (s.erosion_steps < 0 || s.thermal_iterations < 0 || s.relief <= 0.0f) {
        FatalError("GenerateTerrain: step counts must not be negative and the relief must be above zero");
    }
}

template <typename T>
void HashBytes(std::uint64_t& hash, const T* data, size_t count) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
    for (size_t i = 0; i < count * sizeof(T); ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }
}

} // namespace

float TerrainMaps::Sample(const std::vector<float>& map, float x, float z) const {
    if (map.empty() || map_width < 2 || map_depth < 2) {
        return 0.0f;
    }
    const float gx = glm::clamp(x / map_spacing, 0.0f, static_cast<float>(map_width - 1));
    const float gz = glm::clamp(z / map_spacing, 0.0f, static_cast<float>(map_depth - 1));
    const int ix = glm::min(static_cast<int>(gx), map_width - 2);
    const int iz = glm::min(static_cast<int>(gz), map_depth - 2);
    const float fx = gx - static_cast<float>(ix);
    const float fz = gz - static_cast<float>(iz);
    const float* row = &map[static_cast<size_t>(iz) * static_cast<size_t>(map_width) + static_cast<size_t>(ix)];
    const float* next_row = row + map_width;
    return glm::mix(glm::mix(row[0], row[1], fx), glm::mix(next_row[0], next_row[1], fx), fz);
}

float TerrainMaps::GetWaterDepth(float x, float z) const {
    if (water_level.empty()) {
        return 0.0f;
    }
    const int ix = glm::clamp(static_cast<int>(std::lround(x / map_spacing)), 0, map_width - 1);
    const int iz = glm::clamp(static_cast<int>(std::lround(z / map_spacing)), 0, map_depth - 1);
    const float level = water_level[static_cast<size_t>(iz) * static_cast<size_t>(map_width) + static_cast<size_t>(ix)];
    if (level == kNoWater) {
        return 0.0f;
    }
    return glm::max(level - height.GetHeight(x, z), 0.0f);
}

void GenerateTerrain(const TerrainGeneratorSettings& settings, TerrainMaps& maps) {
    ValidateSettings(settings);
    CoarseResult coarse;
    SimulateLargeScale(settings, coarse);

    maps = TerrainMaps{};
    maps.map_spacing = settings.map_spacing;
    maps.map_width = static_cast<int>(std::lround(settings.size / settings.map_spacing)) + 1;
    maps.map_depth = maps.map_width;
    FineState fine;
    fine.deposited.assign(static_cast<size_t>(maps.map_width) * static_cast<size_t>(maps.map_depth), 0.0f);
    fine.eroded.assign(fine.deposited.size(), 0.0f);
    BuildFineLand(settings, coarse, fine);
    ThermalErosion(settings, maps.map_width, settings.thermal_iterations, fine);
    DropletErosion(settings, maps.map_width, fine);
    // A few more slides settle the sharp edges raindrops leave.
    ThermalErosion(settings, maps.map_width, std::min(settings.thermal_iterations, 4), fine);
    BuildMaps(settings, coarse, fine, maps);

    maps.height.width = fine.width;
    maps.height.depth = fine.width;
    maps.height.spacing = settings.spacing;
    maps.height.heights = std::move(fine.heights);
}

std::uint64_t HashTerrainMaps(const TerrainMaps& maps) {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    const int sizes[4] = {maps.height.width, maps.height.depth, maps.map_width, maps.map_depth};
    HashBytes(hash, sizes, 4);
    HashBytes(hash, &maps.height.spacing, 1);
    HashBytes(hash, &maps.map_spacing, 1);
    HashBytes(hash, maps.height.heights.data(), maps.height.heights.size());
    for (const std::vector<float>* map :
         {&maps.water_level, &maps.flow, &maps.moisture, &maps.sediment, &maps.rock, &maps.water_distance}) {
        HashBytes(hash, map->data(), map->size());
    }
    for (const Lake& lake : maps.lakes) {
        const float values[5] = {lake.center.x, lake.center.y, lake.level, lake.area, lake.max_depth};
        HashBytes(hash, values, 5);
    }
    return hash;
}

std::uint64_t HashTerrainGeneratorSettings(const TerrainGeneratorSettings& s) {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    HashBytes(hash, &kGeneratorVersion, 1);
    HashBytes(hash, &s.seed, 1);
    const float floats[] = {s.size, s.spacing, s.coarse_spacing, s.map_spacing, s.relief, s.initial_relief,
                            s.initial_hill_size, s.years_per_step, s.uplift,
                            s.erodibility, s.area_exponent, s.hillslope_diffusion, s.basin_radius, s.basin_start,
                            s.basin_sinking, s.detail_height,
                            s.talus_degrees, s.droplets_per_square_meter, s.droplet_strength, s.min_lake_area,
                            s.min_lake_depth};
    HashBytes(hash, floats, sizeof(floats) / sizeof(floats[0]));
    const int ints[] = {s.erosion_steps, s.basin_count, s.outlet_edges, s.thermal_iterations, s.droplet_steps};
    HashBytes(hash, ints, sizeof(ints) / sizeof(ints[0]));
    return hash;
}

namespace {

struct SettingField {
    const char* name;
    float* real = nullptr;
    int* whole = nullptr;
};

std::vector<SettingField> SettingFields(TerrainGeneratorSettings& s) {
    return {{"size", &s.size},
            {"spacing", &s.spacing},
            {"coarse_spacing", &s.coarse_spacing},
            {"map_spacing", &s.map_spacing},
            {"relief", &s.relief},
            {"initial_relief", &s.initial_relief},
            {"initial_hill_size", &s.initial_hill_size},
            {"erosion_steps", nullptr, &s.erosion_steps},
            {"years_per_step", &s.years_per_step},
            {"uplift", &s.uplift},
            {"erodibility", &s.erodibility},
            {"area_exponent", &s.area_exponent},
            {"hillslope_diffusion", &s.hillslope_diffusion},
            {"basin_count", nullptr, &s.basin_count},
            {"basin_radius", &s.basin_radius},
            {"basin_start", &s.basin_start},
            {"basin_sinking", &s.basin_sinking},
            {"outlet_edges", nullptr, &s.outlet_edges},
            {"detail_height", &s.detail_height},
            {"talus_degrees", &s.talus_degrees},
            {"thermal_iterations", nullptr, &s.thermal_iterations},
            {"droplets_per_square_meter", &s.droplets_per_square_meter},
            {"droplet_strength", &s.droplet_strength},
            {"droplet_steps", nullptr, &s.droplet_steps},
            {"min_lake_area", &s.min_lake_area},
            {"min_lake_depth", &s.min_lake_depth}};
}

} // namespace

bool SetTerrainGeneratorSetting(TerrainGeneratorSettings& settings, const std::string& name, const std::string& value) {
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    if (name == "seed") {
        const unsigned long long seed = std::strtoull(value.c_str(), &end, 10);
        if (*end != '\0' || value[0] == '-') {
            return false;
        }
        settings.seed = seed;
        return true;
    }
    for (const SettingField& field : SettingFields(settings)) {
        if (name != field.name) {
            continue;
        }
        if (field.real != nullptr) {
            const float number = std::strtof(value.c_str(), &end);
            if (*end != '\0' || !std::isfinite(number)) {
                return false;
            }
            *field.real = number;
        } else {
            const long number = std::strtol(value.c_str(), &end, 10);
            if (*end != '\0') {
                return false;
            }
            *field.whole = static_cast<int>(number);
        }
        return true;
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> GetTerrainGeneratorSettings(const TerrainGeneratorSettings& settings) {
    TerrainGeneratorSettings copy = settings;
    std::vector<std::pair<std::string, std::string>> result;
    result.emplace_back("seed", std::to_string(copy.seed));
    for (const SettingField& field : SettingFields(copy)) {
        char text[64];
        if (field.real != nullptr) {
            std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(*field.real));
        } else {
            std::snprintf(text, sizeof(text), "%d", *field.whole);
        }
        result.emplace_back(field.name, text);
    }
    return result;
}

bool FindLakeViewpoint(const TerrainMaps& maps, float eye_height, LakeViewpoint& viewpoint) {
    const HeightMap& map = maps.height;
    viewpoint = LakeViewpoint{};
    viewpoint.position = glm::vec2(map.GetSizeX() * 0.5f, map.GetSizeZ() * 0.5f);
    for (size_t i = 0; i < maps.lakes.size(); ++i) {
        if (viewpoint.lake < 0 || maps.lakes[i].area > maps.lakes[static_cast<size_t>(viewpoint.lake)].area) {
            viewpoint.lake = static_cast<int>(i);
        }
    }
    if (viewpoint.lake < 0) {
        return false;
    }
    const Lake& lake = maps.lakes[static_cast<size_t>(viewpoint.lake)];
    float best_score = -1.0e30f;
    bool found = false;
    const float margin = 300.0f;
    for (float radius = 150.0f; radius <= 500.0f; radius += 50.0f) {
        for (int step = 0; step < 72; ++step) {
            const float angle = static_cast<float>(step) * 0.0872665f;
            const glm::vec2 place = lake.center + radius * glm::vec2(std::cos(angle), std::sin(angle));
            if (place.x < margin || place.y < margin || place.x > map.GetSizeX() - margin || place.y > map.GetSizeZ() - margin) {
                continue;
            }
            if (maps.Sample(maps.water_distance, place.x, place.y) < 30.0f || map.GetSlopeDegrees(place.x, place.y) > 12.0f) {
                continue;
            }
            const float above = map.GetHeight(place.x, place.y) - lake.level;
            if (above < 6.0f) {
                continue;
            }
            // The line from the eye to the lake's middle must stay above the ground.
            const glm::vec3 eye(place.x, map.GetHeight(place.x, place.y) + eye_height, place.y);
            const glm::vec3 target(lake.center.x, lake.level, lake.center.y);
            bool seen = true;
            for (int sample = 1; sample < 64 && seen; ++sample) {
                const glm::vec3 point = glm::mix(eye, target, static_cast<float>(sample) / 64.0f);
                seen = map.GetHeight(point.x, point.z) < point.y - 0.3f;
            }
            if (!seen) {
                continue;
            }
            const float score = std::min(above, 40.0f) - radius * 0.02f;
            if (score > best_score) {
                best_score = score;
                viewpoint.position = place;
                found = true;
            }
        }
    }
    if (!found) {
        return false;
    }
    viewpoint.direction = glm::normalize(lake.center - viewpoint.position);
    viewpoint.distance = glm::length(lake.center - viewpoint.position);
    viewpoint.yaw_degrees = glm::degrees(std::atan2(-viewpoint.direction.x, -viewpoint.direction.y));
    return true;
}

bool SaveTerrainMaps(const char* path, const TerrainMaps& maps) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    char header[256];
    std::snprintf(header, sizeof(header), "poligon-terrain-maps 1 %d %d %.9g %d %d %.9g %d\n", maps.height.width,
                  maps.height.depth, static_cast<double>(maps.height.spacing), maps.map_width, maps.map_depth,
                  static_cast<double>(maps.map_spacing), static_cast<int>(maps.lakes.size()));
    file << header;
    const auto write = [&file](const std::vector<float>& values) {
        file.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(float)));
    };
    write(maps.height.heights);
    for (const std::vector<float>* map :
         {&maps.water_level, &maps.flow, &maps.moisture, &maps.sediment, &maps.rock, &maps.water_distance}) {
        write(*map);
    }
    for (const Lake& lake : maps.lakes) {
        const float values[5] = {lake.center.x, lake.center.y, lake.level, lake.area, lake.max_depth};
        file.write(reinterpret_cast<const char*>(values), sizeof(values));
    }
    return static_cast<bool>(file);
}

bool LoadTerrainMaps(const char* path, TerrainMaps& maps, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = std::string("cannot open ") + path;
        return false;
    }
    std::string line;
    std::getline(file, line);
    TerrainMaps loaded;
    int version = 0;
    int lake_count = 0;
    double spacing = 0.0;
    double map_spacing = 0.0;
    std::istringstream header(line);
    std::string magic;
    header >> magic >> version >> loaded.height.width >> loaded.height.depth >> spacing >> loaded.map_width >>
        loaded.map_depth >> map_spacing >> lake_count;
    if (!header || magic != "poligon-terrain-maps" || version != 1 || loaded.height.width < 2 || loaded.height.depth < 2 || loaded.map_width < 2 ||
        loaded.map_depth < 2 || lake_count < 0) {
        error = std::string(path) + ": not a poligon terrain maps file";
        return false;
    }
    loaded.height.spacing = static_cast<float>(spacing);
    loaded.map_spacing = static_cast<float>(map_spacing);
    const auto read = [&file](std::vector<float>& values, size_t count) {
        values.resize(count);
        file.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(float)));
    };
    read(loaded.height.heights, static_cast<size_t>(loaded.height.width) * static_cast<size_t>(loaded.height.depth));
    const size_t map_count = static_cast<size_t>(loaded.map_width) * static_cast<size_t>(loaded.map_depth);
    for (std::vector<float>* map : {&loaded.water_level, &loaded.flow, &loaded.moisture, &loaded.sediment, &loaded.rock,
                                    &loaded.water_distance}) {
        read(*map, map_count);
    }
    for (int i = 0; i < lake_count; ++i) {
        float values[5] = {};
        file.read(reinterpret_cast<char*>(values), sizeof(values));
        Lake lake;
        lake.center = glm::vec2(values[0], values[1]);
        lake.level = values[2];
        lake.area = values[3];
        lake.max_depth = values[4];
        loaded.lakes.push_back(lake);
    }
    if (!file) {
        error = std::string(path) + ": the file is shorter than its header says";
        return false;
    }
    maps = std::move(loaded);
    return true;
}

bool LoadOrGenerateTerrain(const TerrainGeneratorSettings& settings, const char* cache_directory, TerrainMaps& maps,
                           bool& generated, std::string& error) {
    char name[64];
    std::snprintf(name, sizeof(name), "terrain_%016llx.bin",
                  static_cast<unsigned long long>(HashTerrainGeneratorSettings(settings)));
    const std::filesystem::path path = std::filesystem::path(cache_directory) / name;
    std::string load_error;
    if (std::filesystem::exists(path) && LoadTerrainMaps(path.string().c_str(), maps, load_error)) {
        generated = false;
        return true;
    }
    GenerateTerrain(settings, maps);
    generated = true;
    std::error_code ignored;
    std::filesystem::create_directories(cache_directory, ignored);
    if (!SaveTerrainMaps(path.string().c_str(), maps)) {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

} // namespace poligon
