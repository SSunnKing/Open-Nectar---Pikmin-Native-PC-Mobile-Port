#!/usr/bin/env python3
"""Capture the stand-in golden screenshots and compare them to per-OS goldens (issue #87).

Subcommands:
  capture  run the scenarios and write every shot in SHOTS as <name>.png into --out
  validate require the complete, readable, nonblank capture set in --dir
  check    capture, then compare each PNG with golden/<os>/<name>.png using the OS's
           tolerance from golden/tolerance.json (or, with PIKMIN_UPDATE_GOLDENS=1, copy the
           PNGs into $PIKMIN_GOLDEN_OUT instead of comparing)

The same SHOTS table feeds the CTest check, the golden refresh (`refresh_goldens.sh`) and the
PR before/after job (`make_before_after.py`), so a new shot is added in one place.

PIKMIN_GOLDEN_ADVISORY=1 turns a golden mismatch (or a missing golden) into a warning and
exit 0. A failed capture, a wrong size or a near-blank picture still fails. The workflow sets
it for an OS whose flake rate is not yet zero or explained (README, "Golden screenshots").
"""
from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import struct
import subprocess
import sys
import zlib
from dataclasses import dataclass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from verify_scenario_png import read_png_rgb  # noqa: E402

WIDTH, HEIGHT = 640, 480
MIN_DISTINCT_COLOURS = 60
TOLERANCE_STEPS = (0, 2, 4, 8, 16)


@dataclass(frozen=True)
class Group:
    """One scenario run that writes one or more shots."""

    scenario: str
    stage: str
    rate: int
    shots: tuple  # (--scenario-screenshot tick spec, shot name)
    window: str = ""  # PIKMIN_SCENARIO_WINDOW for the whole run ("" = the 64x36 default)


# Ticks are the ones the region-checked drivers use (check_scenario_title_screens_screenshot.cmake,
# check_scenario_world_map_screenshot.cmake, check_scenario_screenshot.cmake), at 30 Hz.
SHOTS = (
    Group("boot_smoke", "stages/sandbox.ini", 30, (("100", "boot_smoke_tick100"),), window="640x480"),
    Group(
        "title_boot",
        "stages/sandbox.ini",
        30,
        (("boot:314", "title_press_start"), ("boot:350", "title_menu"), ("boot:528", "file_select")),
    ),
    Group(
        "world_map_day_end",
        "stages/sandbox_objects.ini",
        30,
        (("between:70", "world_map_idle"), ("between:120", "world_map_confirm")),
    ),
)


def shot_names() -> list[str]:
    return [name for g in SHOTS for _, name in g.shots]


def os_key() -> str:
    return {"Linux": "linux", "Darwin": "macos", "Windows": "windows"}.get(platform.system(), platform.system().lower())


def capture(game: str, data_dir: str, out_dir: str, groups=SHOTS, timeout: int = 600) -> None:
    """Run each group once; raise if the game fails or a shot is not written."""
    os.makedirs(out_dir, exist_ok=True)
    for g in groups:
        env = dict(os.environ)
        env["PIKMIN_SCENARIO_DATA_DIR"] = data_dir
        env["PIKMIN_SCENARIO_STAGE"] = g.stage
        if g.window:
            env["PIKMIN_SCENARIO_WINDOW"] = g.window
        cmd = [game, "--scenario", g.scenario, "--scenario-tick-rate", str(g.rate)]
        for spec, name in g.shots:
            path = os.path.join(out_dir, name + ".png")
            if os.path.exists(path):
                os.remove(path)
            cmd += ["--scenario-screenshot", f"{spec}:{path}"]
        r = subprocess.run(cmd, cwd=data_dir, env=env, timeout=timeout)
        if r.returncode != 0:
            raise RuntimeError(f"{g.scenario} exited {r.returncode}")
        for _, name in g.shots:
            if not os.path.exists(os.path.join(out_dir, name + ".png")):
                raise RuntimeError(f"{g.scenario} did not write {name}.png")


