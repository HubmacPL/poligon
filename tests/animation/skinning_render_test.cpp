// Checks skinned meshes: the pose math of poligon/animation/animation.h, and that the renderer bends
// a mesh by joint matrices, by looking at single pixels.
// Usage: skinning_render_test d3d12|vulkan
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "poligon/animation/animation.h"
#include "poligon/animation/animation_build.h"
#include "poligon/render/renderer.h"
#include "test_support.h"

namespace {

constexpr int kSize = 256;
constexpr float kCameraHeight = 10.0f;

// Converts a point on the ground (x or z) to the pixel it is drawn at by the top-down camera.
int ToPixel(float ground_coordinate) {
    const float visible_half_extent = kCameraHeight * 0.57735027f;  // height * tan(30 degrees)
    return static_cast<int>(kSize * 0.5f + ground_coordinate / visible_half_extent * kSize * 0.5f);
}

int RedAt(const poligon::Image& image, float x, float z) {
    return image.pixels[static_cast<size_t>(ToPixel(z) * image.width + ToPixel(x)) * 4];
}

bool Near(const glm::mat4& a, const glm::mat4& b) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (std::fabs(a[column][row] - b[column][row]) > 1.0e-4f) {
                return false;
            }
        }
    }
    return true;
}

// A flat bar on the ground from x = 0 to x = 4, half a meter wide, cut into eight pieces
// along its length. The half up to x = 2 follows joint 0, the half beyond it joint 1; the
// vertices exactly at x = 2 follow both equally.
void MakeBar(std::vector<poligon::MeshVertex>& vertices, std::vector<poligon::SkinVertex>& skin,
             std::vector<std::uint32_t>& indices) {
    const int pieces = 8;
    for (int i = 0; i <= pieces; ++i) {
        const float x = 4.0f * static_cast<float>(i) / pieces;
        for (const float z : {0.25f, -0.25f}) {
            poligon::MeshVertex vertex;
            vertex.position = {x, 0.0f, z};
            vertex.normal = {0.0f, 1.0f, 0.0f};
            vertex.uv = {x / 4.0f, z + 0.5f};
            vertices.push_back(vertex);

            poligon::SkinVertex follows;
            follows.joints[0] = 0;
            follows.joints[1] = 1;
            follows.weights[0] = x < 2.0f ? 1.0f : x > 2.0f ? 0.0f : 0.5f;
            follows.weights[1] = 1.0f - follows.weights[0];
            skin.push_back(follows);
        }
    }
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(pieces); ++i) {
        const std::uint32_t corner = i * 2;
        indices.insert(indices.end(), {corner, corner + 2, corner + 1, corner + 1, corner + 2, corner + 3});
    }
}

} // namespace

