// Terrain generator, stage 3: the maps (see generator_internal.h).

#include "generator_internal.h"

namespace poligon {
namespace terrain_generator_internal {

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
            // Linear in the index: ridges dry (about 3.5 and less), hillsides in between,
            // valley floors wet (about 14.5 and more).
            const float from_wetness = std::clamp((wetness - 3.5f) / 11.0f, 0.0f, 1.0f);
            const float from_water = std::exp(-maps.water_distance[i] / 25.0f);
            maps.moisture[i] = maps.water_distance[i] == 0.0f ? 1.0f : std::clamp(std::max(from_wetness, from_water), 0.0f, 1.0f);
        }
    }
}


} // namespace terrain_generator_internal
} // namespace poligon
