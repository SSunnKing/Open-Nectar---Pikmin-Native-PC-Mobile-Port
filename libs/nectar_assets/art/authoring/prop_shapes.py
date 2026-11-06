"""Shape helpers for the prop seed scripts (hives.py, ship.py, pickups.py).

Built on nectar_art.py. Everything is made from plain vertex lists, so the
shapes are deterministic and need no add-ons:
- lathe: a profile turned around the Z axis, with optional per-vertex jitter
  (the cracked rim of a hive, an uneven stone);
- crystal: a faceted prism with a pointed tip, flat shaded (crystals are the
  only sharp shapes in the art direction);
- tube: a round tube along a path of points with a radius per point.
Blender is Z up with the front at -Y.
"""

import math

import bmesh
import bpy
import mathutils

import nectar_art as na


def _object(coll, name, verts, faces, mats, face_mats=None, joint=0, smooth_angle=None):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in faces])
    mesh.update()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()
    ob = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(ob)
    na._finish(ob, coll, mats[0], joint, name)
    for m in mats[1:]:
        ob.data.materials.append(m)
    if face_mats is not None:
        for poly, mi in zip(ob.data.polygons, face_mats):
            poly.material_index = mi
    if smooth_angle is not None:
        na.smooth(ob, smooth_angle)
    return ob


def lathe(coll, name, mats, profile, segments, seg_mats=None, jitter=None, phase=0.0, joint=0, smooth_angle=50.0):
    """Turns profile [(r, z), ...] (r 0 = on the axis) around Z.

    seg_mats[i] is the material index of the band between profile points i and
    i + 1. jitter(s, i) returns (dr, dz) added to point i on segment s.
    """
    verts, faces, face_mats = [], [], []
    index = {}
    for i, (r, z) in enumerate(profile):
        if r <= 0.0:
            index[(i, None)] = len(verts)
            verts.append((0.0, 0.0, z))
            continue
        for s in range(segments):
            a = phase + 2.0 * math.pi * s / segments
            dr, dz = jitter(s, i) if jitter else (0.0, 0.0)
            rr = r + dr
            index[(i, s)] = len(verts)
            verts.append((rr * math.cos(a), rr * math.sin(a), z + dz))

    def vid(i, s):
        return index[(i, None)] if (i, None) in index else index[(i, s % segments)]

    for i in range(len(profile) - 1):
        for s in range(segments):
            quad = [vid(i, s), vid(i, s + 1), vid(i + 1, s + 1), vid(i + 1, s)]
            face = []
            for v in quad:
                if v not in face:
                    face.append(v)
            if len(face) >= 3:
                faces.append(face)
                face_mats.append(seg_mats[i] if seg_mats else 0)
    return _object(coll, name, verts, faces, mats, face_mats, joint, smooth_angle)


def _frame(direction):
    d = mathutils.Vector(direction).normalized()
    up = mathutils.Vector((0.0, 0.0, 1.0)) if abs(d.z) < 0.95 else mathutils.Vector((1.0, 0.0, 0.0))
    u = d.cross(up).normalized()
    w = d.cross(u).normalized()
    return d, u, w


def crystal(coll, name, mats, base, direction, radius, length, tip, sides=6, twist=0.0, root=0.9, joint=0):
    """A faceted crystal from base along direction: root ring, shoulder ring, apex.

    mats: [body] or [body, tip]; the pointed tip faces take the second material.
    """
    d, u, w = _frame(direction)
    b = mathutils.Vector(base)
    verts, faces, face_mats = [], [], []
    shoulder = b + d * (length - tip)
    for ring, (centre, rad) in enumerate(((b, radius * root), (shoulder, radius))):
        for k in range(sides):
            a = twist + 2.0 * math.pi * k / sides
            verts.append(tuple(centre + (u * math.cos(a) + w * math.sin(a)) * rad))
    apex = len(verts)
    verts.append(tuple(b + d * length))
    bottom = len(verts)
    verts.append(tuple(b))
    for k in range(sides):
        k1 = (k + 1) % sides
        faces.append([k, k1, sides + k1, sides + k])
        face_mats.append(0)
        faces.append([sides + k, sides + k1, apex])
        face_mats.append(1 if len(mats) > 1 else 0)
        faces.append([k1, k, bottom])
        face_mats.append(0)
    return _object(coll, name, verts, faces, mats, face_mats, joint, None)


def tube(coll, name, mats, points, radii, sides=8, joint=0, smooth_angle=60.0, caps=True):
    """A round tube through points (xyz) with a radius per point, closed at both ends."""
    pts = [mathutils.Vector(p) for p in points]
    verts, faces = [], []
    prev_u = None
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        if prev_u is None:
            _d, u, _w = _frame(t)
        else:  # parallel transport keeps the rings from twisting
            u = (prev_u - t * prev_u.dot(t)).normalized()
        w = t.cross(u).normalized()
        prev_u = u
        for k in range(sides):
            a = 2.0 * math.pi * k / sides
            verts.append(tuple(p + (u * math.cos(a) + w * math.sin(a)) * radii[i]))
    n = len(pts)
    for i in range(n - 1):
        for k in range(sides):
            k1 = (k + 1) % sides
            faces.append([i * sides + k, i * sides + k1, (i + 1) * sides + k1, (i + 1) * sides + k])
    if caps:
        c0 = len(verts)
        verts.append(tuple(pts[0]))
        c1 = len(verts)
        verts.append(tuple(pts[-1]))
        for k in range(sides):
            k1 = (k + 1) % sides
            faces.append([k1, k, c0])
            faces.append([(n - 1) * sides + k, (n - 1) * sides + k1, c1])
    return _object(coll, name, verts, faces, mats, None, joint, smooth_angle)