int main(int argc, char** argv) {
    using test::Expect;

    poligon::GraphicsBackend backend = poligon::GraphicsBackend::Default;
    if (argc == 2 && std::strcmp(argv[1], "d3d12") == 0) {
        backend = poligon::GraphicsBackend::Direct3D12;
    } else if (argc == 2 && std::strcmp(argv[1], "vulkan") == 0) {
        backend = poligon::GraphicsBackend::Vulkan;
    } else {
        std::printf("usage: skinning_render_test d3d12|vulkan\n");
        return 2;
    }

    const glm::mat4 identity(1.0f);

    // --- Pose math ---
    // Two joints: the second sits 2 m along x from the first, which is where the bar bends.
    poligon::Skeleton skeleton;
    skeleton.joints.resize(2);
    skeleton.joints[0].name = "root";
    skeleton.joints[1].name = "bend";
    skeleton.joints[1].parent = 0;
    skeleton.joints[1].rest.translation = {2.0f, 0.0f, 0.0f};
    skeleton.joints[1].inverse_bind = glm::translate(identity, {-2.0f, 0.0f, 0.0f});
    Expect(skeleton.FindJoint("bend") == 1 && skeleton.FindJoint("none") == -1, "joints are found by name");

    std::vector<poligon::JointTransform> pose = poligon::GetRestPose(skeleton);
    std::vector<glm::mat4> matrices = poligon::ComputeJointMatrices(skeleton, pose);
    Expect(matrices.size() == 2 && Near(matrices[0], identity) && Near(matrices[1], identity),
           "in the rest pose every joint matrix leaves vertices where they are");

    // Turn the second joint a quarter turn around the vertical axis: +x becomes -z.
    pose[1].rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    matrices = poligon::ComputeJointMatrices(skeleton, pose);
    const glm::vec3 tip = glm::vec3(matrices[1] * glm::vec4(4.0f, 0.0f, 0.0f, 1.0f));
    Expect(std::fabs(tip.x - 2.0f) < 1.0e-4f && std::fabs(tip.z + 2.0f) < 1.0e-4f,
           "a turned joint swings the far end of the bar around the joint");

    // A parent's movement carries its children along.
    pose[0].translation = {0.0f, 0.0f, 1.0f};
    const std::vector<glm::mat4> moved = poligon::ComputeJointMatrices(skeleton, pose);
    const glm::vec3 moved_tip = glm::vec3(moved[1] * glm::vec4(4.0f, 0.0f, 0.0f, 1.0f));
    Expect(std::fabs(moved_tip.x - 2.0f) < 1.0e-4f && std::fabs(moved_tip.z + 1.0f) < 1.0e-4f,
           "moving the parent joint moves the child with it");

    // --- Playing clips ---
    {
        const glm::quat no_turn(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::quat quarter_turn = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        // Where the far end of the bar is for the given joint matrices.
        const auto tip_of = [](const std::vector<glm::mat4>& joint_matrices) {
            return glm::vec3(joint_matrices[1] * glm::vec4(4.0f, 0.0f, 0.0f, 1.0f));
        };
        // The far end when the bend joint is turned by `degrees`.
        const auto tip_at = [](float degrees) {
            const float angle = glm::radians(degrees);
            return glm::vec3(2.0f + 2.0f * std::cos(angle), 0.0f, -2.0f * std::sin(angle));
        };
        // Clips are stored compressed, so positions come back with a small error.
        const auto close = [](glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 0.02f; };

        // "bend": the joint turns from straight to a quarter turn within one second.
        // "straight" and "bent": the joint holds still. All three name only the bend joint.
        std::vector<poligon::AnimationClip> clips(3);
        clips[0].name = "bend";
        clips[0].duration_seconds = 1.0f;
        clips[0].channels.resize(1);
        clips[0].channels[0].joint = 1;
        clips[0].channels[0].rotation_times = {0.0f, 1.0f};
        clips[0].channels[0].rotations = {no_turn, quarter_turn};
        clips[1].name = "straight";
        clips[1].duration_seconds = 1.0f;
        clips[1].channels.resize(1);
        clips[1].channels[0].joint = 1;
        clips[1].channels[0].rotation_times = {0.0f};
        clips[1].channels[0].rotations = {no_turn};
        clips[2] = clips[1];
        clips[2].name = "bent";
        clips[2].channels[0].rotations = {quarter_turn};

        const std::unique_ptr<poligon::AnimationSet> built_set = poligon::BuildAnimationSet(skeleton, clips);
        const poligon::AnimationSet& set = *built_set;
        Expect(set.GetJointCount() == 2 && set.GetClipCount() == 3, "the set holds the skeleton and the clips");
        Expect(set.FindClip("bent") == 2 && set.FindClip("none") == -1, "clips are found by name");
        Expect(std::strcmp(set.GetClipName(0), "bend") == 0 && std::fabs(set.GetClipDuration(0) - 1.0f) < 1.0e-5f,
               "clip name and duration are kept");

        poligon::AnimationPlayer player(set);
        Expect(player.GetClip() == -1 && close(tip_of(player.GetJointMatrices()), tip_at(0.0f)),
               "before the first Play the player gives the rest pose");

        player.Play(0);
        Expect(close(tip_of(player.GetJointMatrices()), tip_at(0.0f)), "a clip starts at its beginning");
        player.Update(0.5f);
        Expect(close(tip_of(player.GetJointMatrices()), tip_at(45.0f)), "half-way through, the joint is half-way turned");
        // The clip names no translation for the joint; it must stay 2 m along the bar,
        // not fall back to the origin.
        const glm::vec3 joint_place = glm::vec3(player.GetJointMatrices()[1] * glm::vec4(2.0f, 0.0f, 0.0f, 1.0f));
        Expect(close(joint_place, {2.0f, 0.0f, 0.0f}), "what a clip does not move stays in its rest pose");

        player.Update(0.75f);
        Expect(std::fabs(player.GetTime() - 0.25f) < 1.0e-5f, "a looping clip starts over at its end");
        // Between two keys ozz mixes rotations along a straight line and then normalizes
        // (cheaper than following the arc). With keys a quarter turn apart, a quarter of the
        // way is 21.6 degrees and not 22.5; real clips have keys far closer together.
        Expect(close(tip_of(player.GetJointMatrices()), tip_at(21.6f)), "and shows the pose of that moment");

        player.Play(0, 0.0f, false);
        player.Update(0.5f);
        Expect(!player.IsFinished(), "a clip that does not loop is not finished half-way");
        player.Update(5.0f);
        Expect(player.IsFinished() && close(tip_of(player.GetJointMatrices()), tip_at(90.0f)),
               "a clip that does not loop stops on its last moment");

        player.Play(0);
        player.SetSpeed(2.0f);
        player.Update(0.25f);
        Expect(close(tip_of(player.GetJointMatrices()), tip_at(45.0f)), "at double speed half the time is enough");

        // Fading: from "straight" into "bent" over one second.
        player.Play(1, 0.0f);
        player.Update(0.1f);
        player.Play(2, 1.0f);
        Expect(close(tip_of(player.GetJointMatrices()), tip_at(0.0f)), "a fade starts from the old clip");
        player.Update(0.5f);
        Expect(close(tip_of(player.GetJointMatrices()), tip_at(45.0f)), "half-way through a fade the two clips are mixed evenly");
        player.Update(0.6f);
        Expect(close(tip_of(player.GetJointMatrices()), tip_at(90.0f)), "after the fade only the new clip shows");

        // Two players fed the same steps give the same numbers, bit for bit.
        poligon::AnimationPlayer first(set);
        poligon::AnimationPlayer second(set);
        for (poligon::AnimationPlayer* each : {&first, &second}) {
            each->Play(0);
            for (int tick = 0; tick < 37; ++tick) {
                each->Update(1.0f / 60.0f);
            }
            each->Play(2, 0.4f);
            for (int tick = 0; tick < 11; ++tick) {
                each->Update(1.0f / 60.0f);
            }
        }
        Expect(std::memcmp(first.GetJointMatrices().data(), second.GetJointMatrices().data(), 2 * sizeof(glm::mat4)) == 0,
               "the same steps give bit-identical joint matrices");
    }

    // --- Rendering ---
    poligon::RendererConfig config;
    config.width = kSize;
    config.height = kSize;
    config.headless = true;
    config.enable_validation = true;
    config.backend = backend;
    config.post.tone_mapping = poligon::ToneMapping::None;
    poligon::Renderer renderer(config);

    std::vector<poligon::MeshVertex> vertices;
    std::vector<poligon::SkinVertex> skin;
    std::vector<std::uint32_t> indices;
    MakeBar(vertices, skin, indices);
    const poligon::MeshHandle bar = renderer.CreateSkinnedMesh(vertices.data(), skin.data(), static_cast<int>(vertices.size()),
                                                               indices.data(), static_cast<int>(indices.size()));
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    poligon::Camera camera;
    camera.position = {0.0f, kCameraHeight, 0.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, -1.0f};
    // Even light, sun off: the white bar is drawn as plain white on the black background.
    poligon::DirectionalLight light;
    light.color = {0.0f, 0.0f, 0.0f, 1.0f};
    light.ambient = {1.0f, 1.0f, 1.0f, 1.0f};
    poligon::Material white;
    white.roughness = 1.0f;
    poligon::Image frame;

    // Straight bar.
    const glm::mat4 straight[2] = {identity, identity};
    renderer.BeginFrame(camera, light);
    renderer.DrawSkinnedMesh(bar, identity, white, straight, 2);
    renderer.EndFrame();
    renderer.CaptureFrame(frame);
    Expect(RedAt(frame, 1.0f, 0.0f) == 255 && RedAt(frame, 3.5f, 0.0f) == 255, "the straight bar lies along +x");
    Expect(RedAt(frame, 2.0f, -1.5f) == 0, "nothing is drawn beside the straight bar");
    Expect(renderer.GetFrameStats().meshes_drawn == 1, "the skinned mesh is counted as drawn");

    // Bent bar: the far half swings from +x to -z, the near half stays.
    pose = poligon::GetRestPose(skeleton);
    pose[1].rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const std::vector<glm::mat4> bent = poligon::ComputeJointMatrices(skeleton, pose);
    renderer.BeginFrame(camera, light);
    renderer.DrawSkinnedMesh(bar, identity, white, bent.data(), 2);
    renderer.EndFrame();
    renderer.CaptureFrame(frame);
    Expect(RedAt(frame, 1.0f, 0.0f) == 255, "the half of the bar that follows the still joint stays");
    Expect(RedAt(frame, 3.5f, 0.0f) == 0, "the half that follows the turned joint has left its place");
    Expect(RedAt(frame, 2.0f, -1.5f) == 255, "and now points along -z");

    // Two skinned draws in one frame, each with its own pose, plus a rigid mesh.
    renderer.BeginFrame(camera, light);
    renderer.DrawMesh(plane, glm::translate(identity, {-3.0f, 0.0f, 3.0f}), white);
    renderer.DrawSkinnedMesh(bar, identity, white, straight, 2);
    renderer.DrawSkinnedMesh(bar, glm::translate(identity, {-4.0f, 0.0f, 0.0f}), white, bent.data(), 2);
    renderer.EndFrame();
    renderer.CaptureFrame(frame);
    Expect(RedAt(frame, 3.5f, 0.0f) == 255, "the first of two skinned meshes keeps its own (straight) pose");
    Expect(RedAt(frame, -2.0f, -1.5f) == 255 && RedAt(frame, -0.5f, 0.0f) == 0,
           "the second keeps its own (bent) pose");
    Expect(RedAt(frame, -3.0f, 3.0f) == 255, "a rigid mesh in the same frame is drawn as usual");

    // Culling: a skinned mesh placed far outside the view is not drawn; the floor is.
    poligon::DirectionalLight sun;
    sun.direction = {0.0f, -1.0f, 0.0f};
    sun.color = {0.5f, 0.5f, 0.5f, 1.0f};
    sun.ambient = {0.2f, 0.2f, 0.2f, 1.0f};
    poligon::Environment environment;
    environment.shadow_distance = 30.0f;
    poligon::Material black;
    black.color = {0.0f, 0.0f, 0.0f, 1.0f};
    black.roughness = 1.0f;
    renderer.BeginFrame(camera, sun, environment);
    renderer.DrawMesh(plane, glm::translate(identity, {0.0f, -3.0f, 0.0f}) * glm::scale(identity, {40.0f, 1.0f, 40.0f}), white);
    renderer.DrawSkinnedMesh(bar, glm::translate(identity, {50.0f, 0.0f, 0.0f}), black, straight, 2);
    renderer.EndFrame();
    Expect(renderer.GetFrameStats().meshes_drawn == 1, "a skinned mesh outside the view is culled");

    // Shadows follow the pose: a bent bar floats 3 m above a white floor, with the sun
    // leaning sideways so that the shadow falls beside the bar and not under it.
    sun.direction = {0.6f, -1.0f, 0.0f};
    renderer.BeginFrame(camera, sun, environment);
    renderer.DrawMesh(plane, glm::translate(identity, {0.0f, -3.0f, 0.0f}) * glm::scale(identity, {40.0f, 1.0f, 40.0f}), white);
    renderer.DrawSkinnedMesh(bar, identity, black, bent.data(), 2);
    renderer.EndFrame();
    renderer.CaptureFrame(frame);
    // The sun leans towards +x, so shadows fall 1.8 m further along x than the bar (3 m
    // above the floor). Seen from the camera at 10 m, floor points appear 10/13 closer to
    // the middle. The bent half spans x = 2, z = -2..0: its shadow is at x = 3.8.
    const float floor_scale = 10.0f / 13.0f;
    const int shadow = RedAt(frame, 3.8f * floor_scale, -1.2f * floor_scale);
    const int lit = RedAt(frame, 5.3f * floor_scale, 0.0f);
    if (!(shadow < 140 && lit > 190)) {
        std::printf("  shadow pixel %d, lit pixel %d\n", shadow, lit);
    }
    Expect(shadow < 140, "the bent half casts its shadow where the pose puts it");
    Expect(lit > 190, "no shadow remains where the straight bar would have cast it");

    // Motion vectors follow the pose: the bar drawn straight, then bent, with an object id;
    // in the motion debug view the half that swung shows motion, the still half none.
    poligon::PostSettings post = renderer.GetPostSettings();
    post.temporal_aa = true;
    post.debug_view = poligon::DebugView::Motion;
    renderer.SetPostSettings(post);
    renderer.BeginFrame(camera, light);
    renderer.DrawSkinnedMesh(bar, identity, white, straight, 2, 5);
    renderer.EndFrame();
    renderer.BeginFrame(camera, light);
    renderer.DrawSkinnedMesh(bar, identity, white, bent.data(), 2, 5);
    renderer.EndFrame();
    renderer.CaptureFrame(frame);
    const int still_red = RedAt(frame, 1.0f, 0.0f);
    const int swung_red = RedAt(frame, 2.0f, -1.5f);
    const int swung_green = frame.pixels[static_cast<size_t>(ToPixel(-1.5f) * frame.width + ToPixel(2.0f)) * 4 + 1];
    if (!(std::abs(still_red - 188) <= 4 && (std::abs(swung_red - 188) > 30 || std::abs(swung_green - 188) > 30))) {
        std::printf("  motion: still half %d, swung half %d %d\n", still_red, swung_red, swung_green);
    }
    Expect(std::abs(still_red - 188) <= 4, "the half of the bar on the still joint has no motion");
    Expect(std::abs(swung_red - 188) > 30 || std::abs(swung_green - 188) > 30, "the half that swung has motion from the previous pose");

    std::printf("skinning_render_test finished\n");
    return test::ExitCode();
}
