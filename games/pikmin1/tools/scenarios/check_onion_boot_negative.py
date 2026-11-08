#!/usr/bin/env python3
"""Mutation control: disable only the authored route-opening effect cue of the Onion boot motions."""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--rate', type=int, choices=(30, 60), required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--route-frame', type=int, default=100, help='frame of the authored route cue (nectar::kOnionBootRouteFrame)')
    args = parser.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='onion-boot-', dir=args.work_dir) as scratch:
        data = Path(scratch) / 'data'
        shutil.copytree(args.data, data)
        table = data / 'assets/dataDir/objects/goal/anims.bin'
        original = table.read_bytes()
        # AnimKey record: frame, event type, command id, key type. Change
        # value 2 to an unused effect id; keep timing/order/counts unchanged.
        # The authored boot motions (standin_onion_boot.h) carry the cue at ROUTE_FRAME.
        route_frame = int(args.route_frame)
        cue = struct.pack('>iHBB', route_frame, 1, 0, 2)
        if original.count(cue) != 3:
            raise RuntimeError('expected exactly the three authored route cues (one per boot motion)')
        table.write_bytes(original.replace(cue, struct.pack('>iHBB', route_frame, 1, 0, 255)))
        for colour, stage in (('blue', 'sandbox_objects'), ('yellow', 'sandbox_onions')):
            env = dict(os.environ, PIKMIN_SCENARIO_DATA_DIR=str(data.resolve()),
                       PIKMIN_SCENARIO_STAGE=f'stages/{stage}.ini')
            result = subprocess.run([str(args.game.resolve()), '--scenario', f'{colour}_onion_boot',
                                     '--scenario-tick-rate', str(args.rate)],
                                    env=env, cwd=data, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, timeout=120)
            (args.work_dir / f'{colour}_{args.rate}.log').write_text(result.stdout)
            expected = 'expected route closed through the effect frame, then open'
            if result.returncode != 1 or expected not in result.stdout:
                raise RuntimeError(f'{colour}/{args.rate}: expected missing-route-cue failure; '
                                   f'exit {result.returncode}, inspect log')
            print(f'PASS negative control {colour}/{args.rate}: missing route cue detected')


if __name__ == '__main__':
    main()
