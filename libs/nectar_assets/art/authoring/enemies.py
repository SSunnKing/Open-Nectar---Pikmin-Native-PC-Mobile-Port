"""Seed script for art/blender/enemies.blend: the first enemy (chappy's role).

A dusk-violet shelled stilt-beetle (see ART_DIRECTION.md, First enemy): a high
domed shell with three lighter ridge plates, an amber underside, four legs
bent at the knee, and in front a wide jaw of two crab pincers under two amber
eyes on short stalks. It stands on the ground at the model origin, its shell
inside the spec's 20-unit body sphere. Only the look changes: the collision
parts are the spec's, untouched.

Each part hangs from the chappy joint that means it (standin_objects.cpp gives
the disc's parents; the collision parts name some of them): the shell on the
body (1), the tail plate on its child (2), the neck (3), the head and pincers
on the head (4, where the 'head' and 'slot' parts sit), one eye stalk on each
of its siblings (5, 6), the right legs on the right leg chain (7 hip, 8 shin
'regR_sune', 9 foot) and the left legs on the left chain (10, 11 'regL_sune',
12). The stand-in animations are identity poses, so everything is authored in
place; real animations would move the right part.

    Blender --background --factory-startup --python libs/nectar_assets/art/authoring/enemies.py
"""

import math
import os
import sys

import mathutils

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nectar_art as na  # noqa: E402

V = mathutils.Vector

# chappy's joints (standin_objects.cpp)
J_BODY, J_TAIL, J_NECK, J_HEAD, J_EYE_R, J_EYE_L = 1, 2, 3, 4, 5, 6
J_LEG = {-1: (7, 8, 9), 1: (10, 11, 12)}  # side -1 is the right (game -x), +1 the left


def between(p0, p1):
    """Location and rotation that put a primitive's Z axis from p0 to p1."""
    p0, p1 = V(p0), V(p1)
    d = p1 - p0
    rot = V((0.0, 0.0, 1.0)).rotation_difference(d.normalized()).to_euler()
    return (p0 + p1) * 0.5, d.length, tuple(rot)


def rod(coll, name, mat, p0, p1, r0, r1, verts=8, joint=0):
    """A tapered cylinder from p0 to p1, smooth along its length. Its ends sit inside a
    joint ball, a hand or the shell, so they need no bevel (that keeps the budget)."""
    loc, length, rot = between(p0, p1)
    ob = na.cone(coll, name, mat, r0, r1, length, loc, rot=rot, verts=verts, joint=joint)
    na.smooth(ob, 60.0)
    return ob


def blob(coll, name, mat, radii, loc, segments=14, rings=8, joint=0, rot=(0, 0, 0)):
    ob = na.sphere(coll, name, mat, 1.0, loc, scale=radii, segments=segments, rings=rings, joint=joint, rot=rot)
    na.smooth(ob, 80.0)
    return ob


def squash_below(ob, z0, k):
    """Pulls the vertices below object-local height z0 towards it (a flatter underside)."""
    for v in ob.data.vertices:
        if v.co.z < z0:
            v.co.z = z0 + (v.co.z - z0) * k


def pincer(coll, side, shell, claw, tip):
    """A crab claw on the head joint: an arm, a swollen hand and two fingers closing inward."""
    s = float(side)
    tag = "R" if side < 0 else "L"
    hand_c = V((s * 7.2, -17.0, 5.2))
    rod(coll, f"pincer_{tag}_arm", shell, (s * 3.6, -12.2, 6.6), hand_c + V((-s * 0.8, 2.0, 0.2)), 1.7, 1.4, joint=J_HEAD)
    blob(coll, f"pincer_{tag}_hand", claw, (3.1, 4.3, 2.8), hand_c, segments=10, rings=6, joint=J_HEAD,
         rot=(0.0, 0.0, -s * math.radians(28.0)))
    # Upper finger: long, curving in toward the middle; lower finger: short, opened a little.
    knuckle = hand_c + V((-s * 1.2, -3.3, 0.5))
    bend = knuckle + V((-s * 1.0, -2.6, 0.1))
    end = bend + V((-s * 3.0, -0.7, -0.3))
    rod(coll, f"pincer_{tag}_finger_a", claw, knuckle, bend, 1.45, 1.05, joint=J_HEAD)
    blob(coll, f"pincer_{tag}_finger_bend", claw, (1.05, 1.05, 1.05), bend, segments=6, rings=4, joint=J_HEAD)
    rod(coll, f"pincer_{tag}_finger_b", tip, bend, end, 1.0, 0.3, joint=J_HEAD)
    low0 = hand_c + V((s * 0.6, -3.4, -1.0))
    low1 = low0 + V((-s * 1.9, -2.5, -0.6))
    rod(coll, f"pincer_{tag}_thumb", tip, low0, low1, 1.05, 0.3, joint=J_HEAD)


