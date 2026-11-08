#!/usr/bin/env python3
"""Anchor the 30 Hz fall and flick to the original-disc measurements (local exact lane).

Runs ballistic_fall and ballistic_flick at 30 Hz on a user-supplied GPIE01 rev 1 install
(PIKMIN_SCENARIO_DATA_DIR, read-only) and compares the traces with
evidence/ballistic-gc-2026-11-06.json through check_ballistic.check_disc_reference().
Exit 77 (skip) without an install.
"""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from check_ballistic import check_disc_reference, evidence_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    args = parser.parse_args()
    data = os.environ.get("PIKMIN_SCENARIO_DATA_DIR")
    if not data:
        print("Set PIKMIN_SCENARIO_DATA_DIR to a local GPIE01 rev 1 install.")
        return 77
    if not evidence_path().is_file():
        print(f"No retained original-disc traces at {evidence_path()} (set PIKMIN_BALLISTIC_EVIDENCE).")
        return 77
    data_dir = Path(data).resolve()
    if not (data_dir / "assets/dataDir/parms/gamePrms.bin").is_file():
        print(f"Missing game assets in {data_dir}")
        return 1
    env = dict(os.environ, PIKMIN_SCENARIO_DATA_DIR=str(data_dir),
               PIKMIN_SCENARIO_STAGE="stages/stage1.ini", PIKMIN_BALLISTIC_SPOT="-228,1976")
    env.setdefault("SDL_AUDIODRIVER", "dummy")
    with tempfile.TemporaryDirectory(prefix="nectar-disc-ballistic-") as scratch:
        for case in ("fall", "flick"):
            log = Path(scratch) / f"scenario_ballistic_{case}_30.log"
            try:
                with log.open("w") as output:
                    result = subprocess.run(
                        [str(args.game.resolve()), "--scenario", f"ballistic_{case}", "--scenario-tick-rate", "30"],
                        cwd=scratch, env=env, stdout=output, stderr=subprocess.STDOUT, timeout=180)
            except (OSError, subprocess.TimeoutExpired) as error:
                print(f"FAIL ballistic_{case}: {error}")
                return 1
            if result.returncode:
                print(f"FAIL ballistic_{case} exited with {result.returncode}")
                print("\n".join(log.read_text(errors="replace").splitlines()[-8:]))
                return 1
        try:
            problems = check_disc_reference(scratch)
        except (OSError, ValueError) as error:
            problems = [str(error)]
    for problem in problems:
        print(f"FAIL {problem}")
    if not problems:
        print("30 Hz fall/flick on the disc install match the retained original-disc evidence within 0.001 units")
    return int(bool(problems))


if __name__ == "__main__":
    raise SystemExit(main())
