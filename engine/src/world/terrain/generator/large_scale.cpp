// Terrain generator, stage 1: uplift and stream-power erosion on a coarse grid (see generator_internal.h).

#include "generator_internal.h"

namespace poligon {
namespace terrain_generator_internal {

// --- Stage 1: large scale ---


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


} // namespace terrain_generator_internal
} // namespace poligon