def leg(coll, side, front, mat, joint_mat):
    """A stilt leg bent at the knee: thigh up and out from under the shell, shin down to a pad foot."""
    s = float(side)
    y = -5.0 if front else 5.5
    hip_j, shin_j, foot_j = J_LEG[side]
    tag = ("R" if side < 0 else "L") + ("f" if front else "b")
    hip = V((s * 6.8, y, 6.0))
    knee = V((s * 11.6, y * 1.2, 9.6))
    foot = V((s * 12.8, y * 1.4, 1.0))
    rod(coll, f"leg_{tag}_thigh", mat, hip, knee, 2.1, 1.8, joint=hip_j)
    blob(coll, f"leg_{tag}_knee", joint_mat, (2.2, 2.2, 2.2), knee, segments=8, rings=5, joint=shin_j)
    rod(coll, f"leg_{tag}_shin", mat, knee, foot + V((0, 0, 0.6)), 1.9, 1.4, joint=shin_j)
    blob(coll, f"leg_{tag}_foot", joint_mat, (2.0, 2.3, 1.0), foot, segments=8, rings=4, joint=foot_j)


def chappy():
    shell = na.material("enemy_shell", "#5B3C7A", gloss=0.45)
    shell_dark = na.material("enemy_shell_dark", "#3F2858", gloss=0.25)
    ridge = na.material("enemy_ridge", "#8E6DB5", gloss=0.55)
    belly = na.material("enemy_belly", "#F2A65A", gloss=0.2)
    eye = na.material("enemy_eye", "#F2A65A", emit=0.55, gloss=0.6)
    pupil = na.material("enemy_pupil", "#1C1024", gloss=0.6)
    claw_tip = na.material("enemy_claw_tip", "#E8D6BC", gloss=0.35)

    coll = na.model_collection("chappy", joints=17, ground=True)

    # Shell (body joint): a high dome over a flatter underside, the amber belly showing below its rim.
    dome = na.sphere(coll, "shell", shell, 1.0, (0.0, 1.0, 8.2), scale=(9.6, 11.6, 8.0), segments=18, rings=10, joint=J_BODY)
    squash_below(dome, 0.0, 0.28)
    na.smooth(dome, 80.0)
    blob(coll, "belly", belly, (9.2, 11.2, 2.6), (0.0, 1.0, 5.2), segments=16, rings=5, joint=J_BODY)
    # Three lighter ridge plates down the spine, each tilted to the dome's slope.
    for i, (py, w, l) in enumerate(((-5.6, 5.4, 3.9), (0.6, 6.2, 4.3), (6.8, 5.0, 3.7))):
        t = (py - 1.0) / 11.6
        top = 8.2 + 8.0 * math.sqrt(max(0.0, 1.0 - t * t))
        slope = -8.0 * t / (11.6 * math.sqrt(max(1e-3, 1.0 - t * t)))  # dz/dy along the spine
        blob(coll, f"ridge_{i}", ridge, (w, l, 1.0), (0.0, py, top - 0.15), segments=10, rings=4, joint=J_BODY,
             rot=(math.atan(slope), 0.0, 0.0))
    # Tail plate (the body's child joint): a small rounded segment closing the back.
    blob(coll, "tail", shell_dark, (5.2, 3.0, 3.0), (0.0, 12.0, 7.0), segments=10, rings=5, joint=J_TAIL)

    # Neck and head, low at the front of the shell.
    blob(coll, "neck", shell_dark, (5.4, 3.2, 3.6), (0.0, -8.6, 8.0), segments=10, rings=5, joint=J_NECK)
    blob(coll, "head", shell, (5.6, 4.4, 4.0), (0.0, -11.2, 8.4), segments=14, rings=7, joint=J_HEAD)
    blob(coll, "mouth", belly, (3.4, 1.4, 1.6), (0.0, -14.6, 6.4), segments=10, rings=5, joint=J_HEAD)
    for side in (-1, 1):
        pincer(coll, side, shell_dark, shell, claw_tip)

    # Eyes on short stalks (the head's sibling joints), amber and glowing, looking forward.
    for side, joint in ((-1, J_EYE_R), (1, J_EYE_L)):
        s = float(side)
        base = V((s * 2.2, -12.0, 11.4))
        top = V((s * 3.7, -13.0, 16.6))
        rod(coll, f"eye_{'R' if side < 0 else 'L'}_stalk", shell_dark, base, top, 0.7, 0.55, joint=joint)
        blob(coll, f"eye_{'R' if side < 0 else 'L'}_ball", eye, (2.4, 2.4, 2.4), top + V((0.0, -0.3, 1.1)), segments=10, rings=6, joint=joint)
        blob(coll, f"eye_{'R' if side < 0 else 'L'}_pupil", pupil, (0.9, 0.6, 1.3), top + V((0.0, -2.5, 1.2)), segments=6, rings=4, joint=joint)

    for side in (-1, 1):
        for front in (True, False):
            leg(coll, side, front, shell_dark, shell)
    return coll


def main():
    na.reset()
    chappy()
    na.save("enemies")


main()
