"""Seed script for art/blender/pickups.blend: nectar and the bomb rock.

ART_DIRECTION.md, Nectar and bomb rocks:
- Nectar (objects/water/water.mod, the puddle the folk drink, and
  objects/fl_water/fl_water.mod, the drop that falls and leaves it): a cluster
  of three glowing pale-green dew beads, each with a bright glint. Both models
  are the same cluster, so the drop turns into the puddle without a jump.
- Bomb rock (objects/bomb/bomb.mod): a charcoal stone ball wrapped in a
  riveted iron band, with a glowing hazard-orange crystal cluster set on top.
  The ball keeps the old stand-in's radius (3) and is lifted 1.4 so it no
  longer sits half buried; its collision sphere is unchanged.

    Blender --background --factory-startup --python libs/nectar_assets/art/authoring/pickups.py
"""

import math
import os
import sys

import mathutils

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nectar_art as na  # noqa: E402
import prop_shapes as ps  # noqa: E402

# (radius, x, y) of the three beads; they rest on the ground (z = 0).
BEADS = [(1.9, 0.8, 0.5), (1.5, -1.7, -0.3), (1.2, 0.1, -2.0)]
# The bomb's origin is where the game puts it: on the ground when it lies there
# (the old stand-in, a ball centred on the origin, sat half buried) and in the
# folk's hands when one carries it. Lifting the ball 1.4 shows most of it above
# the ground and keeps it close to the hands. It does not float: the ball's
# bottom stays about 1.6 below the origin, so it sits slightly sunk into the
# ground. A LIFT of about 3.05 would rest it flush, at the cost of carrying it
# that much higher above the hands. Draw-only either way: the collision sphere
# (standin_models.cpp, radius 6 at the origin) does not depend on the art.
LIFT = 1.4


def nectar(name, joints, ground):
    coll = na.model_collection(name, joints=joints, ground=ground)
    bead = na.material("nectar_bead", "#C6F79E", emit=0.6, gloss=0.7)
    glint = na.material("nectar_glint", "#FBFFF4", emit=1.0, gloss=0.0)
    for k, (r, x, y) in enumerate(BEADS):
        z = r * 0.88
        ob = na.sphere(coll, f"{name}_bead{k}", bead, r, (x, y, z), scale=(1.0, 1.0, 0.88), segments=12, rings=8)
        na.smooth(ob, 80.0)
        # A glint towards the key light (up, front, left).
        na.sphere(coll, f"{name}_glint{k}", glint, r * 0.22, (x - r * 0.38, y - r * 0.5, z + r * 0.5), segments=6, rings=4)
    return coll


def bomb():
    coll = na.model_collection("bomb_rock", joints=1, ground=True)
    stone = na.material("bomb_stone", "#46444A", gloss=0.12)
    iron = na.material("bomb_iron", "#5B6068", gloss=0.45)
    rivet = na.material("bomb_rivet", "#8A8F96", gloss=0.5)
    hazard = na.material("bomb_crystal", "#FF7A1A", emit=0.8, gloss=0.6)
    hazard_tip = na.material("bomb_crystal_tip", "#FFC070", emit=0.95, gloss=0.6)
    ps.lumpy_sphere(coll, "stone", [stone], 2.9, (0.0, 0.0, LIFT), segments=14, rings=9, amount=0.05, seed=3)
    # The band runs at a slant across the front, like a strap.
    tilt = mathutils.Euler((0.0, math.radians(58.0), math.radians(28.0)))
    band = na.torus(coll, "band", iron, 2.95, 0.42, (0.0, 0.0, 0.0), rot=tuple(tilt), major_segments=18, minor_segments=5)
    band.location.z = LIFT
    m = tilt.to_matrix()
    for k in range(7):
        a = 2.0 * math.pi * (k + 0.5) / 7
        p = m @ mathutils.Vector((3.3 * math.cos(a), 3.3 * math.sin(a), 0.0)) + mathutils.Vector((0.0, 0.0, LIFT))
        rv = na.sphere(coll, f"rivet{k}", rivet, 0.3, tuple(p), segments=8, rings=4)
        na.smooth(rv, 80.0)
    # The crystal cluster on top: one tall shard and two short ones leaning out.
    ps.crystal(coll, "crystal0", [hazard, hazard_tip], (0.0, 0.0, LIFT + 2.2), (0.0, 0.0, 1.0), 0.75, 2.9, 1.0)
    ps.crystal(coll, "crystal1", [hazard, hazard_tip], (0.55, 0.2, LIFT + 2.1), (0.5, 0.15, 1.0), 0.5, 2.0, 0.7, twist=0.4)
    ps.crystal(coll, "crystal2", [hazard, hazard_tip], (-0.5, -0.25, LIFT + 2.1), (-0.45, -0.3, 1.0), 0.45, 1.8, 0.65, twist=0.9)
    return coll


def main():
    na.reset()
    nectar("nectar", 1, True)
    nectar("nectar_drop", 4, False)
    bomb()
    na.save("pickups")


main()