def write_png_rgb(path: str, width: int, height: int, pixels) -> None:
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for r, g, b in pixels[y * width : (y + 1) * width]:
            raw += bytes((r, g, b))

    def chunk(tag: bytes, body: bytes) -> bytes:
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 6)))
        f.write(chunk(b"IEND", b""))


GUTTER = 4
CHANGED_ABOVE = 8  # per-channel delta that counts a pixel as changed in a before/after picture
AMPLIFY = 4


def compose(before, after, w: int, h: int):
    """Three panels side by side: before, after, and the difference amplified 4x (black where equal).

    Returns (pixels, number of pixels over CHANGED_ABOVE, largest channel delta).
    """
    diff = []
    changed = 0
    max_delta = 0
    for (br, bg, bb), (ar, ag, ab) in zip(before, after):
        dr, dg, db = abs(br - ar), abs(bg - ag), abs(bb - ab)
        d = max(dr, dg, db)
        if d > max_delta:
            max_delta = d
        if d > CHANGED_ABOVE:
            changed += 1
        diff.append((min(255, dr * AMPLIFY), min(255, dg * AMPLIFY), min(255, db * AMPLIFY)))
    sep = [(255, 255, 255)] * GUTTER
    rows = []
    for y in range(h):
        s = y * w
        rows += before[s : s + w] + sep + after[s : s + w] + sep + diff[s : s + w]
    return rows, changed, max_delta


def write_diff_picture(golden_png: str, actual_png: str, out_png: str) -> None:
    """expected | actual | difference, for the artifact of a failed golden check."""
    gw, gh, g = read_png_rgb(golden_png)
    aw, ah, a = read_png_rgb(actual_png)
    if (gw, gh) != (aw, ah):
        return
    rows, _, _ = compose(g, a, gw, gh)
    write_png_rgb(out_png, gw * 3 + GUTTER * 2, gh, rows)


def metrics(golden, actual) -> tuple[int, dict[int, float]]:
    """Largest per-channel delta, and the fraction of pixels differing by more than each step."""
    n = len(golden)
    max_delta = 0
    over = {t: 0 for t in TOLERANCE_STEPS}
    for (gr, gg, gb), (ar, ag, ab) in zip(golden, actual):
        d = max(abs(gr - ar), abs(gg - ag), abs(gb - ab))
        if d > max_delta:
            max_delta = d
        for t in TOLERANCE_STEPS:
            if d > t:
                over[t] += 1
    return max_delta, {t: over[t] / n for t in TOLERANCE_STEPS}


def check_one(png: str, golden_png: str, tolerance: int, max_ratio: float) -> tuple[bool, str]:
    """Compare one capture. Returns (matches, one-line metrics)."""
    w, h, pixels = read_png_rgb(png)
    if (w, h) != (WIDTH, HEIGHT):
        return False, f"size {w}x{h}, expected {WIDTH}x{HEIGHT}"
    if len(set(pixels)) < MIN_DISTINCT_COLOURS:
        return False, f"only {len(set(pixels))} distinct colours (blank readback?)"
    if not os.path.exists(golden_png):
        return False, f"no golden at {golden_png}"
    gw, gh, gpixels = read_png_rgb(golden_png)
    if (gw, gh) != (w, h):
        return False, f"golden size {gw}x{gh} != capture {w}x{h}"
    max_delta, over = metrics(gpixels, pixels)
    ratio = over.get(tolerance)
    if ratio is None:
        ratio = sum(
            1
            for (gr, gg, gb), (ar, ag, ab) in zip(gpixels, pixels)
            if max(abs(gr - ar), abs(gg - ag), abs(gb - ab)) > tolerance
        ) / len(pixels)
    steps = " ".join(f"over{t}={over[t] * 100:.3f}%" for t in TOLERANCE_STEPS)
    line = f"max_delta={max_delta} {steps} (tolerance {tolerance}, at most {max_ratio * 100:.2f}% over)"
    return ratio <= max_ratio, line


def load_tolerance(golden_root: str, os_name: str) -> tuple[int, float]:
    with open(os.path.join(golden_root, "tolerance.json"), encoding="utf-8") as f:
        t = json.load(f)[os_name]
    return int(t["channel"]), float(t["max_diff_ratio"])


