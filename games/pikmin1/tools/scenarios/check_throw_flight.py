#!/usr/bin/env python3
"""Assert the stand-in throw_flight logs against the disc-verified throw.

Every THROW line of the leaf, bud, flower and yellow scenarios at 30/60/120 Hz
must preserve the 30 Hz peak and launch speed. Input keyframes retain the configured
rate's cadence; high-frequency contact may quantize landing by one high-rate tick.

  grab     ticks from the A press to the grab keyframe
  release  ticks from the grab to Flying (Navi::throwPiki)
  flight   ticks from release to the landing bounce
  peak     sampled height above release, in world units
  vS       launch speed up, in world units/second

Original evidence (2026-11-03): the port on GPIE01 rev 1 assets, stand-ins, and
Dolphin agreed on the 30 Hz throw timing. Dolphin measured peak 61.26, launch
289.91; the port gives 61.29/290.00. Fresh original-disc measurements and their
limits are retained in evidence/ballistic-gc-2026-11-06.json.

Issue #157: the 60/120 Hz rows below are newly measured port results, not GameCube
measurements. They reproduce the independently calibrated 30 Hz recurrence. Yellow
launches at 370 and peaks at 104.40 at all three rates; its flight is 37/74/149 ticks.
Stand-ins run from PIKMIN_TF_SPOT=-350,350 to avoid the Onion platforms, with every
landing on the map (see scenario_throw_flight.cpp). The 2.6-second hold and
throw_cadence are not asserted here.
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
        for rate in (30, 60, 120):
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
    print(f'throw_flight leaf/bud/flower/yellow match the disc-calibrated 30 Hz reference {args.label}'.rstrip())
    return 0


if __name__ == '__main__':
    sys.exit(main())
