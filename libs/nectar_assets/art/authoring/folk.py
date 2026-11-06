"""Seed script for art/blender/folk.blend: the captain, the pebble folk, their crests and the buried folk.

See ART_DIRECTION.md (Captain, Pebble folk, Maturity crests, Buried folk).

- captain: the lantern shepherd (pikis/nv3Model.mod, 12-joint rig). The lantern
  hangs from the leaf-tip joint (6); the right mitten is joint 7, on +x where
  the rig's 'rhnd' sphere is.
- folk_red, folk_yellow, folk_blue (and folk_kin for the Puffmin): one egg body,
  big white-rimmed eyes, nub arms and round feet, plus one trait per type. The
  body (joint 0), arms and feet share one white material: the bindings draw
  them with material 0, which the game tints with the type colour (and the idle
  pastel and white damage flash). The eyes and trait hang from joint 1 and keep
  their own colours.
- crest_bead, crest_shards, crest_star: the three maturity crests. The game
  draws them at the folk's joint 6, which the stand-in rig keeps at the model
  origin, so each one is authored sitting on the crown (y 5.55).
- buried_folk: the buried folk (objects/pikihead/pikihead.mod). The game draws
  the crest at its joint 3, also at the origin, so the body shows from the soil
  up to the same crown height. The body hangs from joint 0, which the binding
  draws with material 0: the game tints that material with the folk's colour.

Sizes follow the earlier stand-ins: the folk stand about 6.6 tall (feet at
y -1, crown at 5.55), the captain about 12.5 to the top of his crook.

    Blender --background --factory-startup --python libs/nectar_assets/art/authoring/folk.py
"""

import math
import os
import sys

import bmesh
import bpy
import mathutils

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nectar_art as na  # noqa: E402

# Blender space: x to the model's left (game x), -Y is the front, Z is up.
FOLK_C, FOLK_RX, FOLK_RY, FOLK_RZ, FOLK_TAPER = 2.45, 2.25, 2.05, 3.1, 0.12
CROWN = FOLK_C + FOLK_RZ  # 5.55

RIG_HEAD_FREE = 1  # parts that must not turn with the head look sit on the spine joint
BODY_JOINT = 0     # the root: also free of the head look, and drawn with the tinted material 0
R_HAND, L_HAND, R_LEG, L_LEG, LEAF_TIP = 7, 8, 10, 11, 6


# --- shape helpers --------------------------------------------------------------
def egg(coll, name, mat, rx, ry, rz, cz, taper, segments=14, rings=9, joint=0, floor=None, cx=0.0, cy=0.0):
    """A UV sphere made into an egg: wider below its centre, narrower above.
    floor (absolute z) flattens everything under it into a flat base."""
    ob = na.sphere(coll, name, mat, 1.0, (0.0, 0.0, 0.0), segments=segments, rings=rings, joint=joint)
    for v in ob.data.vertices:
        h = v.co.z
        f = 1.0 - taper * h
        v.co.x *= rx * f
        v.co.y *= ry * f
        v.co.z = cz + h * rz
        if floor is not None and v.co.z < floor:
            v.co.z = floor
        v.co.x += cx
        v.co.y += cy
    ob.data.update()
    return na.smooth(ob, 70.0)


def egg_radius(x, z, rx, ry, rz, cz, taper, side=-1.0):
    """The y of an egg's surface at (x, z), on the front (side -1) or back (+1)."""
    h = max(-0.999, min(0.999, (z - cz) / rz))
    f = (1.0 - taper * h) * math.sqrt(1.0 - h * h)
    a, b = rx * f, ry * f
    t = max(0.0, 1.0 - (x / a) ** 2)
    return side * b * math.sqrt(t)


def folk_surface(x, z, side=-1.0):
    return egg_radius(x, z, FOLK_RX, FOLK_RY, FOLK_RZ, FOLK_C, FOLK_TAPER, side)


