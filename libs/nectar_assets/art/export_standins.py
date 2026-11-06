"""Exports the disc-free stand-in art from the .blend sources to C++ includes.

Run with Blender (tested with 5.2.3), from the repository root:

    Blender --background --factory-startup --python libs/nectar_assets/art/export_standins.py -- [--out DIR] [BLEND ...]

With no BLEND arguments it exports every art/blender/*.blend. --out defaults to
art/export (the committed copy); check_export.py exports to a temporary folder
and compares.

What it reads from each .blend
------------------------------
- Every collection with a string custom property ``nectar_model`` is one model.
  ``nectar_joints`` (int) is the joint count of the .mod it feeds; the C++ side
  checks it against the rig.
- Every visible mesh object in that collection (and its child collections) is
  exported, with its modifiers applied, in model space. ``nectar_joint`` (int,
  default 0) on the object is the joint the mesh hangs from. The stand-in rigs
  keep every joint at the model origin, so geometry is authored in place.
- Colour: each face's material viewport colour (``diffuse_color``), or a
  colour attribute named ``Base`` when the object has one. Material custom
  properties: ``nectar_emit`` (0-1, how much the colour ignores the lighting,
  for glowing parts) and ``nectar_gloss`` (0-1, highlight strength).

What it writes
--------------
One ``<model>.inc`` per model and ``all.inc`` that includes them and lists them,
in the format standin_art_meshes.cpp declares. Blender is Z-up and the game is
Y-up: game (x, y, z) = Blender (x, z, -y), so a model's front (-Y in Blender)
faces the game's +z. Triangles are written clockwise seen from the front, the
.mod convention. Lighting is baked into the vertex colours here (see bake()),
deterministically, so the export of an unchanged .blend is unchanged.
"""

import math
import os
import sys

import bpy
import mathutils
from mathutils.bvhtree import BVHTree

ART_DIR = os.path.dirname(os.path.abspath(__file__))
FORMAT_VERSION = 1

# --- lighting (Blender space, Z up, front is -Y) -----------------------------
KEY_DIR = mathutils.Vector((-0.35, -0.55, 0.76)).normalized()  # towards the key light
VIEW_DIR = mathutils.Vector((0.0, -0.62, 0.78)).normalized()   # a gameplay camera, above and in front
SKY = mathutils.Vector((0.52, 0.55, 0.60))
GROUND = mathutils.Vector((0.24, 0.22, 0.20))
KEY = mathutils.Vector((0.78, 0.74, 0.66))
WRAP = 0.25
AO_RAYS = 48
AO_STRENGTH = 0.75


def fibonacci_dirs(n):
    dirs = []
    golden = math.pi * (3.0 - math.sqrt(5.0))
    for i in range(n):
        z = 1.0 - (i + 0.5) * 2.0 / n
        r = math.sqrt(max(0.0, 1.0 - z * z))
        a = golden * i
        dirs.append(mathutils.Vector((math.cos(a) * r, math.sin(a) * r, z)))
    return dirs


AO_DIRS = fibonacci_dirs(AO_RAYS * 2)


def linear_to_srgb(c):
    c = min(max(c, 0.0), 1.0)
    return c * 12.92 if c <= 0.0031308 else 1.055 * (c ** (1.0 / 2.4)) - 0.055


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def shade(base, emit, gloss, n, ao):
    """Linear base colour lit by the fixed rig; returns linear RGB."""
    hemi = 0.5 + 0.5 * n.z
    ambient = GROUND.lerp(SKY, hemi)
    ndl = (n.dot(KEY_DIR) + WRAP) / (1.0 + WRAP)
    diffuse = KEY * max(0.0, ndl)
    occl = 1.0 - AO_STRENGTH * (1.0 - ao)
    light = (ambient + diffuse) * occl
    half = (KEY_DIR + VIEW_DIR).normalized()
    spec = gloss * (max(0.0, n.dot(half)) ** 28) * occl
    out = []
    for k in range(3):
        lit = base[k] * light[k] + spec
        out.append(base[k] * emit + lit * (1.0 - emit))
    return out


def ambient_occlusion(bvh, p, n, reach):
    if bvh is None:
        return 1.0
    origin = p + n * (reach * 0.002 + 1e-4)
    total = 0.0
    hit = 0.0
    for d in AO_DIRS:
        c = d.dot(n)
        if c <= 0.0:
            continue
        total += c
        loc, _nrm, _idx, _dist = bvh.ray_cast(origin, d, reach)
        if loc is not None:
            hit += c
    return 1.0 - hit / total if total > 0.0 else 1.0


