# Reduces the triangle count of a model with Blender and writes it as glTF.
#
# Run with Blender, without its window:
#   blender -b --factory-startup -P tools/assets/simplify_model.py -- INPUT OUTPUT.gltf RATIO [foliage]
#
# INPUT    any model Blender imports here: .gltf, .glb, .fbx, .obj
# OUTPUT   path of the .gltf to write; its .bin and textures land next to it
# RATIO    part of the triangles to keep, for example 0.1 keeps about a tenth
# foliage  the word itself: treat leaves specially (see below); for levels of detail of trees
#
# Every mesh is reduced by the same ratio with Blender's "collapse" decimation, which keeps
# texture coordinates. Materials and textures are passed through unchanged.
#
# With "foliage", parts whose material is cut out by transparency (leaves: many small flat
# cards with a picture of a twig) are not collapsed, which would thin the crown until the
# tree looks bare. Instead a share of the cards is removed, always the same ones for the
# same model, and the remaining cards are enlarged so that the crown stays as full as it was.
import math
import random
import sys

import bmesh
import bpy


def fail(message):
    print("simplify_model: " + message)
    sys.exit(1)


arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if len(arguments) not in (3, 4) or (len(arguments) == 4 and arguments[3] != "foliage"):
    fail("usage: blender -b --factory-startup -P simplify_model.py -- INPUT OUTPUT.gltf RATIO [foliage]")
input_path, output_path, ratio_text = arguments[:3]
foliage_mode = len(arguments) == 4
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


def is_cutout(material):
    """True for materials whose transparency comes from a texture: leaves, twigs, fur."""
    if material is None or not material.use_nodes:
        return False
    for node in material.node_tree.nodes:
        if node.type == "BSDF_PRINCIPLED" and node.inputs["Alpha"].is_linked:
            return True
    return False


def thin_cards(scene_object, keep):
    """Removes a share of the object's separate pieces (cards) and enlarges the rest."""
    mesh = bmesh.new()
    mesh.from_mesh(scene_object.data)
    mesh.faces.ensure_lookup_table()

    # Group faces into pieces that hang together by shared vertices.
    pieces = []
    seen = set()
    for start in mesh.faces:
        if start.index in seen:
            continue
        piece = []
        stack = [start]
        seen.add(start.index)
        while stack:
            face = stack.pop()
            piece.append(face)
            for vertex in face.verts:
                for neighbor in vertex.link_faces:
                    if neighbor.index not in seen:
                        seen.add(neighbor.index)
                        stack.append(neighbor)
        pieces.append(piece)

    # The same model always loses the same cards: the choice depends only on the order of
    # the pieces, which the file fixes.
    chooser = random.Random(12345)
    order = list(range(len(pieces)))
    chooser.shuffle(order)
    kept_count = max(1, int(round(len(pieces) * keep)))
    kept = set(order[:kept_count])

    # Enlarging each card by the square root of the lost share keeps the covered area.
    growth = math.sqrt(len(pieces) / kept_count)
    doomed = []
    for index, piece in enumerate(pieces):
        if index not in kept:
            doomed.extend(piece)
            continue
        vertices = {vertex for face in piece for vertex in face.verts}
        center = sum((vertex.co for vertex in vertices), type(start.verts[0].co)()) / len(vertices)
        for vertex in vertices:
            vertex.co = center + (vertex.co - center) * growth
    bmesh.ops.delete(mesh, geom=doomed, context="FACES")
    mesh.to_mesh(scene_object.data)
    mesh.free()
    return len(pieces), kept_count


triangles_before = count_triangles()

for scene_object in list(bpy.context.scene.objects):
    if scene_object.type != "MESH":
        continue
    bpy.context.view_layer.objects.active = scene_object
    materials = [slot.material for slot in scene_object.material_slots]
    if foliage_mode and materials and all(is_cutout(material) for material in materials):
        total, kept = thin_cards(scene_object, ratio)
        print("simplify_model: %s: kept %d of %d cards" % (scene_object.name, kept, total))
        continue
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
