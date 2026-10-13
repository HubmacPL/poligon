# Skeletal animation

How a model bends: a skeleton of joints, a mesh whose vertices follow them, and clips that say how the joints move over time.

Headers: `poligon/animation.h`, `poligon/model.h`, `poligon/renderer.h`. Test: `tests/skinning_render_test.cpp`.

**State: in progress (milestone 5).** Loading and drawing are done. Playing a clip and blending between clips are not written yet.

## Terms

- **Joint** (bone): a point of the skeleton with a place relative to its parent joint. Moving a joint moves all joints below it.
- **Skinning**: each vertex of the mesh follows up to four joints, each with a weight; the weights add up to 1. When the joints move, the vertex goes to the weighted mix of where each joint would carry it.
- **Rest pose**: where the joints are when no animation plays. **Bind pose**: where they were when the mesh was attached to them. A joint's `inverse_bind` matrix takes a vertex from the mesh into that joint's space in the bind pose.
- **Pose**: one `JointTransform` (translation, rotation, scale relative to the parent) per joint.
- **Joint matrices**: one matrix per joint that takes a vertex from where it was modeled to where the posed joint puts it. This is what the graphics card needs.

## Data

- `Skeleton` is a list of `Joint`s; a parent always comes before its children. `Skeleton::FindJoint(name)` finds one by name.
- `AnimationClip` has a name, a duration in seconds and one `AnimationChannel` per moving joint, each with separate lists of translation, rotation and scale keys (time in seconds, value).
- `LoadModelFromFile` fills `Model::skeleton` and `Model::animations` when the file has a skinned mesh. The joints are the file's bones plus every node above them in the file's tree. `ModelMesh::skin` holds one `SkinVertex` (four joint numbers, four weights) per vertex; it is empty for a rigid mesh.
- Limits: 256 joints per skeleton (a model with more is loaded without its skeleton, with a warning), four joints per vertex (the four strongest are kept). Animations of rigid parts (a door, a wheel) are not loaded.
- `model_viewer FILE --headless` prints the joint count and the clips with their durations.

## From pose to picture

```cpp
std::vector<poligon::JointTransform> pose = poligon::GetRestPose(model.skeleton);
pose[joint].rotation = ...;                       // or, later, a pose sampled from a clip
const std::vector<glm::mat4> matrices = poligon::ComputeJointMatrices(model.skeleton, pose);
poligon::DrawModel(renderer, render_model, transform, matrices);
```

- `DrawModel(renderer, render_model, transform)` without matrices draws a skinned model in its rest pose.
- Below `DrawModel`: `Renderer::CreateSkinnedMesh(vertices, skin, ...)` and `Renderer::DrawSkinnedMesh(mesh, transform, material, joint_matrices, joint_count)`. The matrices are copied at the call.
- A skinned mesh is placed by its joints and by `transform`; the node it hangs on in the file is ignored, as glTF prescribes.

## In the renderer

- A skinned mesh has a second vertex buffer with joints and weights and is drawn by `mesh_skinned.vert.hlsl`, in the scene, depth and shadow passes alike, so shadows, ambient occlusion and fog follow the pose. The fragment shaders are the same as for rigid meshes.
- The joint matrices of all skinned meshes of a frame go into one buffer on the graphics card, uploaded once in `EndFrame`; each draw only says where its matrices start.
- The shader reads that buffer as plain bytes (`ByteAddressBuffer`). With `StructuredBuffer`, Direct3D 12 through SDL GPU silently reads zeros and the mesh disappears, with no message from the debug layer.
- Culling uses a box that encloses the mesh in its current pose: the mesh's own box as moved by every joint.

## Known limits

- Normals of a skinned mesh are correct for joints that rotate and scale evenly; uneven scaling of a joint would need more math.
- The culling box is safe but generous for models whose joints lie far apart.
