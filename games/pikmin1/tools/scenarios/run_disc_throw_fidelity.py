#!/usr/bin/env python3
"""Run the exact throw checks against a locally supplied GPIE01 rev 1 install."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from check_throw_flight import EXPECTED, check_log


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("--case", choices=EXPECTED)
    parser.add_argument("--rate", type=int, choices=(30, 60))
    args = parser.parse_args()
    data = os.environ.get("PIKMIN_SCENARIO_DATA_DIR")
    if not data:
        print("Set PIKMIN_SCENARIO_DATA_DIR to a local GPIE01 rev 1 install.")
        return 77
    data_dir = Path(data).resolve()
    if not (data_dir / "assets/dataDir/parms/gamePrms.bin").is_file():
        print(f"Missing game assets in {data_dir}")
        return 1

    env = dict(os.environ, PIKMIN_SCENARIO_DATA_DIR=str(data_dir),
               PIKMIN_SCENARIO_STAGE="stages/stage1.ini", PIKMIN_TF_SPOT="-228,1976")
    env.pop("PIKMIN_TF_PROBE", None)
    env.pop("PIKMIN_SCENARIO_KEEP_SAVE_DIR", None)
    env.pop("PIKMIN_TF_FLOWER_GRAV", None)
    env.setdefault("SDL_AUDIODRIVER", "dummy")
    cases = (args.case,) if args.case else EXPECTED
    rates = (args.rate,) if args.rate else (30, 60)
    failed = False
    with tempfile.TemporaryDirectory(prefix="nectar-disc-throw-") as scratch:
        for case in cases:
            for rate in rates:
                scenario = f"throw_flight_{case}"
                log = Path(scratch) / f"{scenario}_{rate}.log"
                try:
                    with log.open("w") as output:
                        result = subprocess.run(
                            [str(args.game.resolve()), "--scenario", scenario,
                             "--scenario-tick-rate", str(rate)],
                            cwd=scratch, env=env, stdout=output,
                            stderr=subprocess.STDOUT, timeout=180,
                        )
                    problems = check_log(log, case, rate)
                    if result.returncode:
                        problems.insert(0, f"scenario exited with {result.returncode}")
                        problems.extend(log.read_text(errors="replace").splitlines()[-8:])
                except (OSError, subprocess.TimeoutExpired) as error:
                    problems = [str(error)]
                if problems:
                    failed = True
                    print(f"FAIL {scenario} @ {rate} Hz")
                    for problem in problems[:20]:
                        print(f"  {problem}")
                else:
                    print(f"PASS {scenario} @ {rate} Hz")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
