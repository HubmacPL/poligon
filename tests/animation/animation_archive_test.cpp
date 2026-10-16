// Checks the ozz-animation archive of an AnimationSet (what the asset cooker stores): a set
// loaded back from it has the same clips and gives exactly the same joint matrices as the
// set it was saved from, at every moment and across a fade; an archive made for another
// skeleton, and bytes that are no archive, are refused with a message.
// Usage: animation_archive_test
#include <cstdio>
#include <string>
#include <vector>

#include "poligon/animation/animation.h"
#include "test_support.h"

namespace {

// A root, a joint 2 m along x and one more 1 m further.
poligon::Skeleton MakeSkeleton() {
    poligon::Skeleton skeleton;
    skeleton.joints.resize(3);
    skeleton.joints[0].name = "root";
    skeleton.joints[1].name = "elbow";
    skeleton.joints[1].parent = 0;
    skeleton.joints[1].rest.translation = {2.0f, 0.0f, 0.0f};
    skeleton.joints[1].inverse_bind = glm::translate(glm::mat4(1.0f), {-2.0f, 0.0f, 0.0f});
    skeleton.joints[2].name = "hand";
    skeleton.joints[2].parent = 1;
    skeleton.joints[2].rest.translation = {1.0f, 0.0f, 0.0f};
    skeleton.joints[2].inverse_bind = glm::translate(glm::mat4(1.0f), {-3.0f, 0.0f, 0.0f});
    return skeleton;
}

// "wave" bends the elbow up and back in 2 s; "reach" moves the root forward and grows the hand.
std::vector<poligon::AnimationClip> MakeClips() {
    const glm::vec3 across{0.0f, 0.0f, 1.0f};
    poligon::AnimationClip wave;
    wave.name = "wave";
    wave.duration_seconds = 2.0f;
    wave.channels.resize(1);
    wave.channels[0].joint = 1;
    wave.channels[0].rotation_times = {0.0f, 1.0f, 2.0f};
    wave.channels[0].rotations = {glm::angleAxis(0.0f, across), glm::angleAxis(glm::radians(70.0f), across),
                                  glm::angleAxis(0.0f, across)};
    poligon::AnimationClip reach;
    reach.name = "reach";
    reach.duration_seconds = 1.5f;
    reach.channels.resize(2);
    reach.channels[0].joint = 0;
    reach.channels[0].translation_times = {0.0f, 1.5f};
    reach.channels[0].translations = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    reach.channels[1].joint = 2;
    reach.channels[1].scale_times = {0.0f, 0.75f, 1.5f};
    reach.channels[1].scales = {{1.0f, 1.0f, 1.0f}, {1.5f, 1.5f, 1.5f}, {1.0f, 1.0f, 1.0f}};
    return {wave, reach};
}

bool SameMatrices(const std::vector<glm::mat4>& a, const std::vector<glm::mat4>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    using test::Expect;

    const poligon::Skeleton skeleton = MakeSkeleton();
    const poligon::AnimationSet built(skeleton, MakeClips());
    const std::vector<std::uint8_t> archive = built.SaveArchive();
    Expect(archive.size() > 100, "the archive holds the skeleton and the clips");
    Expect(built.SaveArchive() == archive, "the same set saves the same bytes");

    std::string error;
    const std::unique_ptr<poligon::AnimationSet> loaded = poligon::AnimationSet::LoadArchive(skeleton, archive, error);
    Expect(loaded != nullptr, "an archive loads back");
    if (loaded == nullptr) {
        std::printf("  %s\n", error.c_str());
        return test::ExitCode();
    }
    Expect(loaded->GetJointCount() == 3 && loaded->GetClipCount() == 2, "with every joint and clip");
    Expect(std::string(loaded->GetClipName(0)) == "wave" && loaded->FindClip("reach") == 1, "with the clips' names");
    Expect(loaded->GetClipDuration(0) == 2.0f && loaded->GetClipDuration(1) == 1.5f, "and their lengths");

    // Both sets played the same way: the same matrices at every tick, through a fade.
    poligon::AnimationPlayer from_clips(built);
    poligon::AnimationPlayer from_archive(*loaded);
    bool same = SameMatrices(from_clips.GetJointMatrices(), from_archive.GetJointMatrices());
    from_clips.Play(0);
    from_archive.Play(0);
    for (int tick = 0; tick < 240; ++tick) {
        if (tick == 90) {
            from_clips.Play(1, 0.4f);
            from_archive.Play(1, 0.4f);
        }
        from_clips.Update(1.0f / 60.0f);
        from_archive.Update(1.0f / 60.0f);
        same = same && SameMatrices(from_clips.GetJointMatrices(), from_archive.GetJointMatrices());
    }
    Expect(same, "a loaded set plays exactly like the set it was saved from");

    // Another skeleton (a joint renamed) and bytes that are no archive are refused.
    poligon::Skeleton other = skeleton;
    other.joints[2].name = "paw";
    error.clear();
    Expect(poligon::AnimationSet::LoadArchive(other, archive, error) == nullptr && !error.empty(),
           "an archive made for another skeleton is refused");
    error.clear();
    const std::vector<std::uint8_t> garbage(64, 7);
    Expect(poligon::AnimationSet::LoadArchive(skeleton, garbage, error) == nullptr && !error.empty(),
           "bytes that are no archive are refused");
    error.clear();
    Expect(poligon::AnimationSet::LoadArchive(skeleton, {}, error) == nullptr && !error.empty(), "an empty archive is refused");

    std::printf("animation_archive_test finished (%zu bytes)\n", archive.size());
    return test::ExitCode();
}
