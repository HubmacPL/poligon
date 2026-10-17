# Skeletal animation

How a model bends: a skeleton of joints, a mesh whose vertices follow them, and clips that say how the joints move over time.

Headers: `poligon/animation/animation.h`, `poligon/animation/animation_build.h` (tool side), `poligon/assets/model.h`, `poligon/render/renderer.h`. Example: `examples/animation/animation`. Test: `tests/animation/skinning_render_test.cpp`.

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

## Playing clips

```cpp
const poligon::AnimationSet& animations = *loaded_model->animations;  // a cooked model; or BuildAnimationSet
poligon::AnimationPlayer player(animations);                          // one per character
player.Play(animations.FindClip("Walk"));
player.Update(tick.delta_seconds);                                    // every tick
poligon::DrawModel(renderer, render_model, transform, player.GetJointMatrices());   // every frame
```

- `AnimationSet` prepares a skeleton and its clips for playing; build it once per model and share it. `AnimationPlayer` keeps the time for one character. The set must outlive its players.
- A cooked model (poligon/assets/assets.h) brings its set ready: `LoadedModel::animations`, loaded from the ozz-animation archive the cooker stored, with no building. `AnimationSet::SaveArchive()` gives that archive (ozz's own format for built skeletons and clips) and `AnimationSet::LoadArchive(skeleton, bytes, error)` reads it back for the skeleton it was made from; a set read back plays exactly like the one saved (`tests/animation/animation_archive_test.cpp`). Building from clips, `BuildAnimationSet(skeleton, clips)` in `poligon/animation/animation_build.h`, is for models read straight from files (the cooker, the viewer, examples). It lives in its own library, `poligon_animation_build`, with ozz's offline part; a game that loads cooked models links only `poligon_animation` (the toy game does, see building.md).
- `Play(clip, fade_seconds, loop)` starts a clip from its beginning. What was playing fades out over `fade_seconds` while the new clip fades in, so a walk turns into a run without a jerk. A looping clip starts over at its end; one that does not loop stops on its last moment (`IsFinished()`).
- `SetSpeed(2.0f)` plays twice as fast; `GetClip()`, `GetTime()` tell where the player is.
- Time moves only in `Update`, by the amount given. With the fixed tick this makes playback repeatable: the same steps give bit-identical joint matrices (tested).
- The work is done by the ozz-animation library (MIT); nothing of it shows outside `animation.cpp`. Clips are stored compressed, so a pose comes back with an error of a fraction of a millimeter per meter. Between two keys rotations are mixed along a straight line and normalized, which is exact enough for keys as close together as animation files have them.
- A clip that does not name a joint, or one of its three parts, leaves that part in its rest pose.
- Viewer: `model_viewer FILE TURN TILT DISTANCE CLIP` plays the named clip; C switches to the next one. With `--headless --ticks N` the picture is taken N sixtieths of a second into the clip.
- `forest_scene` has four foxes in the clearing (two moving around it, one standing, one howling). It uses the realistic fox in `poligon-assets/source/sketchfab/fox_realistic_demo` (FBX, 103 joints) when it is there, otherwise the Khronos sample in `poligon-assets/source/khronos/fox`.

## From pose to picture

Without a player, a pose can be set by hand:

```cpp
std::vector<poligon::JointTransform> pose = poligon::GetRestPose(model.skeleton);
pose[joint].rotation = ...;                       // or, later, a pose sampled from a clip
const std::vector<glm::mat4> matrices = poligon::ComputeJointMatrices(model.skeleton, pose);
poligon::DrawModel(renderer, render_model, transform, matrices);
```

- `DrawModel(renderer, render_model, transform)` without matrices draws a skinned model in its rest pose.
- A last argument `object_id` (any nonzero number that stays the same for the animal) lets temporal anti-aliasing follow the model's movement and pose (motion vectors, see [rendering.md](rendering.md)).
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
- A fade mixes two clips; switching clips faster than they fade drops the oldest one with a small jump.
- Not yet used from ozz: mixing by body part (a walk plus a turn of the head), additive clips, placing feet on uneven ground. The player's state is not part of the ECS world or the state dump yet; that comes with the first game object that owns one.
- Animals in `forest_scene` slide slightly: their speed over the ground is set by hand, not taken from the clip.