def facing(ob, x, y):
    """Turns a part about Z so its local -Y looks out of the body at (x, y)."""
    ob.rotation_euler = (ob.rotation_euler[0], ob.rotation_euler[1], math.atan2(x, -y))
    return ob


def blob(coll, name, mat, r, loc, scale, segments=8, rings=5, joint=0, rot=(0, 0, 0)):
    ob = na.sphere(coll, name, mat, r, loc, scale=scale, segments=segments, rings=rings, joint=joint, rot=rot)
    return na.smooth(ob, 70.0)


def limb(coll, name, mat, p0, p1, r0, r1, verts=8, joint=0):
    """A tapered tube from p0 (radius r0) to p1 (radius r1)."""
    p0, p1 = mathutils.Vector(p0), mathutils.Vector(p1)
    d = p1 - p0
    ob = na.cone(coll, name, mat, r0, r1, d.length, (p0 + p1) * 0.5, verts=verts, joint=joint)
    ob.rotation_mode = 'QUATERNION'
    ob.rotation_quaternion = mathutils.Vector((0.0, 0.0, 1.0)).rotation_difference(d.normalized())
    return na.smooth(ob, 50.0)


def delete_verts(ob, keep):
    """Deletes the mesh vertices (local coordinates) for which keep(co) is false."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    gone = [v for v in bm.verts if not keep(v.co)]
    bmesh.ops.delete(bm, geom=gone, context='VERTS')
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.update()
    return ob


def cull_inside(ob, *containers, margin=0.03):
    """Deletes the faces of ob that lie wholly inside one of the closed meshes in
    containers (hidden anyway), to keep the triangle budget for what shows."""
    bpy.context.view_layer.update()
    to_world = ob.matrix_world.copy()
    tests = [(c, c.matrix_world.inverted()) for c in containers]

    def inside(p):
        for c, to_local in tests:
            q = to_local @ p
            ok, loc, nrm, _ = c.closest_point_on_mesh(q)
            if ok and (q - loc).dot(nrm) < -margin:
                return True
        return False

    bm = bmesh.new()
    bm.from_mesh(ob.data)
    flags = {v.index: inside(to_world @ v.co) for v in bm.verts}
    gone = [f for f in bm.faces if all(flags[v.index] for v in f.verts)]
    bmesh.ops.delete(bm, geom=gone, context='FACES_ONLY')
    loose = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=loose, context='VERTS')
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.update()
    return ob


def mesh_object(coll, name, mat, verts, faces, joint=0):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    me.update()
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    ob["nectar_joint"] = int(joint)
    me.materials.append(mat)
    return ob


# --- the pebble folk --------------------------------------------------------------
def folk_eyes(coll, white, dark, shine, joint, body, z=3.55, spread=0.8):
    for side, tag in ((1.0, "l"), (-1.0, "r")):
        x = side * spread
        y = folk_surface(x, z)
        e = facing(blob(coll, f"eye_{tag}", white, 0.74, (x * 1.02, y * 0.93, z), (1.0, 0.5, 1.12), segments=8, rings=4, joint=joint), x, y)
        p = facing(blob(coll, f"pupil_{tag}", dark, 0.46, (x * 0.98, y * 0.93 - 0.3, z - 0.06), (1.0, 0.45, 1.18), segments=6, rings=4, joint=joint), x, y)
        s = facing(blob(coll, f"shine_{tag}", shine, 0.13, (x * 0.98 + 0.14 * side, y * 0.93 - 0.48, z + 0.22), (1, 0.6, 1), segments=4, rings=3, joint=joint), x, y)
        cull_inside(e, body)
        cull_inside(p, e)
        cull_inside(s, p)


def folk(name, trait, mats):
    coll = na.model_collection(name, joints=12, ground=True)
    body = mats["folk_body"]
    shell = egg(coll, "body", body, FOLK_RX, FOLK_RY, FOLK_RZ, FOLK_C, FOLK_TAPER, segments=14, rings=8, joint=BODY_JOINT)
    folk_eyes(coll, mats["eye"], mats["pupil"], mats["shine"], RIG_HEAD_FREE, shell)
    # Nub arms on the sides (joint 7 on +x, like the captain's 'rhnd'), round feet under.
    for side, joint, tag in ((1.0, R_HAND, "r"), (-1.0, L_HAND, "l")):
        arm = blob(coll, f"arm_{tag}", body, 0.62, (side * 2.3, -0.15, 1.95), (0.8, 0.85, 1.15), segments=6, rings=4, joint=joint,
                   rot=(0.0, side * math.radians(-22.0), 0.0))
        cull_inside(arm, shell)
    for side, joint, tag in ((1.0, R_LEG, "r"), (-1.0, L_LEG, "l")):
        cull_inside(blob(coll, f"foot_{tag}", body, 0.8, (side * 0.95, -0.4, -0.58), (0.85, 1.15, 0.56), segments=6, rings=4, joint=joint), shell)
    trait(coll, shell)
    return coll


def trait_red(coll, shell):
    # Three rounded ember scales down the back (+Y), the middle one largest.
    ember = na.material("ember", "#F4782E", emit=0.3, gloss=0.3)
    for i, (z, r) in enumerate(((4.45, 0.5), (3.4, 0.62), (2.3, 0.52))):
        y = folk_surface(0.0, z, side=1.0)
        cull_inside(blob(coll, f"ember_{i}", ember, r, (0.0, y + 0.05, z), (0.8, 0.62, 1.0), segments=8, rings=4, joint=RIG_HEAD_FREE,
                         rot=(math.radians(-28.0), 0.0, 0.0)), shell)


def trait_yellow(coll, shell):
    pouch = na.material("pouch", "#FFEDB0", gloss=0.25)
    band = na.material("zigzag", "#B97A10", gloss=0.15)
    for side, tag in ((1.0, "l"), (-1.0, "r")):
        x, z = side * 1.3, 2.6
        y = folk_surface(x, z)
        p = facing(blob(coll, f"pouch_{tag}", pouch, 0.55, (x, y + 0.12, z), (1.0, 0.6, 0.8), segments=8, rings=4, joint=RIG_HEAD_FREE), x, y)
        cull_inside(p, shell)
    # A zig-zag band across the belly: thin bars between alternating high and low points.
    pts = [(-1.5 + i * 0.5, 1.35 + (0.22 if i % 2 else -0.22)) for i in range(7)]
    for i in range(len(pts) - 1):
        (x0, z0), (x1, z1) = pts[i], pts[i + 1]
        mx, mz = (x0 + x1) * 0.5, (z0 + z1) * 0.5
        y = folk_surface(mx, mz)
        length = math.hypot(x1 - x0, z1 - z0) + 0.2
        bar = na.box(coll, f"zig_{i}", band, (length, 0.3, 0.24), (mx, y + 0.06, mz),
                     rot=(0.0, -math.atan2(z1 - z0, x1 - x0), 0.0), joint=RIG_HEAD_FREE)
        bar.rotation_mode = 'YXZ'  # the slope about Y first, then the turn about Z
        bar.rotation_euler = (0.0, -math.atan2(z1 - z0, x1 - x0), math.atan2(mx, -y))
        cull_inside(bar, shell)


def trait_blue(coll, shell):
    # A wide flat paddle tail behind, low down, on a short stub.
    tail = na.material("paddle", "#2A60C8", gloss=0.25)
    stub = blob(coll, "tail_stub", tail, 0.7, (0.0, 1.95, 0.35), (0.85, 1.0, 0.7), segments=8, rings=4, joint=RIG_HEAD_FREE)
    cull_inside(stub, shell)
    paddle = blob(coll, "tail_paddle", tail, 1.0, (0.0, 3.0, 0.15), (1.45, 1.0, 0.26), segments=12, rings=4, joint=RIG_HEAD_FREE,
                  rot=(math.radians(-10.0), 0.0, 0.0))
    cull_inside(paddle, stub)


def trait_none(coll, shell):
    pass


# --- crests ---------------------------------------------------------------------
def crest_bead():
    coll = na.model_collection("crest_bead", joints=1)
    m = na.material("crest_pebble", "#D9D4C7", gloss=0.08)
    blob(coll, "bead", m, 0.8, (0.0, 0.0, CROWN + 0.5), (1.0, 1.0, 0.9), segments=12, rings=6)


def shard(coll, name, mat, x, tilt, height, r):
    # A six-sided crystal: a long point up, a short point down.
    up = na.cone(coll, name + "_up", mat, r, 0.0, height, (0.0, 0.0, height * 0.5), verts=6)
    down = na.cone(coll, name + "_down", mat, r, 0.0, 0.45, (0.0, 0.0, -0.225), rot=(math.pi, 0.0, 0.0), verts=6)
    for ob in (up, down):
        ob.data.transform(ob.matrix_basis)
        ob.matrix_basis = mathutils.Matrix.Identity(4)
        ob.data.transform(mathutils.Matrix.Rotation(math.radians(tilt), 4, 'Y'))
        ob.data.transform(mathutils.Matrix.Translation((x, 0.0, CROWN - 0.1)))
        na.smooth(ob, 15.0)


def crest_shards():
    coll = na.model_collection("crest_shards", joints=1)
    m = na.material("crest_ice", "#7FE3F0", emit=0.3, gloss=0.7)
    shard(coll, "shard_l", m, 0.42, 16.0, 2.6, 0.42)
    shard(coll, "shard_r", m, -0.4, -14.0, 2.0, 0.38)


def crest_star():
    coll = na.model_collection("crest_star", joints=1)
    m = na.material("crest_gold", "#FFEC9E", emit=1.0, gloss=0.4)
    R, r, depth = 1.85, 0.8, 0.65
    cz = CROWN - 0.35 + R * math.sin(math.radians(54.0))  # the two lower points rest on the crown
    verts = []
    for i in range(10):
        a = math.radians(90.0 + 36.0 * i)
        rad = R if i % 2 == 0 else r
        verts.append((math.cos(a) * rad, 0.0, cz + math.sin(a) * rad))
    verts.append((0.0, -depth, cz))  # front apex
    verts.append((0.0, depth, cz))   # back apex
    faces = []
    for i in range(10):
        j = (i + 1) % 10
        faces.append((10, j, i))
        faces.append((11, i, j))
    ob = mesh_object(coll, "star", m, verts, faces)
    na.smooth(ob, 10.0)


# --- the captain ------------------------------------------------------------------
def captain():
    coll = na.model_collection("captain", joints=12, ground=True)
    coat = na.material("coat", "#1E7F86", gloss=0.2)
    coat_dark = na.material("coat_trim", "#145B61", gloss=0.15)
    cream = na.material("cream", "#EFE6CF", gloss=0.15)
    face = na.material("face", "#F0C9A0", gloss=0.2)
    dark = na.material("eye_dark", "#1B1B22", gloss=0.5)
    shine = na.material("eye_shine", "#FFFFFF", emit=1.0)
    leather = na.material("satchel", "#7A5232", gloss=0.2)
    brass = na.material("brass", "#C9A13B", gloss=0.6)
    wood = na.material("crook", "#8A6440", gloss=0.15)
    glow = na.material("lantern", "#FFA630", emit=1.0)

    # A pear-shaped hooded coat with a darker hem, on cream boots (legs 10, 11).
    body = egg(coll, "coat", coat, 2.55, 2.3, 3.6, 2.6, 0.2, segments=12, rings=8, joint=1, floor=-0.1)
    hem = na.torus(coll, "hem", coat_dark, 1.98, 0.2, (0.0, 0.0, 0.0), major_segments=12, minor_segments=3, joint=1)
    cull_inside(hem, body)
    for side, joint, tag in ((1.0, R_LEG, "r"), (-1.0, L_LEG, "l")):
        cull_inside(blob(coll, f"boot_{tag}", cream, 1.0, (side * 0.9, -0.3, -0.5), (0.72, 1.0, 0.55), segments=6, rings=4, joint=joint), body)

    # Hood and face (on the spine joint, so the head look does not swing them off).
    hood = blob(coll, "hood", coat, 1.9, (0.0, 0.0, 6.9), (1.0, 1.0, 1.0), segments=12, rings=6, joint=1)
    cull_inside(hood, body)
    fc = blob(coll, "face", face, 1.0, (0.0, -1.45, 6.8), (1.3, 0.55, 1.2), segments=8, rings=5, joint=1)
    cull_inside(fc, hood)
    rim = na.torus(coll, "hood_rim", coat_dark, 1.42, 0.26, (0.0, -1.42, 6.85), rot=(math.radians(90.0), 0.0, 0.0),
                   major_segments=12, minor_segments=4, joint=1)
    rim.scale = (1.0, 1.08, 1.0)
    cull_inside(rim, hood)
    for side, tag in ((1.0, "l"), (-1.0, "r")):
        e = blob(coll, f"eye_{tag}", dark, 0.3, (side * 0.5, -1.98, 7.0), (0.9, 0.5, 1.35), segments=6, rings=4, joint=1)
        cull_inside(e, fc)
        cull_inside(blob(coll, f"shine_{tag}", shine, 0.09, (side * 0.5 + 0.1, -2.12, 7.18), (1, 0.6, 1), segments=4, rings=3, joint=1), e)

    # Sleeves and cream mittens; the right mitten (joint 7) is on +x like 'rhnd'.
    for side, joint, tag in ((1.0, R_HAND, "r"), (-1.0, L_HAND, "l")):
        sleeve = limb(coll, f"sleeve_{tag}", coat, (side * 1.9, 0.0, 4.9), (side * 2.6, -0.3, 3.3), 0.62, 0.5, joint=joint)
        cull_inside(sleeve, body)
        cull_inside(blob(coll, f"mitten_{tag}", cream, 0.62, (side * 2.72, -0.38, 2.95), (1.0, 1.0, 1.05), segments=6, rings=4, joint=joint),
                    body, sleeve)

    # Satchel on the back. The crook rises from it behind the left shoulder (-x;
    # the right mitten, joint 7, is on +x) and arches forward, so the lantern
    # hangs beside the hood, never above its middle like an antenna.
    sat = na.box(coll, "satchel", leather, (2.2, 1.0, 1.75), (0.0, 2.4, 3.9), joint=1)
    na.bevel(sat, 0.28, segments=1)
    na.smooth(sat, 40.0)
    buckle = na.box(coll, "buckle", brass, (0.45, 0.2, 0.4), (0.0, 2.92, 4.2), joint=1)
    cull_inside(buckle, sat)
    sx, staff_y, staff_top, arc_r = -1.5, 2.05, 10.0, 1.75
    arc_cy = staff_y - arc_r
    staff = limb(coll, "staff", wood, (-0.95, 2.35, 3.4), (sx, staff_y, staff_top), 0.17, 0.16, verts=6, joint=1)
    cull_inside(staff, sat)
    arc = na.torus(coll, "hook", wood, arc_r, 0.16, (sx, arc_cy, staff_top), rot=(0.0, math.radians(90.0), 0.0),
                   major_segments=16, minor_segments=5, joint=1)
    delete_verts(arc, lambda co: co.x <= 0.05)  # keep the upper half (local -x is world +z)
    na.smooth(arc, 50.0)
    tip_y = arc_cy - arc_r
    limb(coll, "hook_end", wood, (sx, tip_y, staff_top + 0.05), (sx, tip_y, staff_top - 0.28), 0.16, 0.14, verts=6, joint=1)

    # The lantern on the leaf-tip joint (6): a hanging ring, brass cap, amber glass, brass base.
    na.torus(coll, "lantern_ring", brass, 0.15, 0.045, (sx, tip_y, staff_top - 0.32), rot=(0.0, math.radians(90.0), 0.0),
             major_segments=6, minor_segments=3, joint=LEAF_TIP)
    cap = na.cone(coll, "lantern_cap", brass, 0.74, 0.14, 0.34, (sx, tip_y, staff_top - 0.6), verts=8, joint=LEAF_TIP)
    na.smooth(cap, 40.0)
    glass = blob(coll, "lantern_glass", glow, 0.7, (sx, tip_y, staff_top - 1.27), (1.0, 1.0, 1.0), segments=8, rings=5, joint=LEAF_TIP)
    cull_inside(glass, cap)
    base = na.cylinder(coll, "lantern_base", brass, 0.52, 0.14, (sx, tip_y, staff_top - 1.97), verts=8, joint=LEAF_TIP)
    na.smooth(base, 40.0)
    return coll


def cull_below(ob, z):
    """Deletes the faces of ob wholly under height z (under the ground, never seen)."""
    bpy.context.view_layer.update()
    to_world = ob.matrix_world.copy()
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    gone = [f for f in bm.faces if all((to_world @ v.co).z < z for v in f.verts)]
    bmesh.ops.delete(bm, geom=gone, context='FACES_ONLY')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.update()
    return ob


# --- the buried folk ----------------------------------------------------------------
def buried_folk(mats):
    coll = na.model_collection("buried_folk", joints=4, ground=True)
    # Joint 0 is drawn with the game-tinted material, so its base colour is white.
    body = na.material("buried_body", "#F4F4F4", gloss=0.22)
    soil = na.material("soil", "#8A6A45", gloss=0.05)
    clod = na.material("soil_dark", "#6B4F33", gloss=0.05)
    shell = egg(coll, "body", body, FOLK_RX, FOLK_RY, FOLK_RZ, FOLK_C, FOLK_TAPER, segments=14, rings=8, joint=0)
    cull_below(shell, -0.2)
    folk_eyes(coll, mats["eye"], mats["pupil"], mats["shine"], 1, shell)
    mound = na.torus(coll, "mound", soil, 2.55, 1.0, (0.0, 0.0, 0.25), major_segments=14, minor_segments=6, joint=1)
    mound.scale = (1.0, 1.0, 0.75)
    na.smooth(mound, 70.0)
    cull_below(mound, -0.2)
    cull_inside(mound, shell)
    for i in range(5):
        a = math.radians(20.0 + 72.0 * i)
        rr = 3.5 if i % 2 else 3.3
        cull_below(blob(coll, f"clod_{i}", clod, 0.42 if i % 2 else 0.34, (math.cos(a) * rr, math.sin(a) * rr, 0.1), (1.0, 0.9, 0.75),
                        segments=6, rings=3, joint=1), -0.05)
    return coll


def main():
    na.reset()
    mats = {
        "eye": na.material("eye_white", "#F6F3EA", gloss=0.3),
        "pupil": na.material("pupil", "#1B1B22", gloss=0.5),
        "shine": na.material("shine", "#FFFFFF", emit=1.0),
        # White: the game paints the folk body (red, yellow, blue; the Puffmin deep purple).
        "folk_body": na.material("folk_body", "#F4F4F4", gloss=0.22),
    }
    folk("folk_red", trait_red, mats)
    folk("folk_yellow", trait_yellow, mats)
    folk("folk_blue", trait_blue, mats)
    folk("folk_kin", trait_none, mats)
    crest_bead()
    crest_shards()
    crest_star()
    captain()
    buried_folk(mats)
    na.save("folk")


main()