def fin(coll, name, mats, outline, angle, thickness, joint=0, bevel_width=0.9, smooth_angle=50.0):
    """A slab in the vertical plane at `angle` (radians from +X): outline is
    [(rho, z), ...] counter-clockwise, extruded `thickness` across the plane
    (or (inner, outer) to taper it from rho min to rho max), then bevelled."""
    c, s = math.cos(angle), math.sin(angle)
    side = mathutils.Vector((-s, c, 0.0))
    t_lo, t_hi = thickness if isinstance(thickness, tuple) else (thickness, thickness)
    rhos = [p[0] for p in outline]
    r0, r1 = min(rhos), max(rhos)
    verts, faces = [], []
    n = len(outline)
    for sign in (-1.0, 1.0):
        for rho, z in outline:
            t = t_lo + (t_hi - t_lo) * ((rho - r0) / (r1 - r0) if r1 > r0 else 0.0)
            p = mathutils.Vector((rho * c, rho * s, z)) + side * (sign * t * 0.5)
            verts.append(tuple(p))
    faces.append(list(range(n - 1, -1, -1)))
    faces.append([n + i for i in range(n)])
    for i in range(n):
        j = (i + 1) % n
        faces.append([i, j, n + j, n + i])
    ob = _object(coll, name, verts, faces, mats, None, joint, None)
    if bevel_width > 0.0:
        na.bevel(ob, bevel_width, segments=2, limit_angle=25.0)
    na.smooth(ob, smooth_angle)
    return ob


def sector(coll, name, mats, centre_angle, rows, cols=6, joint=0, bevel_width=0.8, smooth_angle=45.0):
    """A solid ring sector around Z: rows [(z, r_in, r_out, half_width_deg), ...]
    from the bottom up. A pier or a buttress that follows the ring."""
    verts, faces = [], []
    nr = len(rows)

    def vid(i, j, outer):
        return (i * cols + j) * 2 + (1 if outer else 0)

    for z, r_in, r_out, hw in rows:
        for j in range(cols):
            a = centre_angle + math.radians(hw) * (2.0 * j / (cols - 1) - 1.0)
            verts.append((r_in * math.cos(a), r_in * math.sin(a), z))
            verts.append((r_out * math.cos(a), r_out * math.sin(a), z))
    for i in range(nr - 1):
        for j in range(cols - 1):
            faces.append([vid(i, j, True), vid(i, j + 1, True), vid(i + 1, j + 1, True), vid(i + 1, j, True)])
            faces.append([vid(i, j, False), vid(i + 1, j, False), vid(i + 1, j + 1, False), vid(i, j + 1, False)])
        for j in (0, cols - 1):
            faces.append([vid(i, j, False), vid(i, j, True), vid(i + 1, j, True), vid(i + 1, j, False)])
    for i in (0, nr - 1):
        for j in range(cols - 1):
            faces.append([vid(i, j, False), vid(i, j + 1, False), vid(i, j + 1, True), vid(i, j, True)])
    ob = _object(coll, name, verts, faces, mats, None, joint, None)
    if bevel_width > 0.0:
        na.bevel(ob, bevel_width, segments=2, limit_angle=40.0)
    na.smooth(ob, smooth_angle)
    return ob


def bezier(p0, p1, p2, steps):
    """Points on a quadratic Bezier from p0 to p2 (control p1)."""
    a, b, c = (mathutils.Vector(p) for p in (p0, p1, p2))
    return [tuple(a * (1 - t) ** 2 + b * 2 * (1 - t) * t + c * t * t) for t in (i / steps for i in range(steps + 1))]


def lumpy_sphere(coll, name, mats, radius, loc, scale=(1, 1, 1), segments=14, rings=9, amount=0.06, seed=1, joint=0, smooth_angle=70.0):
    """A UV sphere with a deterministic bumpy surface (a stone)."""
    ob = na.sphere(coll, name, mats[0], radius, (0.0, 0.0, 0.0), scale=scale, segments=segments, rings=rings, joint=joint)
    for m in mats[1:]:
        ob.data.materials.append(m)
    for v in ob.data.vertices:
        p = v.co
        n = math.sin(p.x * 2.1 + seed) * math.cos(p.y * 1.7 - seed * 0.5) + 0.6 * math.sin(p.z * 2.9 + seed * 1.3)
        v.co = p * (1.0 + amount * n)
    ob.data.transform(mathutils.Matrix.Translation(loc))
    na.smooth(ob, smooth_angle)
    return ob
