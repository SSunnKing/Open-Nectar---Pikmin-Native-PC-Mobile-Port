#!/usr/bin/env python3
"""Build side-by-side before/after pictures from two directories of shots (issue #87 P3).

    make_before_after.py --before BASE_DIR --after HEAD_DIR --out OUT_DIR

For each shot name in both directories, writes OUT_DIR/<name>_before_after.png (before, after,
and the difference amplified 4x, black where the two match) and OUT_DIR/summary.md with one row
per shot. Pictures from different builds on the same runner and GPU stack should match to the
pixel when nothing visual changed, so a "changed" row is a real difference to look at.
"""
from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from golden_shots import CHANGED_ABOVE, GUTTER, compose, write_png_rgb  # noqa: E402
from verify_scenario_png import read_png_rgb  # noqa: E402

def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--before", required=True)
    p.add_argument("--after", required=True)
    p.add_argument("--out", required=True)
    args = p.parse_args()
    os.makedirs(args.out, exist_ok=True)
    names = sorted(
        n[:-4] for n in os.listdir(args.before) if n.endswith(".png") and os.path.exists(os.path.join(args.after, n))
    )
    only_before = sorted(n[:-4] for n in os.listdir(args.before) if n.endswith(".png") and n[:-4] not in names)
    only_after = sorted(n[:-4] for n in os.listdir(args.after) if n.endswith(".png") and n[:-4] not in names)
    lines = [
        "| Shot | Result | Changed pixels | Largest channel change |",
        "|------|--------|----------------|------------------------|",
    ]
    n_changed = 0
    for name in names:
        bw, bh, before = read_png_rgb(os.path.join(args.before, name + ".png"))
        aw, ah, after = read_png_rgb(os.path.join(args.after, name + ".png"))
        if (bw, bh) != (aw, ah):
            lines.append(f"| {name} | size changed ({bw}x{bh} to {aw}x{ah}) | n/a | n/a |")
            n_changed += 1
            continue
        pixels, changed, max_delta = compose(before, after, bw, bh)
        write_png_rgb(os.path.join(args.out, name + "_before_after.png"), bw * 3 + GUTTER * 2, bh, pixels)
        n_changed += 1 if changed else 0
        result = "changed" if changed else "same"
        lines.append(f"| {name} | {result} | {changed} ({changed * 100 / (bw * bh):.2f}%) | {max_delta} |")
    for name in only_before:
        lines.append(f"| {name} | removed on the head | n/a | n/a |")
    for name in only_after:
        lines.append(f"| {name} | new on the head | n/a | n/a |")
    header = (
        f"{n_changed} of {len(names)} shots differ between the base and the head "
        f"(a pixel counts as changed above a per-channel delta of {CHANGED_ABOVE})."
    )
    with open(os.path.join(args.out, "summary.md"), "w", encoding="utf-8") as f:
        f.write(header + "\n\n" + "\n".join(lines) + "\n")
    print(header)
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