def validate_capture_set(directory: str) -> None:
    """Require every expected capture, with readable nonblank pixels at the target size."""
    expected = {name + ".png" for name in shot_names()}
    actual = {name for name in os.listdir(directory) if name.endswith(".png")}
    if actual != expected:
        raise RuntimeError(
            f"incomplete capture set in {directory}: "
            f"missing {sorted(expected - actual)}, unexpected {sorted(actual - expected)}"
        )
    for name in sorted(expected):
        w, h, pixels = read_png_rgb(os.path.join(directory, name))
        if (w, h) != (WIDTH, HEIGHT) or len(set(pixels)) < MIN_DISTINCT_COLOURS:
            raise RuntimeError(f"invalid capture {name}: blank or wrong size ({w}x{h})")


def run_check(args) -> int:
    os_name = args.os or os_key()
    update = os.environ.get("PIKMIN_UPDATE_GOLDENS") == "1"
    advisory = os.environ.get("PIKMIN_GOLDEN_ADVISORY") == "1"
    capture(args.game, args.data, args.work_dir)
    validate_capture_set(args.work_dir)
    if update:
        out = os.environ.get("PIKMIN_GOLDEN_OUT") or os.path.join(args.work_dir, "updated-goldens", os_name)
        os.makedirs(out, exist_ok=True)
        for name in shot_names():
            png = os.path.join(args.work_dir, name + ".png")
            shutil.copyfile(png, os.path.join(out, name + ".png"))
        print(f"golden update: wrote {len(shot_names())} PNGs to {out}")
        return 0
    tol, ratio = load_tolerance(args.golden_dir, os_name)
    failed = []
    for name in shot_names():
        png = os.path.join(args.work_dir, name + ".png")
        golden = os.path.join(args.golden_dir, os_name, name + ".png")
        try:
            ok, line = check_one(png, golden, tol, ratio)
        except ValueError as e:
            ok, line = False, f"unreadable PNG: {e}"
        print(f"golden-metrics os={os_name} shot={name} {'ok' if ok else 'DIFFERS'} {line}")
        if not ok:
            if os.path.exists(golden) and os.path.exists(png):
                diffs = os.path.join(args.work_dir, "diffs")
                os.makedirs(diffs, exist_ok=True)
                write_diff_picture(golden, png, os.path.join(diffs, name + "_expected_actual_diff.png"))
            failed.append((name, line))
    if not failed:
        print(f"golden screenshots ok ({len(shot_names())} shots, {os_name})")
        return 0
    for name, line in failed:
        msg = f"{os_name} golden {name}: {line}"
        if advisory:
            print(f"::warning title=Golden screenshot differs ({os_name})::{msg}")
        else:
            print(f"golden mismatch: {msg}", file=sys.stderr)
    if advisory:
        print("golden compare is advisory on this OS; not failing the run")
        return 0
    print(
        "If the change is intended, refresh the goldens (tools/scenarios/README.md, 'Golden screenshots').",
        file=sys.stderr,
    )
    return 1


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("capture", "check"):
        s = sub.add_parser(name)
        s.add_argument("--game", required=True)
        s.add_argument("--data", required=True, help="the stand-in data directory (holds assets/)")
        if name == "capture":
            s.add_argument("--out", required=True)
        else:
            s.add_argument("--work-dir", required=True)
            s.add_argument("--golden-dir", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden"))
            s.add_argument("--os", default="", help="override the OS key (linux, macos, windows)")
    validate = sub.add_parser("validate", help="reject incomplete or invalid golden artifacts")
    validate.add_argument("--dir", required=True)
    args = p.parse_args()
    try:
        if args.cmd == "validate":
            validate_capture_set(args.dir)
            print(f"validated {len(shot_names())} golden captures")
            return 0
        if args.cmd == "capture":
            capture(args.game, args.data, args.out)
            return 0
        return run_check(args)
    except (RuntimeError, subprocess.TimeoutExpired, OSError, ValueError) as e:
        print(f"golden_shots: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
