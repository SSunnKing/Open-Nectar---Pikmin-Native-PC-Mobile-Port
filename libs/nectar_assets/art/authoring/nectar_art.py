"""Helpers for the seed scripts that built the first version of each .blend.

The .blend files are the source of truth; these scripts only record how each
one was first built (so its provenance is plain: shapes made from Blender
primitives, nothing imported). Re-running a seed script overwrites the .blend.

Conventions the exporter (art/export_standins.py) reads:
- a collection per model, with custom properties nectar_model (name),
  nectar_joints (the .mod's joint count) and optionally nectar_ground (bool);
- nectar_joint on each object (default 0);
- material viewport colour as the base colour, nectar_emit and nectar_gloss.
Blender is Z up with the front facing -Y; the game is Y up with the front +z.
"""

import math
import os

import bpy
import mathutils

ART_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def hex_colour(h):
    h = h.lstrip("#")
    srgb = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    return tuple(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in srgb)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = 'NONE'
    return scene


_materials = {}


def material(name, colour, emit=0.0, gloss=0.15):
    mat = bpy.data.materials.get(name)
    if mat is None:
        mat = bpy.data.materials.new(name)
    lin = hex_colour(colour) if isinstance(colour, str) else colour
    mat.diffuse_color = (lin[0], lin[1], lin[2], 1.0)
    mat["nectar_emit"] = float(emit)
    mat["nectar_gloss"] = float(gloss)
    mat.use_nodes = False
    return mat


def model_collection(name, joints, ground=False):
    coll = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(coll)
    coll["nectar_model"] = name
    coll["nectar_joints"] = int(joints)
    coll["nectar_ground"] = bool(ground)
    return coll


def _apply_scale(ob, scale):
    """Bakes a scale into the mesh so bevel widths stay in model units."""
    ob.data.transform(mathutils.Matrix.Diagonal((scale[0], scale[1], scale[2], 1.0)))
    ob.scale = (1.0, 1.0, 1.0)


def _finish(ob, coll, mat, joint, name):
    for c in list(ob.users_collection):
        c.objects.unlink(ob)
    coll.objects.link(ob)
    ob.name = name
    ob["nectar_joint"] = int(joint)
    ob.data.materials.clear()
    if mat is not None:
        ob.data.materials.append(mat)
    return ob


def smooth(ob, angle_deg=40.0):
    """Shade smooth with sharp edges above angle_deg (the Smooth by Angle modifier)."""
    with bpy.context.temp_override(active_object=ob, object=ob, selected_objects=[ob], selected_editable_objects=[ob]):
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(angle_deg), keep_sharp_edges=True)
    return ob


def bevel(ob, width, segments=3, limit_angle=30.0):
    m = ob.modifiers.new("Bevel", 'BEVEL')
    m.width = width
    m.segments = segments
    m.limit_method = 'ANGLE'
    m.angle_limit = math.radians(limit_angle)
    m.harden_normals = False
    return m


def subsurf(ob, levels=1):
    m = ob.modifiers.new("Subdivision", 'SUBSURF')
    m.levels = levels
    m.render_levels = levels
    return m


def cylinder(coll, name, mat, radius, depth, loc, rot=(0, 0, 0), verts=24, joint=0):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=radius, depth=depth, location=loc, rotation=rot)
    return _finish(bpy.context.active_object, coll, mat, joint, name)


def sphere(coll, name, mat, radius, loc, scale=(1, 1, 1), segments=16, rings=8, joint=0, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments, ring_count=rings, radius=radius, location=loc, rotation=rot)
    ob = bpy.context.active_object
    _apply_scale(ob, scale)
    return _finish(ob, coll, mat, joint, name)


def box(coll, name, mat, size, loc, rot=(0, 0, 0), joint=0):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=loc, rotation=rot)
    ob = bpy.context.active_object
    _apply_scale(ob, size)
    return _finish(ob, coll, mat, joint, name)


def cone(coll, name, mat, r1, r2, depth, loc, rot=(0, 0, 0), verts=8, joint=0):
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r1, radius2=r2, depth=depth, location=loc, rotation=rot)
    return _finish(bpy.context.active_object, coll, mat, joint, name)


def torus(coll, name, mat, major, minor, loc, rot=(0, 0, 0), major_segments=24, minor_segments=8, joint=0):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, major_segments=major_segments,
                                     minor_segments=minor_segments, location=loc, rotation=rot)
    return _finish(bpy.context.active_object, coll, mat, joint, name)


def save(name):
    path = os.path.join(ART_DIR, "blender", name + ".blend")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=path, compress=True)
    print(f"[seed] wrote {path}")
    return path
