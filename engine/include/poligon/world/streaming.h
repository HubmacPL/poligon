#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "poligon/core/math.h"

namespace poligon {

// World streaming: a large world is cut into square patches (512 m in theHunter Classic's
// way: a 32 768 m reserve is 64 x 64 of them), and only a window of patches around the
// camera is kept in memory. Each kind of data (terrain heights, plants, water...) is its own
// PatchSource with its own window; patches are loaded on a background thread, nearest ring
// first, and handed over on the main thread at one fixed place in the frame
// (WorldStreamer::Update). Patches that leave the window are let go.
//
// Determinism: what the simulation stands on (terrain heights for physics, colliders) must
// never depend on how fast loading went. Such sources are Immediate: they are loaded on the
// calling thread, only when the game asks before a tick (RequirePatches with the positions
// of the player and the animals), and let go only by those positions. Everything only drawn
// is Background. The game state thus depends on the game state alone.
//
//     StreamingGrid grid(settings);
//     WorldStreamer streamer(grid);
//     streamer.AddSource(terrain_graphics, 4, StreamingMode::Background);  // 9 x 9 patches
//     streamer.AddSource(terrain_physics, 1, StreamingMode::Immediate);    // 3 x 3 around each position
//     // before every tick:  streamer.RequirePatches({player_position});
//     // every frame, before drawing:  streamer.Update(camera.position);
//
// Docs: docs/en/streaming.md.

// A patch's place in the grid: column x, row z (patch (0, 0) starts at the grid's origin).
struct PatchCoord {
    int x = 0;
    int z = 0;
    bool operator==(const PatchCoord& other) const { return x == other.x && z == other.z; }
    bool operator!=(const PatchCoord& other) const { return !(*this == other); }
    // Row by row, for sorted lists.
    bool operator<(const PatchCoord& other) const { return z != other.z ? z < other.z : x < other.x; }
};

struct StreamingGridSettings {
    double patch_size = 512.0;   // meters along each side
    glm::dvec2 origin{0.0};      // world x and z of patch (0, 0)'s corner with the smallest x and z
    int patch_count_x = 64;      // the world's patches; no patch exists outside them
    int patch_count_z = 64;
};

class StreamingGrid {
public:
    explicit StreamingGrid(const StreamingGridSettings& settings);

    const StreamingGridSettings& GetSettings() const { return settings_; }
    // The patch a world position lies in (may be outside the world: check Contains).
    PatchCoord GetPatchAt(const glm::dvec3& position) const;
    bool Contains(PatchCoord patch) const;
    // World x and z of a patch's corner with the smallest x and z.
    glm::dvec2 GetCorner(PatchCoord patch) const;
    // The patches of the world within `radius` patches of the one holding `position` (a square
    // of (2 radius + 1)^2), nearest ring first; within a ring row by row.
    std::vector<PatchCoord> GetWindow(const glm::dvec3& position, int radius) const;
    // The ring a patch lies on around another: 0 = the same patch, 1 = its eight neighbors...
    static int GetRing(PatchCoord center, PatchCoord patch);

private:
    StreamingGridSettings settings_;
};

// What a source loaded for one patch. Each source defines its own.
struct PatchData {
    virtual ~PatchData() = default;
};

// One kind of data loaded by patches.
class PatchSource {
public:
    virtual ~PatchSource() = default;
    // Reads or computes a patch. Runs on the loading thread for Background sources, on the
    // caller's thread for Immediate ones; must only read what the main thread does not change.
    // May return nullptr (nothing there).
    virtual std::unique_ptr<PatchData> Load(PatchCoord patch) = 0;
    // On the main thread: makes the loaded patch live (uploads images, adds plants or bodies).
    virtual void Attach(PatchCoord patch, std::unique_ptr<PatchData> data) = 0;
    // On the main thread: lets the patch go.
    virtual void Detach(PatchCoord patch) = 0;
};

enum class StreamingMode {
    Background,  // loaded on the loading thread, handed over in Update; for what is drawn
    Immediate,   // loaded at once in RequirePatches; for what the simulation stands on
};

struct StreamingStats {
    int loads_started = 0;     // since the streamer was made
    int attached = 0;
    int detached = 0;
    int discarded = 0;         // loads that finished after their patch had left the window
    int pending = 0;           // loads waiting, running or finished but not handed over
};

class WorldStreamer {
public:
    // The grid must outlive the streamer. The streamer starts one loading thread.
    explicit WorldStreamer(const StreamingGrid& grid);
    // Stops the loading thread (dropping loads not handed over); does not call Detach.
    ~WorldStreamer();
    WorldStreamer(const WorldStreamer&) = delete;
    WorldStreamer& operator=(const WorldStreamer&) = delete;

    // Adds a source with its window: `radius` patches around the observer (Background) or
    // around each required position (Immediate). Returns its number. The source must
    // outlive the streamer, or be detached first (DetachAll).
    int AddSource(PatchSource& source, int radius, StreamingMode mode);

    // Once per frame, at a fixed place (before drawing): Background windows follow
    // `observer`; patches that left are detached and loads for them dropped, missing ones are
    // queued nearest ring first, and up to `max_attach_per_frame` finished loads are attached.
    void Update(const glm::dvec3& observer);
    // Before a tick: every Immediate source gets each patch within its radius of any of
    // `positions`, loaded and attached now on this thread (in a fixed order), and lets go of
    // patches no position needs. Depends on the positions only.
    void RequirePatches(const std::vector<glm::dvec3>& positions);

    // Synchronous loading for Background sources too: Update loads and attaches every
    // missing patch at once. For headless runs whose pictures must not depend on timing.
    void SetSynchronous(bool synchronous);
    // Waits for every queued load and attaches it (those still in their window).
    void FinishLoading();
    void SetMaxAttachPerFrame(int count);
    // For tests: each background load waits this long first, as a slow disk would.
    void SetLoadDelayMilliseconds(int milliseconds);
    // Detaches every patch of every source.
    void DetachAll();

    bool IsAttached(int source, PatchCoord patch) const;
    // A source's attached patches, sorted row by row.
    std::vector<PatchCoord> GetAttached(int source) const;
    // The order patches of a source were attached in, since the streamer was made (tests).
    const std::vector<PatchCoord>& GetAttachOrder(int source) const;
    StreamingStats GetStats() const;
    const StreamingGrid& GetGrid() const { return grid_; }

private:
    struct Impl;
    const StreamingGrid& grid_;
    std::unique_ptr<Impl> impl_;
};

} // namespace poligon
