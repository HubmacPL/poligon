# Reduces the triangle count of a model with Blender and writes it as glTF.
#
# Run with Blender, without its window:
#   blender -b --factory-startup -P tools/assets/simplify_model.py -- INPUT OUTPUT.gltf RATIO
#
# INPUT   any model Blender imports here: .gltf, .glb, .fbx, .obj
# OUTPUT  path of the .gltf to write; its .bin and textures land next to it
# RATIO   part of the triangles to keep, for example 0.1 keeps about a tenth
#
# Every mesh is reduced by the same ratio with Blender's "collapse" decimation, which keeps
# texture coordinates. Materials and textures are passed through unchanged.
import sys

import bpy


def fail(message):
    print("simplify_model: " + message)
    sys.exit(1)


arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if len(arguments) != 3:
    fail("usage: blender -b --factory-startup -P simplify_model.py -- INPUT OUTPUT.gltf RATIO")
input_path, output_path, ratio_text = arguments
ratio = float(ratio_text)
if not 0.0 < ratio <= 1.0:
    fail("RATIO must be above 0 and at most 1")

# Start from an empty scene.
bpy.ops.wm.read_factory_settings(use_empty=True)

lower = input_path.lower()
if lower.endswith((".gltf", ".glb")):
    bpy.ops.import_scene.gltf(filepath=input_path)
elif lower.endswith(".fbx"):
    bpy.ops.import_scene.fbx(filepath=input_path)
elif lower.endswith(".obj"):
    bpy.ops.wm.obj_import(filepath=input_path)
else:
    fail("unsupported input format: " + input_path)


def count_triangles():
    total = 0
    for scene_object in bpy.context.scene.objects:
        if scene_object.type == "MESH":
            scene_object.data.calc_loop_triangles()
            total += len(scene_object.data.loop_triangles)
    return total


triangles_before = count_triangles()

for scene_object in list(bpy.context.scene.objects):
    if scene_object.type != "MESH":
        continue
    bpy.context.view_layer.objects.active = scene_object
    modifier = scene_object.modifiers.new(name="Simplify", type="DECIMATE")
    modifier.decimate_type = "COLLAPSE"
    modifier.ratio = ratio
    bpy.ops.object.modifier_apply(modifier=modifier.name)

triangles_after = count_triangles()

bpy.ops.export_scene.gltf(
    filepath=output_path,
    export_format="GLTF_SEPARATE",
    export_texture_dir="textures",
    export_image_format="AUTO",
    export_apply=True,
)

print("simplify_model: %d -> %d triangles, wrote %s" % (triangles_before, triangles_after, output_path))
