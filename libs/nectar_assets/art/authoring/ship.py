"""Seed script for art/blender/ship.blend: the captain's lantern barge (objects/ufo/ufo0705.mod).

ART_DIRECTION.md, Ship: a wide round teal-and-brass gondola like a squat kettle,
a riveted brass band around the middle, a big amber glass lamp on top (the
captain's lantern amber), four stubby ducted fan pods on short arms, each
standing on a landing foot, and a brass-rimmed cargo hatch at the front where
parts are delivered. Not elongated, no nose, no canopy, no fins: a round flying
lamp-kettle.

The fan pods sit on the diagonals, near the eng1-4 spheres (game x, z = +-14.4).
Everything hangs from joint 0; the rig keeps the disc's 57 joints.

    Blender --background --factory-startup --python libs/nectar_assets/art/authoring/ship.py
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nectar_art as na  # noqa: E402
import prop_shapes as ps  # noqa: E402

# The gondola, bottom centre to the lamp seat: (r, z).
HULL = [
    (0.0, 4.4), (7.0, 4.5), (12.0, 5.5), (15.2, 7.5), (16.8, 10.0), (17.2, 12.5), (16.9, 15.0), (15.8, 17.3),
    (13.6, 19.6), (10.6, 21.3), (7.6, 22.3), (6.6, 23.0), (6.0, 24.0), (0.0, 24.0),
]
# 0 dark teal underside, 1 teal, 2 light teal shoulder, 3 brass lid.
HULL_MATS = [0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3]
BAND_Z = 12.5
LAMP_Z = 29.6
LAMP_R = 6.2


def main():
    na.reset()
    coll = na.model_collection("ship", joints=57, ground=True)
    teal_dark = na.material("ship_teal_dark", "#16585E", gloss=0.2)
    teal = na.material("ship_teal", "#23808A", gloss=0.35)
    brass = na.material("ship_brass", "#C9A04A", gloss=0.55)
    brass_dark = na.material("ship_brass_dark", "#8C6A2C", gloss=0.4)
    iron = na.material("ship_iron", "#3D4248", gloss=0.25)
    amber = na.material("ship_lamp", "#FFA630", emit=0.85, gloss=0.6)
    amber_dim = na.material("ship_porthole", "#F2B65A", emit=0.6, gloss=0.5)
    glow_core = na.material("ship_lamp_core", "#FFE2A0", emit=1.0, gloss=0.0)

    teal_light = na.material("ship_teal_light", "#2E97A0", gloss=0.4)
    ps.lathe(coll, "hull", [teal_dark, teal, teal_light, brass], HULL, 24, HULL_MATS, smooth_angle=50.0)

    # The riveted brass band around the middle.
    na.torus(coll, "band", brass, 17.25, 1.15, (0.0, 0.0, BAND_Z), major_segments=24, minor_segments=5)
    for k in range(12):
        a = 2.0 * math.pi * (k + 0.5) / 12
        na.sphere(coll, f"rivet{k}", brass_dark, 0.55, (18.35 * math.cos(a), 18.35 * math.sin(a), BAND_Z), segments=6, rings=3)

    # The lamp: a collar, the amber globe in a brass cage, and a cap with a finial.
    na.torus(coll, "lamp_collar", brass, 5.9, 0.9, (0.0, 0.0, 24.2), major_segments=20, minor_segments=6)
    na.sphere(coll, "lamp", amber, LAMP_R, (0.0, 0.0, LAMP_Z), segments=16, rings=10)
    na.sphere(coll, "lamp_core", glow_core, 2.4, (0.0, -1.0, LAMP_Z + 0.6), segments=8, rings=5)
    for k in range(3):
        na.torus(coll, f"cage{k}", brass, LAMP_R + 0.2, 0.32, (0.0, 0.0, LAMP_Z), rot=(math.radians(90.0), 0.0, math.radians(60.0 * k)),
                 major_segments=20, minor_segments=4)
    na.sphere(coll, "lamp_cap", brass_dark, 2.6, (0.0, 0.0, LAMP_Z + LAMP_R - 0.4), scale=(1.0, 1.0, 0.55), segments=12, rings=5)
    na.sphere(coll, "finial", brass, 0.9, (0.0, 0.0, LAMP_Z + LAMP_R + 1.4), segments=8, rings=4)

    # Portholes on the shoulder, between the pods and the hatch.
    for k in range(4):
        a = math.radians(0.0 + 90.0 * k)
        rho = 15.9
        c, s = math.cos(a), math.sin(a)
        rot = (math.radians(90.0 - 25.0), 0.0, a + math.radians(90.0))
        na.cylinder(coll, f"porthole{k}", amber_dim, 1.5, 0.8, (rho * c, rho * s, 16.6), rot=rot, verts=10)
        na.torus(coll, f"porthole{k}_rim", brass, 1.65, 0.38, (rho * c, rho * s, 16.6), rot=rot, major_segments=10, minor_segments=3)

    # The cargo hatch at the front (-Y), under the band: where parts go in.
    hatch_rot = (math.radians(90.0 + 8.0), 0.0, 0.0)
    na.cylinder(coll, "hatch", teal_dark, 3.4, 0.8, (0.0, -15.9, 8.4), rot=hatch_rot, verts=14)
    na.torus(coll, "hatch_rim", brass, 3.5, 0.5, (0.0, -16.0, 8.4), rot=hatch_rot, major_segments=14, minor_segments=4)
    na.box(coll, "hatch_handle", brass_dark, (2.4, 0.6, 0.6), (0.0, -16.7, 8.4))

    # Four ducted fan pods on the diagonals, on short brass arms, each on a foot.
    for k in range(4):
        a = math.radians(45.0 + 90.0 * k)
        c, s = math.cos(a), math.sin(a)
        rho = 23.0
        pz = 11.8
        centre = (rho * c, rho * s, pz)
        ps.tube(coll, f"arm{k}", [brass], [(14.0 * c, 14.0 * s, pz), (19.6 * c, 19.6 * s, pz)], [1.3, 1.1], sides=8)
        na.torus(coll, f"duct{k}", teal_dark, 3.5, 1.15, centre, major_segments=12, minor_segments=5)
        na.torus(coll, f"duct{k}_lip", brass, 3.5, 0.45, (centre[0], centre[1], pz + 1.1), major_segments=12, minor_segments=3)
        na.sphere(coll, f"hub{k}", iron, 1.1, centre, segments=6, rings=3)
        for b in range(3):
            ba = a + math.radians(120.0 * b + 15.0)
            na.box(coll, f"blade{k}_{b}", brass, (2.9, 0.9, 0.22), (centre[0] + 1.7 * math.cos(ba), centre[1] + 1.7 * math.sin(ba), pz),
                           rot=(math.radians(25.0), 0.0, ba))
        ps.tube(coll, f"leg{k}", [iron], [(rho * c, rho * s, pz - 1.0), ((rho + 0.6) * c, (rho + 0.6) * s, 5.0), ((rho + 1.2) * c, (rho + 1.2) * s, 1.0)],
                [0.75, 0.7, 0.8], sides=6)
        na.sphere(coll, f"foot{k}", iron, 1.9, ((rho + 1.3) * c, (rho + 1.3) * s, 0.6), scale=(1.0, 1.0, 0.4), segments=8, rings=4)

    na.save("ship")


main()
