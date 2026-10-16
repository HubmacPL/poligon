// Example: a skinned mesh, a skeleton, an animation clip and a player.
//
// A flat bar with a joint in its middle bends back and forth. Everything is made in code;
// for a model from a file, LoadModelFromFile fills the same Skeleton and AnimationClip
// structures (Model::skeleton, Model::animations) and DrawModel takes the joint matrices.
// Usage: example_animation OUTPUT.png
#include <cstdio>
#include <vector>

#include "poligon/animation/animation.h"
#include "poligon/render/renderer.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: example_animation OUTPUT.png\n");
        return 2;
    }
    const glm::mat4 identity(1.0f);

    // --- The skeleton: a root joint and, 2 m along x, the joint the bar bends at. ---
    poligon::Skeleton skeleton;
    skeleton.joints.resize(2);
    skeleton.joints[0].name = "root";
    skeleton.joints[1].name = "bend";
    skeleton.joints[1].parent = 0;
    skeleton.joints[1].rest.translation = {2.0f, 0.0f, 0.0f};
    // Takes a vertex of the bar into the joint's own space: the joint sits at x = 2.
    skeleton.joints[1].inverse_bind = glm::translate(identity, {-2.0f, 0.0f, 0.0f});

    // --- The mesh: a strip from x = 0 to x = 4. Each vertex says which joints it follows;
    // around the middle it follows both, so the bend is smooth. ---
    std::vector<poligon::MeshVertex> vertices;
    std::vector<poligon::SkinVertex> skin;
    std::vector<std::uint32_t> indices;
    const int pieces = 16;
    for (int i = 0; i <= pieces; ++i) {
        const float x = 4.0f * static_cast<float>(i) / pieces;
        for (const float z : {0.3f, -0.3f}) {
            vertices.push_back({{x, 0.0f, z}, {0.0f, 1.0f, 0.0f}, {x / 4.0f, z + 0.5f}});
            poligon::SkinVertex follows;
            follows.joints[0] = 0;
            follows.joints[1] = 1;
            follows.weights[1] = glm::clamp((x - 1.5f) / 1.0f, 0.0f, 1.0f);  // 0 before 1.5 m, 1 after 2.5 m
            follows.weights[0] = 1.0f - follows.weights[1];
            skin.push_back(follows);
        }
    }
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(pieces); ++i) {
        const std::uint32_t corner = i * 2;
        indices.insert(indices.end(), {corner, corner + 2, corner + 1, corner + 1, corner + 2, corner + 3});
    }

    // --- The clip: the bend joint tips up by 70 degrees and comes back, in two seconds. ---
    poligon::AnimationClip clip;
    clip.name = "wave";
    clip.duration_seconds = 2.0f;
    clip.channels.resize(1);
    clip.channels[0].joint = skeleton.FindJoint("bend");
    const glm::vec3 across{0.0f, 0.0f, 1.0f};
    clip.channels[0].rotation_times = {0.0f, 1.0f, 2.0f};
    clip.channels[0].rotations = {glm::angleAxis(0.0f, across), glm::angleAxis(glm::radians(70.0f), across),
                                  glm::angleAxis(0.0f, across)};

    // --- Playing: the set is built once, a player keeps the time for one character. ---
    const poligon::AnimationSet animations(skeleton, {clip});
    poligon::AnimationPlayer player(animations);
    player.Play(animations.FindClip("wave"));
    // A game calls Update once per tick. Here: 45 ticks of 1/60 s, which is 0.75 s into the clip.
    for (int tick = 0; tick < 45; ++tick) {
        player.Update(1.0f / 60.0f);
    }
    std::printf("clip \"%s\", %.2f s long, now at %.2f s\n", animations.GetClipName(player.GetClip()),
                static_cast<double>(animations.GetClipDuration(player.GetClip())), static_cast<double>(player.GetTime()));

    // --- Drawing ---
    poligon::RendererConfig config;
    config.width = 640;
    config.height = 360;
    config.headless = true;
    config.post.exposure = 0.45f;
    poligon::Renderer renderer(config);
    const poligon::MeshHandle bar = renderer.CreateSkinnedMesh(vertices.data(), skin.data(), static_cast<int>(vertices.size()),
                                                               indices.data(), static_cast<int>(indices.size()));
    const poligon::MeshHandle plane = renderer.CreatePlaneMesh();

    poligon::Camera camera;
    camera.position = {2.0f, 2.5f, 6.0f};
    camera.target = {2.0f, 0.8f, 0.0f};
    poligon::DirectionalLight sun;
    sun.color = {2.6f, 2.45f, 2.2f, 1.0f};
    poligon::Environment environment;
    environment.draw_sky = true;
    environment.sky_light = true;

    poligon::Material bar_material;
    bar_material.color = {0.8f, 0.3f, 0.2f, 1.0f};
    bar_material.double_sided = true;

    renderer.BeginFrame(camera, sun, environment);
    renderer.DrawMesh(plane, glm::scale(identity, {30.0f, 1.0f, 30.0f}), poligon::Color{0.4f, 0.5f, 0.3f, 1.0f});
    // The joint matrices for this moment bend the mesh; the transform lifts the whole bar.
    const std::vector<glm::mat4>& joint_matrices = player.GetJointMatrices();
    renderer.DrawSkinnedMesh(bar, glm::translate(identity, {0.0f, 0.5f, 0.0f}), bar_material, joint_matrices.data(),
                             static_cast<int>(joint_matrices.size()));
    renderer.EndFrame();

    if (!renderer.SaveFrameToPng(argv[1])) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    std::printf("saved %s\n", argv[1]);
    return 0;
}
