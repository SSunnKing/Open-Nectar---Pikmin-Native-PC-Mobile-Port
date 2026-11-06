"""Seed script for art/blender/pellets.blend: the 1, 5, 10 and 20 pellets.

Honey-gold seed tokens standing on edge: a thick disc with a rounded rim, a
raised inner face on both sides and the value in raised dark numerals (see
ART_DIRECTION.md, Pellets). Sizes match the earlier stand-ins (radius 0.9 x
the spec's half size), so carry and collision are unchanged; only the look is.

    Blender --background --factory-startup --python libs/nectar_assets/art/authoring/pellets.py
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nectar_art as na  # noqa: E402

# Seven-segment layout in a 1 x 2 digit box: (centre x, centre z, horizontal?)
SEGMENTS = {
    "a": (0.0, 1.0, True), "b": (0.5, 0.5, False), "c": (0.5, -0.5, False), "d": (0.0, -1.0, True),
    "e": (-0.5, -0.5, False), "f": (-0.5, 0.5, False), "g": (0.0, 0.0, True),
}
DIGITS = {"0": "abcdef", "2": "abged", "5": "afgcd"}


def digit(coll, mat, ch, cx, cz, w, face_y, depth, side, tag):
    """Raised strokes for one digit on the face at y = face_y (side -1 front, +1 back)."""
    t = w * 0.36  # stroke thickness
    mirror = -1.0 if side > 0 else 1.0  # the back face reads correctly from behind
    parts = []
    if ch == "1":
        parts.append(na.box(coll, f"{tag}_1_stem", mat, (t, depth, w * 2.0 + t), (cx * mirror, face_y, cz)))
        flag = na.box(coll, f"{tag}_1_flag", mat, (t * 0.9, depth, w * 0.7), (cx * mirror - mirror * w * 0.3, face_y, cz + w * 0.72),
                      rot=(0.0, mirror * math.radians(-55.0), 0.0))
        parts.append(flag)
        parts.append(na.box(coll, f"{tag}_1_base", mat, (w * 0.9, depth, t * 0.8), (cx * mirror, face_y, cz - w)))
    else:
        for s in DIGITS[ch]:
            sx, sz, horiz = SEGMENTS[s]
            size = (w + t, depth, t) if horiz else (t, depth, w + t)
            parts.append(na.box(coll, f"{tag}_{ch}_{s}", mat, size, ((cx + sx * w) * mirror, face_y, cz + sz * w)))
    for p in parts:
        na.bevel(p, t * 0.3, segments=1)
        na.smooth(p, 50.0)


def pellet(value, r, body, face, ink):
    coll = na.model_collection(f"pellet_{value}", joints=3, ground=True)
    thick = r * 0.62
    cz = r  # stands on its rim, centre at height r
    rot = (math.radians(90.0), 0.0, 0.0)  # cylinder axis along Y: faces look front and back
    disc = na.cylinder(coll, "body", body, r, thick, (0.0, 0.0, cz), rot=rot, verts=24)
    na.bevel(disc, r * 0.2, segments=3)
    na.smooth(disc, 45.0)
    inner_r = r * 0.74
    inner_t = thick + r * 0.08
    inner = na.cylinder(coll, "face", face, inner_r, inner_t, (0.0, 0.0, cz), rot=rot, verts=24)
    na.bevel(inner, r * 0.035, segments=1)
    na.smooth(inner, 45.0)

    text = str(value)
    w = r * (0.34 if len(text) == 1 else 0.23)
    gap = w * 1.75
    depth = r * 0.07
    for side in (-1, 1):
        face_y = side * (inner_t * 0.5 + depth * 0.35)
        for i, ch in enumerate(text):
            cx = (i - (len(text) - 1) * 0.5) * gap
            digit(coll, ink, ch, cx, cz, w, face_y, depth, side, f"{'front' if side < 0 else 'back'}{i}")
    return coll


def main():
    na.reset()
    body = na.material("pellet_body", "#E9B949", gloss=0.35)
    face = na.material("pellet_face", "#F6D77A", gloss=0.45)
    ink = na.material("pellet_numeral", "#5A3517", gloss=0.2)
    # white1-4: the 1, 5, 10 and 20 pellets; radius 0.9 x the spec half size (5, 10, 17, 24).
    for value, half in ((1, 5.0), (5, 10.0), (10, 17.0), (20, 24.0)):
        pellet(value, half * 0.9, body, face, ink)
    na.save("pellets")


main()
