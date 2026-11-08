#!/usr/bin/env python3
"""Assert throw flight against the selected fixture contract.

Each profile checks throw count, grab/release/flight ticks, peak height, launch
velocity and ground contact at 30, 60 and 120 Hz. Input keyframes retain the
configured rate's cadence; high-frequency contact may quantize landing by one
high-rate tick.

  grab     ticks from the A press to the grab keyframe
  release  ticks from the grab to Flying (Navi::throwPiki)
  flight   ticks from release to the landing bounce
  peak     sampled height above release, in world units
  vS       launch speed up, in world units/second

The synthetic profile (the public CI default) uses independently authored
animation timing and the source constructor parameters. Its 30 Hz values are the
fixture's own characterization. The 60 and 120 Hz rows must reproduce the 30 Hz
peak and launch speed (ballistic parity, issue #157); tick counts scale with the
rate. They are measured port results on authored fixtures, not GameCube data.

The disc profile is the local exact lane (run_disc_throw_fidelity.py, read-only
user-supplied disc assets). Its 30 Hz values were measured on 2026-11-03 three
ways with the same throws: the PC port on a GPIE01 rev 1 disc install, the port on
the stand-in tree of that date, and the real disc in Dolphin (headless, game logic
at 30 Hz). All three agree on the ticks; Dolphin's peak is 61.26 and its launch
289.91 (the port gives 61.29 and 290.00), hence the float tolerances. The 60/120 Hz
disc rows are port results, not GameCube measurements; the original-disc evidence
and its limits are in evidence/ballistic-gc-2026-11-06.json. Yellow Pikmin throw
harder (launch 370, peak 104.40, flight 37 ticks at 30 Hz).
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
    'leaf':   (36, {30: (1, 2, 29, 61.29, 290.0), 60: (2, 3, 58, 61.29, 290.0), 120: (4, 5, 116, 61.29, 290.0)}),
    'bud':    (18, {30: (1, 2, 29, 61.29, 290.0), 60: (2, 3, 58, 61.29, 290.0), 120: (4, 5, 116, 61.29, 290.0)}),
    'flower': (18, {30: (1, 2, 33, 61.29, 290.0), 60: (2, 3, 66, 61.29, 290.0), 120: (4, 5, 132, 61.29, 290.0)}),
    'yellow': (18, {30: (1, 2, 37, 104.40, 370.0), 60: (2, 3, 74, 104.40, 370.0), 120: (4, 5, 149, 104.40, 370.0)}),
}
# The authored motions emit Action0 at frame 10 at 30 fps. The synthetic parameter files
# retain source defaults. These flight measurements use the authored flat throw area. Peak
# and launch speed are identical at every rate (ballistic parity, issue #157); grab, release
# and flight ticks scale with the rate, and a landing at 60/120 Hz can differ by one tick
# (flower glide also varies with direction).
SYNTHETIC_EXPECTED = {
    'leaf':   (36, {30: (10, 11, 30, 66.25, 297.50), 60: (20, 21, 61, 66.25, 297.50), 120: (40, 41, 122, 66.25, 297.50)}),
    'bud':    (18, {30: (10, 11, 30, 66.25, 297.50), 60: (20, 21, 61, 66.25, 297.50), 120: (40, 41, 122, 66.25, 297.50)}),
    'flower': (18, {30: (10, 11, (51, 52), 66.25, 297.50), 60: (20, 21, (103, 104), 66.25, 297.50), 120: (40, 41, (207, 208), 66.25, 297.50)}),
    'yellow': (18, {30: (10, 11, 56, 236.44, 537.50), 60: (20, 21, 112, 236.44, 537.50), 120: (40, 41, 224, 236.44, 537.50)}),
}
PEAK_TOL   = 0.05  # units; the disc in Dolphin is 0.03 under the port
VS_TOL     = 0.10  # units/s; Dolphin 289.91
GROUND_TOL = 0.50  # units over the map at the landing point

THROW = re.compile(
    r'^\[scenario\] throw_flight_(?P<case>\w+): THROW (?P<i>\d+) dist (?P<dist>[-\d.]+) yaw (?P<yaw>[-\d.]+) .*?'
    r'grab (?P<grab>\d+) release (?P<rel>\d+) flight (?P<flight>\d+) .*?peak \+(?P<peak>[-\d.]+) .*?'
    r'vS (?P<vs>[-\d.]+) .*?y-ground (?P<yg>[-\d.]+)', re.M)


def check_log(path, case, rate, expected=EXPECTED, profile="disc"):
    """Return a list of deviations from the selected fixture contract."""
    count, by_rate = expected[case]
    grab, rel, flight, peak, vs = by_rate[rate]
    flight_ticks = flight if isinstance(flight, tuple) else (flight,)
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
        if got[:2] != (grab, rel) or got[2] not in flight_ticks:
            problems.append(f'{where}: grab/release/flight {got}, {profile} {(grab, rel, flight)}')
        if abs(float(m['peak']) - peak) > PEAK_TOL:
            problems.append(f"{where}: peak {m['peak']}, {profile} {peak:.2f} (+-{PEAK_TOL})")
        if abs(float(m['vs']) - vs) > VS_TOL:
            problems.append(f"{where}: launch vy {m['vs']}, {profile} {vs:.2f} (+-{VS_TOL})")
        if abs(float(m['yg'])) > GROUND_TOL:
            problems.append(f"{where}: landed {m['yg']} over the ground (on an object, not the map)")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log_dir', help='the scenario-logs directory of a build')
    parser.add_argument('--label', default='')
    parser.add_argument('--profile', choices=('disc', 'synthetic'), default='disc')
    args = parser.parse_args()
    expected = SYNTHETIC_EXPECTED if args.profile == 'synthetic' else EXPECTED
    failed = 0
    for case in expected:
        for rate in (30, 60, 120):
            path = pathlib.Path(args.log_dir) / f'scenario_throw_flight_{case}_{rate}.log'
            try:
                problems = check_log(path, case, rate, expected, args.profile)
            except OSError as error:
                problems = [f'cannot read: {error}']
            name = f'throw_flight_{case} @ {rate} Hz'
            if problems:
                failed += 1
                print(f'FAIL {name}: {len(problems)} problem(s)')
                for p in problems[:20]:
                    print(f'  {p}')
            else:
                g, r, f, pk, v = expected[case][1][rate]
                print(f'ok   {name}: {expected[case][0]} throws, grab {g} release {r} flight {f} peak {pk:.2f} vy {v:.2f}')
    if failed:
        print(f'{failed} throw_flight run(s) differ from the {args.profile} contract {args.label}'.rstrip())
        return 1
    print(f'throw_flight leaf/bud/flower/yellow match the {args.profile} contract {args.label}'.rstrip())
    return 0


if __name__ == '__main__':
    sys.exit(main())
