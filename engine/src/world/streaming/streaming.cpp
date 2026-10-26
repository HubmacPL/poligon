#include "poligon/world/streaming.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <utility>

#include "poligon/core/fatal_error.h"
#include "poligon/core/worker.h"

namespace poligon {

StreamingGrid::StreamingGrid(const StreamingGridSettings& settings) : settings_(settings) {
    if (settings.patch_size <= 0.0 || settings.patch_count_x < 1 || settings.patch_count_z < 1 ||
        settings.patch_count_x > (1 << 19) || settings.patch_count_z > (1 << 19)) {
        FatalError("StreamingGrid: needs a patch size above zero and 1 to 524288 patches each way");
    }
}

PatchCoord StreamingGrid::GetPatchAt(const glm::dvec3& position) const {
    PatchCoord patch;
    patch.x = static_cast<int>(std::floor((position.x - settings_.origin.x) / settings_.patch_size));
    patch.z = static_cast<int>(std::floor((position.z - settings_.origin.y) / settings_.patch_size));
    return patch;
}

bool StreamingGrid::Contains(PatchCoord patch) const {
    return patch.x >= 0 && patch.z >= 0 && patch.x < settings_.patch_count_x && patch.z < settings_.patch_count_z;
}

glm::dvec2 StreamingGrid::GetCorner(PatchCoord patch) const {
    return settings_.origin + glm::dvec2(static_cast<double>(patch.x), static_cast<double>(patch.z)) * settings_.patch_size;
}

double StreamingGrid::GetDistance(const glm::dvec3& position, PatchCoord patch) const {
    const glm::dvec2 low = GetCorner(patch);
    const glm::dvec2 high = low + glm::dvec2(settings_.patch_size);
    const double dx = std::max({low.x - position.x, position.x - high.x, 0.0});
    const double dz = std::max({low.y - position.z, position.z - high.y, 0.0});
    return std::max(dx, dz);
}

int StreamingGrid::GetRing(PatchCoord center, PatchCoord patch) {
    return std::max(std::abs(patch.x - center.x), std::abs(patch.z - center.z));
}

std::vector<PatchCoord> StreamingGrid::GetWindow(const glm::dvec3& position, int radius) const {
    const PatchCoord center = GetPatchAt(position);
    std::vector<PatchCoord> window;
    for (int ring = 0; ring <= radius; ++ring) {
        for (int z = center.z - ring; z <= center.z + ring; ++z) {
            for (int x = center.x - ring; x <= center.x + ring; ++x) {
                const PatchCoord patch{x, z};
                if (GetRing(center, patch) == ring && Contains(patch)) {
                    window.push_back(patch);
                }
            }
        }
    }
    return window;
}

namespace {

// A load job's tag: the source's number and the patch.
std::uint64_t MakeTag(int source, PatchCoord patch) {
    return (static_cast<std::uint64_t>(source) << 40) | (static_cast<std::uint64_t>(patch.x) << 20) |
           static_cast<std::uint64_t>(patch.z);
}

int TagSource(std::uint64_t tag) {
    return static_cast<int>(tag >> 40);
}

PatchCoord TagPatch(std::uint64_t tag) {
    return {static_cast<int>((tag >> 20) & 0xFFFFF), static_cast<int>(tag & 0xFFFFF)};
}

} // namespace

struct WorldStreamer::Impl {
    struct Source {
        PatchSource* source = nullptr;
        int radius = 0;
        StreamingMode mode = StreamingMode::Background;
        std::set<PatchCoord> attached;
        std::set<PatchCoord> pending;  // queued, loading, or loaded and not yet handed over
        std::set<PatchCoord> wanted;   // the current window
        std::vector<PatchCoord> attach_order;
    };
    std::vector<Source> sources;
    BackgroundWorker worker{"poligon streaming"};
    StreamingStats stats;
    bool synchronous = false;
    int max_attach_per_frame = 4;
    bool has_observer = false;
    PatchCoord observer_patch;

    void AttachLoaded(int index, PatchCoord patch, std::unique_ptr<PatchData> data) {
        Source& source = sources[static_cast<size_t>(index)];
        source.source->Attach(patch, std::move(data));
        source.attached.insert(patch);
        source.attach_order.push_back(patch);
        ++stats.attached;
    }

