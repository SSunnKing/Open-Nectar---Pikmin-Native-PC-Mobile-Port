"""Seed script for art/blender/hives.blend: the home hive and its three crystal sets.

The hive (ART_DIRECTION.md, Home hives) is one shared model, objects/goal/goal.mod:
a squat stone geode dome on four short buttresses (wide piers that leave a round
arch on each side), cracked open at the top, with a pale dormant crystal spire
in the crack and a low stone ramp in the front arch. The game draws the same
goal.mod for every Onion, so the hive itself carries no type colour.

The colour comes from effects/shapes/[rby]_goal.mod: the game draws the one that
matches the Onion's type at the Onion's position (GoalItem::startAI), at 0.1
scale, and squeezes it flat while the Onion is dormant (startConeShrink) and
grows it back when the Onion boots (startConeEmit). Those three models are the
coloured crystals: a spire that encloses the pale one, two side spires and a
ring of crystals in the crack. They are authored at hive scale and bound with
scale 10 to cancel the effect's 0.1. They are not rotated with the hive, so
everything in them grows out of the round cavity and nothing depends on the
hive's yaw.

Sizes: the dome's radius is about 18 and its underside sits 12.8 up, so the
captain and the folk fit under it at the cont spot (the origin); the spire
tops out at 50, which keeps the hive inside the standin_showcase frame.

    Blender --background --factory-startup --python libs/nectar_assets/art/authoring/hives.py
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nectar_art as na  # noqa: E402
import prop_shapes as ps  # noqa: E402

SEGMENTS = 18
# Rim heights of the crack, one per segment: a crown of uneven teeth.
JAG = [0.0, 2.6, 1.0, 3.4, 0.3, 2.0, 3.0, 0.8, 2.4, 1.3, 3.6, 0.2, 1.8, 2.9, 0.6, 2.2, 1.1, 3.2]

# The dome shell as one closed profile (r, z): the underside, the outside up to
# the cracked rim, then down the inside of the crack to the cavity floor.
DOME = [
    (0.0, 12.8), (6.0, 12.6), (11.5, 13.0), (15.5, 14.6), (17.6, 17.2), (18.4, 20.5), (18.0, 23.8),
    (16.6, 27.0), (14.2, 29.8), (11.8, 31.8), (10.6, 32.8), (9.4, 32.2), (8.6, 29.5), (6.0, 26.8),
    (2.5, 25.8), (0.0, 25.6),
]
# Material per band: 0 underside, 1 stone, 2 strata, 3 rim, 4 cavity.
DOME_MATS = [0, 0, 0, 1, 2, 1, 1, 1, 3, 3, 3, 4, 4, 4, 4]
# How much of the crown's jag each profile point takes.
JAG_WEIGHT = {8: 0.25, 9: 0.65, 10: 1.0, 11: 0.85, 12: 0.3}

SPIRE_BASE = 24.0


def dome_jitter(s, i):
    if i in (0, len(DOME) - 1):
        return 0.0, 0.0
    wobble = 0.55 * math.sin(s * 2.3 + i * 1.7) + 0.3 * math.cos(s * 4.1 - i * 0.9)
    return wobble, JAG[s] * JAG_WEIGHT.get(i, 0.0)


def hive(coll_name="hive"):
    coll = na.model_collection(coll_name, joints=13, ground=True)
    under = na.material("hive_underside", "#5E574C", gloss=0.05)
    stone = na.material("hive_stone", "#8C8577", gloss=0.1)
    strata = na.material("hive_strata", "#6F675A", gloss=0.08)
    rim = na.material("hive_rim", "#A39B8A", gloss=0.12)
    cavity = na.material("hive_cavity", "#2B2622", gloss=0.0)
    moss = na.material("hive_moss", "#6E9E3A", gloss=0.05)
    moss_dark = na.material("hive_moss_dark", "#4F7A2A", gloss=0.05)
    quartz = na.material("hive_quartz", "#D6D1C6", emit=0.1, gloss=0.5)
    quartz_tip = na.material("hive_quartz_tip", "#ECE8DF", emit=0.15, gloss=0.6)

    ps.lathe(coll, "dome", [under, stone, strata, rim, cavity], DOME, SEGMENTS, DOME_MATS, dome_jitter, phase=0.1, smooth_angle=55.0)

    # Four short buttresses on the diagonals: wide stone piers that follow the
    # ring and flare at the foot, leaving an arched opening on each side (the
    # front one at -Y) for the captain and the folk to walk under the dome.
    # The piers widen near the top until they almost meet: four round arches.
    pier = [(0.0, 13.4, 22.4, 25.0), (2.0, 14.0, 21.3, 21.5), (5.5, 14.6, 20.1, 19.0), (8.0, 14.8, 19.6, 19.5),
            (10.0, 14.8, 19.3, 22.0), (11.8, 14.6, 19.0, 27.0), (13.6, 14.2, 18.8, 34.0), (16.5, 13.5, 18.6, 42.0),
            (19.5, 12.5, 18.2, 44.0)]
    for k in range(4):
        a = math.radians(45.0 + 90.0 * k)
        c, s = math.cos(a), math.sin(a)

        def at(rho, z, turn=0.0):
            return (rho * math.cos(a + turn), rho * math.sin(a + turn), z)

        ps.sector(coll, f"buttress{k}", [stone], a, pier, cols=8, bevel_width=0.9)
        # A strata line across each pier, and moss at its foot.
        ps.sector(coll, f"buttress{k}_band", [strata], a, [(5.0, 14.4, 20.4, 19.2), (6.2, 14.4, 20.2, 19.2)], cols=6, bevel_width=0.0)
        na.sphere(coll, f"moss{k}a", moss, 2.0, at(22.4, 0.8, math.radians(-12.0)), scale=(1.0, 1.0, 0.55), segments=8, rings=4)
        na.sphere(coll, f"moss{k}b", moss_dark, 1.5, at(22.0, 0.6, math.radians(14.0)), scale=(1.0, 1.0, 0.55), segments=8, rings=4)

    # The lip: a low stone ramp in the front opening, leading in under the dome.
    lip = na.box(coll, "lip", strata, (11.0, 10.0, 2.4), (0.0, -19.5, 0.4), rot=(math.radians(10.0), 0.0, 0.0))
    na.bevel(lip, 1.0, segments=2)
    na.smooth(lip, 50.0)

    # The dormant crystal: a pale quartz spire in the crack. The coloured spire of
    # the hive's type (below) encloses it once the hive boots.
    ps.crystal(coll, "quartz_spire", [quartz, quartz_tip], (0.0, 0.0, SPIRE_BASE), (0.0, 0.0, 1.0), 2.2, 21.0, 5.0, twist=0.3)
    return coll


def crystals(colour_name, body_hex, tip_hex):
    coll = na.model_collection(f"hive_crystals_{colour_name}", joints=2)
    body = na.material(f"crystal_{colour_name}", body_hex, emit=0.35, gloss=0.55)
    tip = na.material(f"crystal_{colour_name}_tip", tip_hex, emit=0.45, gloss=0.65)
    mats = [body, tip]
    # The spire: inradius 3.6 * cos(30) = 3.1 is wider than the pale spire's 2.2
    # at every height, at any yaw, so it hides it.
    ps.crystal(coll, "spire", mats, (0.0, 0.0, SPIRE_BASE), (0.0, 0.0, 1.0), 3.6, 26.0, 7.0)
    for k, (x, y, lean, length) in enumerate(((2.4, 0.8, 20.0, 17.0), (-2.0, -1.5, 24.0, 14.5))):
        a = math.atan2(y, x)
        d = (math.sin(math.radians(lean)) * math.cos(a), math.sin(math.radians(lean)) * math.sin(a), math.cos(math.radians(lean)))
        ps.crystal(coll, f"side{k}", mats, (x, y, SPIRE_BASE + 0.5), d, 2.0, length, 4.5, twist=0.5 * k)
    # The crack ring: crystals growing out of the cavity and over the rim.
    for k in range(9):
        a = math.radians(20.0 + 40.0 * k + 7.0 * math.sin(k * 2.7))
        lean = math.radians(34.0 + 6.0 * math.sin(k * 1.9))
        rho = 6.4 + 0.4 * math.cos(k * 3.1)
        d = (math.sin(lean) * math.cos(a), math.sin(lean) * math.sin(a), math.cos(lean))
        length = 12.5 + 2.0 * math.sin(k * 2.2)
        radius = 2.1 + 0.35 * math.cos(k * 1.3)
        ps.crystal(coll, f"ring{k}", mats, (rho * math.cos(a), rho * math.sin(a), 27.0), d, radius, length, 3.2, twist=k * 0.4)
    return coll


def main():
    na.reset()
    hive()
    crystals("red", "#E8412F", "#FF8A6A")
    crystals("yellow", "#F5C518", "#FFE680")
    crystals("blue", "#2F6FE0", "#7FAAFF")
    na.save("hives")


main()