# --- reading the scene --------------------------------------------------------
def collection_objects(coll):
    seen = []
    for ob in coll.all_objects:
        if ob.type == 'MESH' and not ob.hide_render and ob not in seen:
            seen.append(ob)
    return sorted(seen, key=lambda o: o.name)


def evaluated_mesh(ob, depsgraph):
    ev = ob.evaluated_get(depsgraph)
    mesh = ev.to_mesh(preserve_all_data_layers=True, depsgraph=depsgraph)
    mesh.transform(ob.matrix_world)
    if ob.matrix_world.determinant() < 0.0:
        mesh.flip_normals()
    mesh.calc_loop_triangles()
    return ev, mesh


def material_info(mat):
    if mat is None:
        return (0.8, 0.8, 0.8), 0.0, 0.0
    col = tuple(mat.diffuse_color[:3])
    emit = float(mat.get("nectar_emit", 0.0))
    gloss = float(mat.get("nectar_gloss", 0.15))
    return col, emit, gloss


def export_model(coll, depsgraph, blend_name):
    name = coll["nectar_model"]
    joints = int(coll.get("nectar_joints", 1))
    ground = bool(coll.get("nectar_ground", False))
    objects = collection_objects(coll)
    if not objects:
        raise RuntimeError(f"{blend_name}: collection {coll.name} has no mesh objects")

    meshes = []
    all_verts, all_tris = [], []
    for ob in objects:
        ev, mesh = evaluated_mesh(ob, depsgraph)
        meshes.append((ob, ev, mesh))
        base = len(all_verts)
        all_verts.extend(v.co.copy() for v in mesh.vertices)
        all_tris.extend(tuple(base + i for i in t.vertices) for t in mesh.loop_triangles)
    lo = mathutils.Vector((min(v.x for v in all_verts), min(v.y for v in all_verts), min(v.z for v in all_verts)))
    hi = mathutils.Vector((max(v.x for v in all_verts), max(v.y for v in all_verts), max(v.z for v in all_verts)))
    size = (hi - lo).length
    if ground:  # a floor under the model darkens what touches the ground
        g = size * 2.0
        k = len(all_verts)
        all_verts.extend([mathutils.Vector((-g, -g, 0.0)), mathutils.Vector((g, -g, 0.0)), mathutils.Vector((g, g, 0.0)), mathutils.Vector((-g, g, 0.0))])
        all_tris.extend([(k, k + 1, k + 2), (k, k + 2, k + 3)])
    bvh = BVHTree.FromPolygons(all_verts, all_tris, epsilon=0.0)
    reach = size * 0.35

    by_joint = {}
    for ob, ev, mesh in meshes:
        joint = int(ob.get("nectar_joint", 0))
        if not 0 <= joint < joints:
            raise RuntimeError(f"{blend_name}: {ob.name} hangs from joint {joint}, but {name} has {joints}")
        base_attr = mesh.color_attributes.get("Base")
        ao_cache = {}
        normals = mesh.corner_normals
        out = by_joint.setdefault(joint, {"verts": [], "index": {}, "tris": []})
        for tri in mesh.loop_triangles:
            mat = ev.material_slots[tri.material_index].material if tri.material_index < len(ev.material_slots) else None
            mcol, emit, gloss = material_info(mat)
            corner = []
            for li, vi in zip(tri.loops, tri.vertices):
                p = mesh.vertices[vi].co
                n = mathutils.Vector(normals[li].vector).normalized()
                if base_attr is not None:
                    d = base_attr.data[li if base_attr.domain == 'CORNER' else vi]
                    col = tuple(srgb_to_linear(c) for c in d.color_srgb[:3])
                else:
                    col = mcol
                key_ao = (vi, round(n.x, 2), round(n.y, 2), round(n.z, 2))
                ao = ao_cache.get(key_ao)
                if ao is None:
                    ao = ambient_occlusion(bvh, p, n, reach)
                    ao_cache[key_ao] = ao
                lit = shade(col, emit, gloss, n, ao)
                rgb = tuple(int(round(linear_to_srgb(c) * 255.0)) for c in lit)
                gp = (round(p.x, 4) + 0.0, round(p.z, 4) + 0.0, round(-p.y, 4) + 0.0)
                gn = (round(n.x, 3) + 0.0, round(n.z, 3) + 0.0, round(-n.y, 3) + 0.0)
                vkey = gp + gn + rgb
                idx = out["index"].get(vkey)
                if idx is None:
                    idx = len(out["verts"])
                    out["index"][vkey] = idx
                    out["verts"].append(vkey)
                corner.append(idx)
            if len(set(corner)) < 3:
                continue
            # Collapsed bevels leave zero-area slivers; they draw nothing and have no winding.
            pa, pb, pc = (mathutils.Vector(out["verts"][i][0:3]) for i in corner)
            if (pb - pa).cross(pc - pa).length < 1e-6:
                continue
            out["tris"].append((corner[0], corner[2], corner[1]))  # clockwise from the front
        ev.to_mesh_clear()
    return name, joints, by_joint


