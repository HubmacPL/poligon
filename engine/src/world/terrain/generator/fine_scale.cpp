// Terrain generator, stage 2: the land at full resolution from the coarse one, then thermal and
// droplet erosion (see generator_internal.h).

#include "generator_internal.h"

namespace poligon {
namespace terrain_generator_internal {

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


} // namespace terrain_generator_internal
} // namespace poligon
