#!/usr/bin/env python3
"""Self-test for verify_scenario_png.py (run from CTest)."""
from __future__ import annotations

import struct
import sys
import tempfile
import zlib
from pathlib import Path

from verify_scenario_png import (
    compare_golden,
    count_in_region,
    distinct_color_count,
    edge_object_pixels,
    is_saturated_red,
    is_silver,
    parse_region_colour,
    region_colour_count,
    read_png_rgb,
    verify_showcase_composition,
)

ROOT = Path(__file__).resolve().parent
GOLDEN = ROOT / "golden" / "linux" / "boot_smoke_tick100.png"


def _crc32(chunk_type: bytes, data: bytes) -> int:
    import binascii

    return binascii.crc32(chunk_type + data) & 0xFFFFFFFF


def write_rgba_png(path: Path, width: int, height: int, rgba_rows: list[bytes], filter_type: int = 0) -> None:
    """Write an 8-bit RGBA PNG; each row is width*4 bytes, filter_type per scanline."""
    assert len(rgba_rows) == height
    assert all(len(r) == width * 4 for r in rgba_rows)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    raw = bytearray()
    for row in rgba_rows:
        raw.append(filter_type)
        raw.extend(row)
    idat = zlib.compress(bytes(raw), 9)

    def chunk(ctype: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + ctype + data + struct.pack(">I", _crc32(ctype, data))

    with path.open("wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))


def rgba_rows_from_rgb_pixels(
    width: int, height: int, pixels: list[tuple[int, int, int]]
) -> list[bytes]:
    rows: list[bytes] = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            r, g, b = pixels[y * width + x]
            row.extend((r, g, b, 255))
        rows.append(bytes(row))
    return rows


def run_self_test() -> None:
    if not GOLDEN.is_file():
        raise AssertionError(f"missing golden {GOLDEN}")

    w, h, pixels = read_png_rgb(str(GOLDEN))
    assert w == 640 and h == 480
    n = distinct_color_count(pixels)
    # The Blender terrain (#115) textures the ground, so the stand-in frame has about
    # 28,200-28,400 distinct colours on every OS (flat-shaded art had 100-400, grey cubes ~126).
    # The band still rejects a blank or flat frame and a decode that scrambles pixels.
    if not (15000 <= n <= 45000):
        raise AssertionError(f"golden distinct colours {n}, expected textured stand-in range (15000..45000)")

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        filter0 = tmp_path / "golden_filter0.png"
        rows = rgba_rows_from_rgb_pixels(w, h, pixels)
        write_rgba_png(filter0, w, h, rows, filter_type=0)
        _, _, decoded0 = read_png_rgb(str(filter0))
        ok, _ = compare_golden(pixels, decoded0, tolerance=0, max_diff_ratio=0.0)
        if not ok:
            raise AssertionError("filter-0 re-encoded golden must match itself exactly")

        ok_g, _ = compare_golden(pixels, decoded0, tolerance=10, max_diff_ratio=0.03)
        if not ok_g:
            raise AssertionError("filter-0 golden must pass production tolerances")

        tampered = list(pixels)
        y0 = h * 3 // 4
        for i in range(y0 * w, w * h):
            tampered[i] = (255, 0, 255)
        bad_png = tmp_path / "tampered_bottom.png"
        write_rgba_png(bad_png, w, h, rgba_rows_from_rgb_pixels(w, h, tampered), filter_type=1)
        _, _, bad_px = read_png_rgb(str(bad_png))
        bad_ok, _ = compare_golden(pixels, bad_px, tolerance=10, max_diff_ratio=0.03)
        if bad_ok:
            raise AssertionError("magenta bottom quarter must fail golden compare")

        ok_show, _ = verify_showcase_composition(
            w, h, pixels, max_near_black_ratio=0.95, min_center_non_grass=0, min_center_distinct=0
        )
        if not ok_show:
            raise AssertionError("boot golden must pass loose showcase composition helper")

    # Showcase guards on synthetic frames: grass with a red block, a silver block,
    # and a model running off the left border.
    sw, sh = 64, 48
    grass = (85, 150, 70)
    frame = [grass] * (sw * sh)
    for y in range(5, 15):
        for x in range(5, 15):
            frame[y * sw + x] = (230, 50, 55)
        for x in range(40, 50):
            frame[y * sw + x] = (215, 220, 235)
    if count_in_region(sw, sh, frame, (0, 0, 20, 20), is_saturated_red) != 100:
        raise AssertionError("red block must count 100 saturated red pixels")
    if count_in_region(sw, sh, frame, (0, 0, 20, 20), is_silver) != 0:
        raise AssertionError("red block must not count as silver")
    if count_in_region(sw, sh, frame, (35, 0, 60, 20), is_silver) != 100:
        raise AssertionError("silver block must count 100 silver pixels")
    if edge_object_pixels(sw, sh, frame, y_max=40) != 0:
        raise AssertionError("frame with models inside must have a clear border")
    for x in range(10, 20):
        frame[x] = (0, 0, 0)
    if edge_object_pixels(sw, sh, frame, y_max=40) != 0:
        raise AssertionError("black clear colour through the horizon is not a clipped model")
    for y in range(20, 30):
        for x in range(0, 6):
            frame[y * sw + x] = (230, 50, 55)
    if edge_object_pixels(sw, sh, frame, y_max=40) != 20:
        raise AssertionError("model on the left border must leave 20 border pixels (2 columns x 10 rows)")
    # #115 terrain: a sandstone map edge across the top and a soil patch on the right
    # border are ground, not models; the Onion's mauve-brown leg colour still counts.
    for y in range(0, 4):
        for x in range(sw):
            frame[y * sw + x] = (79, 74, 66) if (x + y) % 2 else (94, 87, 76)
    for y in range(30, 36):
        for x in range(sw - 4, sw):
            frame[y * sw + x] = (124, 94, 62)
    if edge_object_pixels(sw, sh, frame, y_max=40) != 20:
        raise AssertionError("sandstone and soil on the border must not count as a clipped model")
    for y in range(20, 26):
        for x in range(sw - 3, sw):
            frame[y * sw + x] = (98, 76, 76)
    if edge_object_pixels(sw, sh, frame, y_max=40) != 32:
        raise AssertionError("an Onion leg on the right border must still count (2 columns x 6 rows)")

    # Region colour guard (#99): exact colour inside a region, tolerance, and bad specs.
    region, rgb, tol, bound = parse_region_colour("0,0,20,20:E63237:12:100")
    if (region, rgb, tol, bound) != ((0, 0, 20, 20), (0xE6, 0x32, 0x37), 12, 100):
        raise AssertionError("region colour spec must parse")
    if region_colour_count(sw, sh, frame, region, rgb, tol) != 100:
        raise AssertionError("red block must count 100 pixels near E63237")
    if region_colour_count(sw, sh, frame, region, rgb, 0) != 100:
        raise AssertionError("tolerance 0 must match (230,50,55) exactly against E63237")
    if region_colour_count(sw, sh, frame, region, (0xE6, 0x32, 0x3A), 2) != 0:
        raise AssertionError("tolerance 2 must not match blue 55 against 58")
    if region_colour_count(sw, sh, frame, (35, 0, 60, 20), rgb, tol) != 0:
        raise AssertionError("silver block must not match red")
    for bad in ("0,0,20,20:E6323:12:1", "0,0,20,20:E63237:12", "20,0,0,20:E63237:1:1"):
        try:
            parse_region_colour(bad)
        except ValueError:
            continue
        raise AssertionError(f"bad region colour spec {bad!r} must be rejected")

    print(f"verify_scenario_png self-test ok (golden {n} distinct colours)")


def main() -> int:
    try:
        run_self_test()
        return 0
    except AssertionError as e:
        print(f"self-test failed: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