    void Detach(int index, PatchCoord patch) {
        Source& source = sources[static_cast<size_t>(index)];
        source.source->Detach(patch);
        source.attached.erase(patch);
        ++stats.detached;
    }

    // Queues a background load; its result is handed over in RunFinished if the patch is
    // still wanted then.
    void Queue(int index, PatchCoord patch) {
        Source& source = sources[static_cast<size_t>(index)];
        source.pending.insert(patch);
        ++stats.loads_started;
        PatchSource* loader = source.source;
        worker.Push(MakeTag(index, patch), [this, loader, index, patch]() -> BackgroundWorker::Finish {
            std::shared_ptr<std::unique_ptr<PatchData>> box = std::make_shared<std::unique_ptr<PatchData>>(loader->Load(patch));
            return [this, index, patch, box] {
                Source& owner = sources[static_cast<size_t>(index)];
                owner.pending.erase(patch);
                if (owner.wanted.count(patch) == 0 || owner.attached.count(patch) != 0) {
                    ++stats.discarded;
                    return;
                }
                AttachLoaded(index, patch, std::move(*box));
            };
        });
    }
};

WorldStreamer::WorldStreamer(const StreamingGrid& grid) : grid_(grid), impl_(std::make_unique<Impl>()) {}

WorldStreamer::~WorldStreamer() = default;

int WorldStreamer::AddSource(PatchSource& source, int radius, StreamingMode mode) {
    if (radius < 0 || radius > 64) {
        FatalError("WorldStreamer::AddSource: the radius must be between 0 and 64 patches");
    }
    Impl::Source added;
    added.source = &source;
    added.radius = radius;
    added.mode = mode;
    impl_->sources.push_back(std::move(added));
    impl_->has_observer = false;  // the next Update fills the new source's window
    return static_cast<int>(impl_->sources.size()) - 1;
}

void WorldStreamer::Update(const glm::dvec3& observer) {
    Impl& impl = *impl_;
    const PatchCoord center = grid_.GetPatchAt(observer);
    const bool moved = !impl.has_observer || center != impl.observer_patch;
    impl.has_observer = true;
    impl.observer_patch = center;
    if (moved) {
        // New windows; loads still queued are dropped and queued again nearest first.
        for (const std::uint64_t tag : impl.worker.CancelAll()) {
            impl.sources[static_cast<size_t>(TagSource(tag))].pending.erase(TagPatch(tag));
            --impl.stats.loads_started;
        }
    }
    // Every frame: held patches farther than radius + 1/2 patches go (hysteresis); on a new
    // observer patch the windows are made again.
    int largest_radius = 0;
    for (size_t index = 0; index < impl.sources.size(); ++index) {
        Impl::Source& source = impl.sources[index];
        if (source.mode != StreamingMode::Background) {
            continue;
        }
        if (moved) {
            const std::vector<PatchCoord> window = grid_.GetWindow(observer, source.radius);
            source.wanted = std::set<PatchCoord>(window.begin(), window.end());
        }
        const double keep = (static_cast<double>(source.radius) + 0.5) * grid_.GetSettings().patch_size;
        const std::vector<PatchCoord> held(source.attached.begin(), source.attached.end());
        for (const PatchCoord patch : held) {
            if (grid_.GetDistance(observer, patch) > keep && StreamingGrid::GetRing(center, patch) > source.radius) {
                source.wanted.erase(patch);
                impl.Detach(static_cast<int>(index), patch);
            } else {
                source.wanted.insert(patch);
            }
        }
        largest_radius = std::max(largest_radius, source.radius);
    }
    if (moved) {
        // Ring by ring over all sources, so that every kind of data near the observer comes first.
        const std::vector<PatchCoord> window = grid_.GetWindow(observer, largest_radius);
        for (const PatchCoord patch : window) {
            const int ring = StreamingGrid::GetRing(center, patch);
            for (size_t index = 0; index < impl.sources.size(); ++index) {
                Impl::Source& source = impl.sources[index];
                if (source.mode != StreamingMode::Background || ring > source.radius || source.attached.count(patch) != 0 ||
                    source.pending.count(patch) != 0) {
                    continue;
                }
                if (impl.synchronous) {
                    ++impl.stats.loads_started;
                    impl.AttachLoaded(static_cast<int>(index), patch, source.source->Load(patch));
                } else {
                    impl.Queue(static_cast<int>(index), patch);
                }
            }
        }
    }
    impl.worker.RunFinished(impl.max_attach_per_frame);
}

void WorldStreamer::RequirePatches(const std::vector<glm::dvec3>& positions) {
    Impl& impl = *impl_;
    for (size_t index = 0; index < impl.sources.size(); ++index) {
        Impl::Source& source = impl.sources[index];
        if (source.mode != StreamingMode::Immediate) {
            continue;
        }
        // The union of the windows, in a fixed order: by position, then nearest ring first.
        std::vector<PatchCoord> needed;
        std::set<PatchCoord> wanted;
        for (const glm::dvec3& position : positions) {
            for (const PatchCoord patch : grid_.GetWindow(position, source.radius)) {
                if (wanted.insert(patch).second) {
                    needed.push_back(patch);
                }
            }
        }
        // Held patches stay until every position is more than radius + 1/2 patches away (hysteresis).
        const double keep = (static_cast<double>(source.radius) + 0.5) * grid_.GetSettings().patch_size;
        const std::vector<PatchCoord> held(source.attached.begin(), source.attached.end());
        for (const PatchCoord patch : held) {
            bool near = wanted.count(patch) != 0;
            for (size_t i = 0; i < positions.size() && !near; ++i) {
                near = grid_.GetDistance(positions[i], patch) <= keep;
            }
            if (near) {
                wanted.insert(patch);
            } else {
                impl.Detach(static_cast<int>(index), patch);
            }
        }
        source.wanted = wanted;
        for (const PatchCoord patch : needed) {
            if (source.attached.count(patch) == 0) {
                ++impl.stats.loads_started;
                impl.AttachLoaded(static_cast<int>(index), patch, source.source->Load(patch));
            }
        }
    }
}

void WorldStreamer::SetSynchronous(bool synchronous) {
    impl_->synchronous = synchronous;
    if (synchronous) {
        FinishLoading();
    }
}

void WorldStreamer::FinishLoading() {
    impl_->worker.WaitUntilIdle();
    impl_->worker.RunFinished();
}

void WorldStreamer::SetMaxAttachPerFrame(int count) {
    impl_->max_attach_per_frame = std::max(count, 1);
}

void WorldStreamer::SetLoadDelayMilliseconds(int milliseconds) {
    impl_->worker.SetDelayMilliseconds(milliseconds);
}

void WorldStreamer::DetachAll() {
    for (const std::uint64_t tag : impl_->worker.CancelAll()) {
        impl_->sources[static_cast<size_t>(TagSource(tag))].pending.erase(TagPatch(tag));
    }
    impl_->worker.WaitUntilIdle();
    for (Impl::Source& source : impl_->sources) {
        source.wanted.clear();
    }
    impl_->worker.RunFinished();  // nothing is wanted: finished loads are discarded
    for (size_t index = 0; index < impl_->sources.size(); ++index) {
        const std::vector<PatchCoord> held(impl_->sources[index].attached.begin(), impl_->sources[index].attached.end());
        for (const PatchCoord patch : held) {
            impl_->Detach(static_cast<int>(index), patch);
        }
    }
    impl_->has_observer = false;
}

bool WorldStreamer::IsAttached(int source, PatchCoord patch) const {
    return impl_->sources.at(static_cast<size_t>(source)).attached.count(patch) != 0;
}

std::vector<PatchCoord> WorldStreamer::GetAttached(int source) const {
    const std::set<PatchCoord>& attached = impl_->sources.at(static_cast<size_t>(source)).attached;
    return std::vector<PatchCoord>(attached.begin(), attached.end());
}

const std::vector<PatchCoord>& WorldStreamer::GetAttachOrder(int source) const {
    return impl_->sources.at(static_cast<size_t>(source)).attach_order;
}

StreamingStats WorldStreamer::GetStats() const {
    StreamingStats stats = impl_->stats;
    stats.pending = impl_->worker.GetPendingCount();
    return stats;
}

} // namespace poligon
