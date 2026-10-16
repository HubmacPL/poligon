// Checks height maps: reading heights and slopes, noise maps, the TIFF reader (on a file
// the test writes itself), Poligon's own file format, joining tiles and rescaling.
// Usage: height_map_test OUTPUT_DIRECTORY
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "poligon/world/height_map.h"
#include "test_support.h"

namespace {

bool Near(float a, float b, float tolerance = 1.0e-4f) {
    return std::fabs(a - b) <= tolerance;
}

template <typename T>
void Append(std::vector<unsigned char>& bytes, T value) {
    const unsigned char* raw = reinterpret_cast<const unsigned char*>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof(T));
}

void AppendEntry(std::vector<unsigned char>& bytes, std::uint16_t tag, std::uint16_t type, std::uint32_t count, std::uint32_t value) {
    Append(bytes, tag);
    Append(bytes, type);
    Append(bytes, count);
    if (type == 3 && count == 1) {
        Append(bytes, static_cast<std::uint16_t>(value));
        Append(bytes, static_cast<std::uint16_t>(0));
    } else {
        Append(bytes, value);
    }
}

// Writes a TIFF the way the Geoportal service does: floats, no compression, two strips.
void WriteTestTiff(const std::string& path, int width, int depth, const std::vector<float>& heights, std::uint16_t compression) {
    std::vector<unsigned char> bytes = {'I', 'I', 42, 0};
    Append(bytes, std::uint32_t{0});  // place of the directory, filled in below
    const int first_rows = depth / 2;
    const std::uint32_t strip_offsets[2] = {static_cast<std::uint32_t>(bytes.size()),
                                            static_cast<std::uint32_t>(bytes.size()) + static_cast<std::uint32_t>(first_rows * width * 4)};
    const std::uint32_t strip_sizes[2] = {static_cast<std::uint32_t>(first_rows * width * 4),
                                          static_cast<std::uint32_t>((depth - first_rows) * width * 4)};
    for (const float height : heights) {
        Append(bytes, height);
    }
    const std::uint32_t offsets_at = static_cast<std::uint32_t>(bytes.size());
    Append(bytes, strip_offsets[0]);
    Append(bytes, strip_offsets[1]);
    const std::uint32_t sizes_at = static_cast<std::uint32_t>(bytes.size());
    Append(bytes, strip_sizes[0]);
    Append(bytes, strip_sizes[1]);
    const std::uint32_t directory = static_cast<std::uint32_t>(bytes.size());
    std::memcpy(&bytes[4], &directory, 4);
    Append(bytes, std::uint16_t{8});
    AppendEntry(bytes, 256, 3, 1, static_cast<std::uint32_t>(width));
    AppendEntry(bytes, 257, 3, 1, static_cast<std::uint32_t>(depth));
    AppendEntry(bytes, 258, 3, 1, 32);
    AppendEntry(bytes, 259, 3, 1, compression);
    AppendEntry(bytes, 273, 4, 2, offsets_at);
    AppendEntry(bytes, 277, 3, 1, 1);
    AppendEntry(bytes, 279, 4, 2, sizes_at);
    AppendEntry(bytes, 339, 3, 1, 3);
    Append(bytes, std::uint32_t{0});
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;
    if (argc != 2) {
        std::printf("usage: height_map_test OUTPUT_DIRECTORY\n");
        return 2;
    }
    const std::string directory = argv[1];
    std::string error;

    // --- A slope: heights rise by 1 m per meter along x, on a grid with 2 m between points ---
    poligon::HeightMap slope;
    slope.width = 5;
    slope.depth = 4;
    slope.spacing = 2.0f;
    for (int iz = 0; iz < slope.depth; ++iz) {
        for (int ix = 0; ix < slope.width; ++ix) {
            slope.heights.push_back(static_cast<float>(ix) * 2.0f);
        }
    }
    Expect(Near(slope.GetSizeX(), 8.0f) && Near(slope.GetSizeZ(), 6.0f), "five points 2 m apart cover 8 m");
    Expect(Near(slope.GetHeight(4.0f, 2.0f), 4.0f), "the height at a grid point is that point's value");
    Expect(Near(slope.GetHeight(3.0f, 2.7f), 3.0f), "between points the height is blended");
    Expect(Near(slope.GetHeight(-5.0f, 100.0f), 0.0f) && Near(slope.GetHeight(50.0f, 1.0f), 8.0f),
           "outside the map the nearest edge counts");
    Expect(Near(slope.GetSlopeDegrees(4.0f, 2.0f), 45.0f, 0.01f), "a rise of 1 m per meter is a 45 degree slope");
    const glm::vec3 normal = slope.GetNormal(4.0f, 2.0f);
    Expect(normal.x < -0.7f && normal.y > 0.7f && Near(normal.z, 0.0f), "the normal leans away from the uphill side");
    float lowest = 0.0f;
    float highest = 0.0f;
    slope.GetHeightRange(lowest, highest);
    Expect(Near(lowest, 0.0f) && Near(highest, 8.0f), "the height range is found");

    // --- Noise ---
    const poligon::HeightMap hills = poligon::MakeNoiseHeightMap(129, 129, 4.0f, 7, 120.0f, 40.0f);
    const poligon::HeightMap same_hills = poligon::MakeNoiseHeightMap(129, 129, 4.0f, 7, 120.0f, 40.0f);
    const poligon::HeightMap other_hills = poligon::MakeNoiseHeightMap(129, 129, 4.0f, 8, 120.0f, 40.0f);
    Expect(hills.heights == same_hills.heights, "the same seed gives the same hills");
    Expect(hills.heights != other_hills.heights, "another seed gives other hills");
    hills.GetHeightRange(lowest, highest);
    Expect(lowest >= 0.0f && highest <= 40.0f && highest - lowest > 10.0f, "hills stay within the asked height and are not flat");
    float steepest = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        steepest = std::fmax(steepest, hills.GetSlopeDegrees(static_cast<float>(i % 50) * 10.0f, static_cast<float>(i / 50) * 12.0f));
    }
    Expect(steepest > 3.0f && steepest < 60.0f, "noise hills have slopes, but no cliffs");

    // --- The TIFF reader ---
    {
        std::vector<float> heights;
        for (int i = 0; i < 6 * 5; ++i) {
            heights.push_back(200.0f + static_cast<float>(i) * 0.25f);
        }
        heights[7] = 0.0f;  // the service writes 0 where it has no data
        const std::string path = directory + "/height_map_test.tif";
        WriteTestTiff(path, 6, 5, heights, 1);
        poligon::HeightMap loaded;
        int missing = -1;
        Expect(poligon::LoadHeightMapFromTiff(path.c_str(), 2.0f, loaded, missing, error), "a float TIFF in strips loads");
        Expect(loaded.width == 6 && loaded.depth == 5 && Near(loaded.spacing, 2.0f), "size and spacing are set");
        Expect(loaded.heights == heights, "every height is read exactly");
        Expect(missing == 1, "pixels without data are counted");

        const std::string compressed_path = directory + "/height_map_test_compressed.tif";
        WriteTestTiff(compressed_path, 6, 5, heights, 5);
        Expect(!poligon::LoadHeightMapFromTiff(compressed_path.c_str(), 2.0f, loaded, missing, error) &&
                   error.find("compressed") != std::string::npos,
               "a compressed TIFF is refused with a clear message");
        Expect(!poligon::LoadHeightMapFromTiff((directory + "/no_such.tif").c_str(), 2.0f, loaded, missing, error),
               "a missing file is an error");
    }

    // --- Poligon's own format ---
    {
        const std::string path = directory + "/height_map_test.heights";
        Expect(poligon::SaveHeightMap(path.c_str(), hills), "a height map is saved");
        poligon::HeightMap loaded;
        Expect(poligon::LoadHeightMap(path.c_str(), loaded, error), "and loads again");
        Expect(loaded.width == hills.width && loaded.depth == hills.depth && loaded.spacing == hills.spacing &&
                   loaded.heights == hills.heights,
               "with exactly the same contents");
        std::ofstream(directory + "/height_map_bad.heights") << "something else\n";
        Expect(!poligon::LoadHeightMap((directory + "/height_map_bad.heights").c_str(), loaded, error),
               "a file of another kind is refused");
    }

    // --- Joining tiles and rescaling ---
    {
        poligon::HeightMap whole;
        whole.width = 6;
        whole.depth = 4;
        whole.spacing = 2.0f;
        whole.heights.assign(24, 100.0f);
        poligon::HeightMap tile;
        tile.width = 3;
        tile.depth = 2;
        tile.spacing = 2.0f;
        tile.heights = {110.0f, 120.0f, 130.0f, 140.0f, 150.0f, 160.0f};
        poligon::PasteHeightMap(whole, tile, 2, 1);
        Expect(whole.heights[1 * 6 + 2] == 110.0f && whole.heights[2 * 6 + 4] == 160.0f && whole.heights[0] == 100.0f,
               "a tile lands where it is pasted");
        // Pinholes get the average of their neighbors.
        poligon::HeightMap holed = whole;
        holed.heights[2 * 6 + 3] = 0.0f;
        holed.heights[0] = -9999.0f;
        Expect(poligon::FillMissingHeights(holed) == 2, "two points without data are found");
        Expect(holed.heights[2 * 6 + 3] > 100.0f && holed.heights[2 * 6 + 3] < 160.0f && holed.heights[0] == 100.0f,
               "and filled from the points around them");

        poligon::PasteHeightMap(whole, tile, 5, 3);
        Expect(whole.heights[3 * 6 + 5] == 110.0f, "a tile hanging over the edge is cut off, not an error");

        // A part of a map keeps the heights and the spacing.
        const poligon::HeightMap part = poligon::CropHeightMap(slope, 2, 1, 3, 2);
        Expect(part.width == 3 && part.depth == 2 && Near(part.spacing, 2.0f) && Near(part.heights[0], 4.0f) &&
                   Near(part.heights[5], 8.0f),
               "a cut-out part holds the heights of its place in the map");

        // Halving distances and keeping heights doubles every slope; the lowest point goes to 0.
        const float slope_before = slope.GetSlopeDegrees(4.0f, 2.0f);
        poligon::HeightMap squeezed = slope;
        for (float& height : squeezed.heights) {
            height += 230.0f;
        }
        poligon::RescaleHeightMap(squeezed, 0.5f, 1.0f);
        squeezed.GetHeightRange(lowest, highest);
        Expect(Near(squeezed.spacing, 1.0f) && Near(squeezed.GetSizeX(), 4.0f), "squeezing by half halves the distances");
        Expect(Near(lowest, 0.0f) && Near(highest, 8.0f), "heights keep their differences and start at 0");
        Expect(Near(slope_before, 45.0f, 0.01f) && Near(squeezed.GetSlopeDegrees(2.0f, 1.0f), 63.435f, 0.01f),
               "a 45 degree slope becomes 63 degrees");
    }

    std::printf("height_map_test finished\n");
    return test::ExitCode();
}