def fmt(x, places):
    s = f"{x:.{places}f}"
    if s.startswith("-") and float(s) == 0.0:
        s = s[1:]
    return s


def write_model(path, name, joints, by_joint, source):
    vcount = sum(len(m["verts"]) for m in by_joint.values())
    tcount = sum(len(m["tris"]) for m in by_joint.values())
    if vcount > 0xFFFF:
        raise RuntimeError(f"{name}: {vcount} vertices do not fit 16-bit indices")
    ident = "kArt_" + name
    lines = [
        f"// {name}: from art/blender/{source}. Written by art/export_standins.py; do not edit.",
        f"// format {FORMAT_VERSION}, {joints} joints, {len(by_joint)} meshes, {vcount} vertices, {tcount} triangles.",
        "// Vertex: game-space position, normal, then sRGB colour with the lighting baked in.",
        f"static const ArtVertex {ident}_v[] = {{",
    ]
    meshes = []
    first_v = first_t = 0
    tris = []
    for joint in sorted(by_joint):
        m = by_joint[joint]
        for v in m["verts"]:
            lines.append("\t{ " + ", ".join(fmt(c, 4) + "f" for c in v[0:3]) + ", " + ", ".join(fmt(c, 3) + "f" for c in v[3:6]) + ", "
                         + ", ".join(str(c) for c in v[6:9]) + " },")
        for t in m["tris"]:
            tris.append(t)
        meshes.append((joint, first_v, len(m["verts"]), first_t, len(m["tris"])))
        first_v += len(m["verts"])
        first_t += len(m["tris"])
    lines.append("};")
    lines.append(f"static const ArtTri {ident}_t[] = {{")
    for t in tris:
        lines.append(f"\t{{ {t[0]}, {t[1]}, {t[2]} }},")
    lines.append("};")
    lines.append(f"static const ArtMesh {ident}_m[] = {{")
    lines.append("\t// joint, first vertex, vertex count, first triangle, triangle count")
    for mm in meshes:
        lines.append("\t{ " + ", ".join(str(x) for x in mm) + " },")
    lines.append("};")
    lines.append(f'static const ArtModel {ident} = {{ "{name}", {joints}, {ident}_m, {len(meshes)}, {ident}_v, {ident}_t }};')
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out_dir = os.path.join(ART_DIR, "export")
    blends = []
    i = 0
    while i < len(argv):
        if argv[i] == "--out":
            out_dir = os.path.abspath(argv[i + 1])
            i += 2
        else:
            blends.append(os.path.abspath(argv[i]))
            i += 1
    os.makedirs(out_dir, exist_ok=True)
    if not blends:  # a full export: start from an empty folder so removed models go
        bdir = os.path.join(ART_DIR, "blender")
        blends = sorted(os.path.join(bdir, f) for f in os.listdir(bdir) if f.endswith(".blend"))
        for f in os.listdir(out_dir):
            if f.endswith(".inc"):
                os.remove(os.path.join(out_dir, f))

    names = []
    for blend in blends:
        bpy.ops.wm.open_mainfile(filepath=blend)
        depsgraph = bpy.context.evaluated_depsgraph_get()
        source = os.path.basename(blend)
        for coll in sorted(bpy.data.collections, key=lambda c: c.name):
            if "nectar_model" not in coll:
                continue
            name, joints, by_joint = export_model(coll, depsgraph, source)
            if name in names:
                raise RuntimeError(f"model {name} is defined twice")
            names.append(name)
            write_model(os.path.join(out_dir, name + ".inc"), name, joints, by_joint, source)
            print(f"[export] {source}: {name}")

    names = sorted(f[:-4] for f in os.listdir(out_dir) if f.endswith(".inc") and f != "all.inc")
    lines = ["// Every stand-in art model. Written by art/export_standins.py; do not edit."]
    lines += [f'#include "{n}.inc"' for n in names]
    lines.append("static const ArtModel* const kArtModels[] = {")
    lines += [f"\t&kArt_{n}," for n in names]
    lines.append("};")
    with open(os.path.join(out_dir, "all.inc"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"[export] {len(names)} models to {out_dir}")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:  # make Blender exit non-zero
        import traceback
        traceback.print_exc()
        sys.exit(1)
