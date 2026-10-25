// Example: world streaming. A world of 16 x 16 patches of 256 m; a source "loads" each patch
// (here: works out its highest point from a noise height map) on the loading thread, and a
// walker crosses the world. The window of 5 x 5 patches follows the walker: patches come in
// nearest ring first and go when they fall out of it.
// Usage: example_streaming
#include <cstdio>
#include <map>
#include <memory>

#include "poligon/world/height_map.h"
#include "poligon/world/streaming.h"

namespace {

// What one patch holds once loaded.
struct HillData : poligon::PatchData {
    float highest = 0.0f;
};

class HillSource : public poligon::PatchSource {
public:
    explicit HillSource(const poligon::HeightMap& map) : map_(map) {}

    // On the loading thread: only reads the map, which nobody changes.
    std::unique_ptr<poligon::PatchData> Load(poligon::PatchCoord patch) override {
        auto data = std::make_unique<HillData>();
        const poligon::HeightMap part = poligon::CutHeightMap(map_, patch.x * 64, patch.z * 64, 65, 65);
        float lowest = 0.0f;
        part.GetHeightRange(lowest, data->highest);
        return data;
    }
    // On the main thread, in WorldStreamer::Update.
    void Attach(poligon::PatchCoord patch, std::unique_ptr<poligon::PatchData> data) override {
        highest_[patch] = static_cast<HillData&>(*data).highest;
    }
    void Detach(poligon::PatchCoord patch) override { highest_.erase(patch); }

    size_t GetCount() const { return highest_.size(); }

private:
    const poligon::HeightMap& map_;
    std::map<poligon::PatchCoord, float> highest_;
};

} // namespace

int main() {
    // The whole world's heights, a point every 4 m (a real game reads patches from files).
    const poligon::HeightMap map = poligon::MakeNoiseHeightMap(1025, 1025, 4.0f, 3u, 600.0f, 80.0f);

    poligon::StreamingGridSettings settings;
    settings.patch_size = 256.0;
    settings.patch_count_x = 16;
    settings.patch_count_z = 16;
    const poligon::StreamingGrid grid(settings);
    poligon::WorldStreamer streamer(grid);
    HillSource hills(map);
    const int source = streamer.AddSource(hills, 2, poligon::StreamingMode::Background);

    // The walker crosses the world diagonally; once per "frame" the streamer is updated.
    for (int step = 0; step <= 8; ++step) {
        const glm::dvec3 walker(300.0 + 400.0 * step, 0.0, 300.0 + 400.0 * step);
        streamer.Update(walker);
        streamer.FinishLoading();  // a game would not wait; here every step shows a full window
        const poligon::PatchCoord here = grid.GetPatchAt(walker);
        std::printf("walker in patch (%d, %d): %zu patches loaded\n", here.x, here.z, hills.GetCount());
    }
    const poligon::StreamingStats stats = streamer.GetStats();
    std::printf("%d patches attached and %d let go in all; the first five came in this order:", stats.attached, stats.detached);
    for (size_t i = 0; i < 5; ++i) {
        const poligon::PatchCoord patch = streamer.GetAttachOrder(source)[i];
        std::printf(" (%d, %d)", patch.x, patch.z);
    }
    std::printf("\n");
    return stats.attached > 25 && stats.detached > 0 ? 0 : 1;
}
