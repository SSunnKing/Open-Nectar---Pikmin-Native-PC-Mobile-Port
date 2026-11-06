#!/usr/bin/env python3
"""Assert the stand-in throw_flight logs against the disc-verified throw.

Every THROW line of scenario_throw_flight_<case>_<rate>.log (leaf, bud, flower and yellow
Pikmin, 30 and 60 Hz, the ctests of the same name) must show the throw the disc gives:

  grab     ticks from the A press to the grab keyframe
  release  ticks from the grab to the release (the Pikmin is Flying)
  flight   ticks from the release to the landing bounce
  peak     peak height above the release height (world units)
  vS       launch speed up (world units/s), Navi::throwPiki

The expected values were measured on 2026-11-03 three ways, with the same throws: the PC
port on Kyle's GPIE01 rev 1 disc assets, the port on the stand-in tree, and the real disc in
Dolphin (headless, game logic at 30 Hz; 60 Hz exists only in the port, so its values are
the port's on disc assets). All three agree on the ticks; Dolphin's peak is 61.26 and its
launch 289.91 (the port gives 61.29 and 290.00 on both trees), hence the float tolerances.
Yellow Pikmin throw harder (launch 370, peak 104.40 at 30 Hz and 113.16 at 60 Hz, flight 37
and 77 ticks): the port gives the same values on the disc assets and on the stand-in tree,
the stand-in from PIKMIN_TF_SPOT=-350,350 (a spot near an Onion lands some yellow throws on
it, see scenario_throw_flight.cpp).
A throw that lands on something other than the ground (y-ground, the landing height over
the map, not about 0) also fails: its flight ends early and says nothing about the throw.
The 2.6 s hold and throw_cadence are not asserted here yet.
"""
import argparse
import pathlib
import re
import sys

# case: (throws per run, {rate: (grab, release, flight, peak, vS)})
EXPECTED = {
    'leaf':   (36, {30: (1, 2, 29, 61.29, 290.0), 60: (2, 3, 61, 68.00, 290.0)}),
    'bud':    (18, {30: (1, 2, 29, 61.29, 290.0), 60: (2, 3, 61, 68.00, 290.0)}),
    'flower': (18, {30: (1, 2, 33, 61.29, 290.0), 60: (2, 3, 71, 68.00, 290.0)}),
    'yellow': (18, {30: (1, 2, 37, 104.40, 370.0), 60: (2, 3, 77, 113.16, 370.0)}),
}
PEAK_TOL   = 0.05  # units; the disc in Dolphin is 0.03 under the port
VS_TOL     = 0.10  # units/s; Dolphin 289.91
GROUND_TOL = 0.50  # units over the map at the landing point

THROW = re.compile(
    r'^\[scenario\] throw_flight_(?P<case>\w+): THROW (?P<i>\d+) dist (?P<dist>[-\d.]+) yaw (?P<yaw>[-\d.]+) .*?'
    r'grab (?P<grab>\d+) release (?P<rel>\d+) flight (?P<flight>\d+) .*?peak \+(?P<peak>[-\d.]+) .*?'
    r'vS (?P<vs>[-\d.]+) .*?y-ground (?P<yg>[-\d.]+)', re.M)


def check_log(path, case, rate):
    """Returns a list of problems (empty: the log matches the disc)."""
    count, by_rate = EXPECTED[case]
    grab, rel, flight, peak, vs = by_rate[rate]
    text = pathlib.Path(path).read_text(errors='replace')
    problems = []
    if not re.search(rf'^\[scenario\] PASS throw_flight_{case}\b', text, re.M) or re.search(r'^\[scenario\] FAIL\b', text, re.M):
        problems.append('missing PASS or reported FAIL')
    throws = [m for m in THROW.finditer(text) if m['case'] == case]
    if len(throws) != count:
        problems.append(f'{len(throws)} THROW lines, expected {count}')
    for m in throws:
        where = f"throw {m['i']} (dist {m['dist']}, yaw {m['yaw']})"
        got = (int(m['grab']), int(m['rel']), int(m['flight']))
        if got != (grab, rel, flight):
            problems.append(f'{where}: grab/release/flight {got}, disc {(grab, rel, flight)}')
        if abs(float(m['peak']) - peak) > PEAK_TOL:
            problems.append(f"{where}: peak {m['peak']}, disc {peak:.2f} (+-{PEAK_TOL})")
        if abs(float(m['vs']) - vs) > VS_TOL:
            problems.append(f"{where}: launch vy {m['vs']}, disc {vs:.2f} (+-{VS_TOL})")
        if abs(float(m['yg'])) > GROUND_TOL:
            problems.append(f"{where}: landed {m['yg']} over the ground (on an object, not the map)")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log_dir', help='the scenario-logs directory of a build')
    parser.add_argument('--label', default='')
    args = parser.parse_args()
    failed = 0
    for case in EXPECTED:
        for rate in (30, 60):
            path = pathlib.Path(args.log_dir) / f'scenario_throw_flight_{case}_{rate}.log'
            try:
                problems = check_log(path, case, rate)
            except OSError as error:
                problems = [f'cannot read: {error}']
            name = f'throw_flight_{case} @ {rate} Hz'
            if problems:
                failed += 1
                print(f'FAIL {name}: {len(problems)} problem(s)')
                for p in problems[:20]:
                    print(f'  {p}')
            else:
                g, r, f, pk, v = EXPECTED[case][1][rate]
                print(f'ok   {name}: {EXPECTED[case][0]} throws, grab {g} release {r} flight {f} peak {pk:.2f} vy {v:.2f}')
    if failed:
        print(f'{failed} throw_flight run(s) differ from the disc {args.label}'.rstrip())
        return 1
    print(f'throw_flight leaf/bud/flower/yellow match the disc {args.label}'.rstrip())
    return 0


if __name__ == '__main__':
    sys.exit(main())
