#!/usr/bin/env python3
"""Checks that the committed art/export/*.inc match a fresh export of the .blend files.

    python3 libs/nectar_assets/art/check_export.py [--blender PATH]

Blender is found from --blender, then $BLENDER, then the usual install paths.
The export runs into a temporary folder and is compared with art/export file by
file. File names, line structure, triangle and mesh tables must match exactly;
vertex numbers may differ by a small tolerance (positions 0.002, normals 0.005,
colour 2 of 255), so a different OS or CPU that rounds a last digit or flips an
ambient-occlusion ray does not fail the check, while any real edit does.
Exit 0 when they match, 1 when they differ, 2 when Blender cannot run.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

ART_DIR = os.path.dirname(os.path.abspath(__file__))
EXPORT_DIR = os.path.join(ART_DIR, "export")
VERTEX = re.compile(r"^\t\{ (.*) \},$")
TOL = [0.002] * 3 + [0.005] * 3 + [2.0] * 3
CANDIDATES = [
    "/Applications/Blender.app/Contents/MacOS/Blender",
    "/usr/bin/blender",
    "/usr/local/bin/blender",
    r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe",
]


def find_blender(arg):
    for c in [arg, os.environ.get("BLENDER")] + CANDIDATES:
        if c and (shutil.which(c) or os.path.isfile(c)):
            return c
    return None


def compare_file(name, want, got, problems):
    a = open(want, encoding="utf-8").read().splitlines()
    b = open(got, encoding="utf-8").read().splitlines()
    if len(a) != len(b):
        problems.append(f"{name}: {len(a)} lines committed, {len(b)} exported")
        return
    for i, (x, y) in enumerate(zip(a, b), 1):
        if x == y:
            continue
        mx, my = VERTEX.match(x), VERTEX.match(y)
        if mx and my:
            vx = [float(v.strip().rstrip("f")) for v in mx.group(1).split(",")]
            vy = [float(v.strip().rstrip("f")) for v in my.group(1).split(",")]
            if len(vx) == len(vy) == len(TOL) and all(abs(p - q) <= t for p, q, t in zip(vx, vy, TOL)):
                continue
        problems.append(f"{name}:{i}: committed '{x.strip()}' exported '{y.strip()}'")
        if len(problems) > 20:
            return


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--blender", help="the Blender executable")
    args = ap.parse_args()
    blender = find_blender(args.blender)
    if not blender:
        print("check_export: Blender not found (pass --blender or set BLENDER)", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory() as tmp:
        cmd = [blender, "--background", "--factory-startup", "--python", os.path.join(ART_DIR, "export_standins.py"), "--", "--out", tmp]
        run = subprocess.run(cmd, capture_output=True, text=True)
        if run.returncode != 0 or "[export]" not in run.stdout:
            print(run.stdout[-4000:] + run.stderr[-4000:], file=sys.stderr)
            print("check_export: the export failed", file=sys.stderr)
            return 2
        want = sorted(f for f in os.listdir(EXPORT_DIR) if f.endswith(".inc"))
        got = sorted(f for f in os.listdir(tmp) if f.endswith(".inc"))
        problems = []
        for f in sorted(set(want) - set(got)):
            problems.append(f"{f}: committed but not exported (a removed model?)")
        for f in sorted(set(got) - set(want)):
            problems.append(f"{f}: exported but not committed (a new model?)")
        for f in sorted(set(want) & set(got)):
            compare_file(f, os.path.join(EXPORT_DIR, f), os.path.join(tmp, f), problems)
    if problems:
        print("check_export: art/export does not match the .blend files:")
        for p in problems[:20]:
            print("  " + p)
        print("Re-export with the command in libs/nectar_assets/art/README.md and commit the result.")
        return 1
    print(f"check_export: {len(want)} files match a fresh export")
    return 0


if __name__ == "__main__":
    sys.exit(main())
