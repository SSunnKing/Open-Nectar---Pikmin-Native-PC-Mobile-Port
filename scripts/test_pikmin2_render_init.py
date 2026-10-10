"""Run synthetic P2 field-decoder and pellet-init regressions without game assets.

Usage: python scripts/test_pikmin2_render_init.py --compiler g++
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='g++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    game = root / 'games/pikmin2'
    decomp = game / 'pikmin2-decomp/include'
    env = dict(os.environ)
    env['PATH'] = str(Path(args.compiler).resolve().parent) + os.pathsep + env.get('PATH', '')
    flags = ['-w', '-std=c++17', '-fpermissive', '-DPIKI_PC_PORT', '-DVERSION_GPVE01',
             '-include', str(game / 'pikmin2-decomp-adapter/pikmin2_pc_types.h'),
             # Explicit paths avoid case-insensitive Windows adapter wrappers.
             '-include', str(decomp / 'Dolphin/mtx.h'),
             '-include', str(decomp / 'Dolphin/os.h')]
    includes = ['-I' + str(game / p) for p in
                ('pikmin2-decomp-adapter', 'pc_port', 'pikmin2-decomp/include')]
    with tempfile.TemporaryDirectory(prefix='nectar-p2-regression-') as temp:
        out = Path(temp)
        for name in ('field_vtx_decode', 'pellet_init'):
            exe = out / (name + '.exe')
            subprocess.run([args.compiler, *flags, *includes,
                            str(game / f'pc_port/pc_p2_{name}_test.cpp'), '-o', str(exe)],
                           check=True, env=env)
            subprocess.run([str(exe)], check=True, env=env)
        # Removing the initialization must reproduce poisoned-memory failure.
        old_include = out / 'old/Game'
        old_include.mkdir(parents=True)
        header = (decomp / 'Game/pelletMgr.h').read_text(encoding='utf-8')
        assert header.count('mPelletColor = 0;') == 1
        (old_include / 'pelletMgr.h').write_text(header.replace('mPelletColor = 0;', ''), encoding='utf-8')
        negative = out / 'pellet_old.exe'
        subprocess.run([args.compiler, *flags, '-I' + str(out / 'old'), *includes,
                        str(game / 'pc_port/pc_p2_pellet_init_test.cpp'), '-o', str(negative)],
                       check=True, env=env)
        result = subprocess.run([str(negative)], env=env, capture_output=True)
        if result.returncode == 0:
            raise RuntimeError('Pellet regression failed to detect the original bug')
        print('Original pellet constructor rejected by poisoned-memory negative control')


if __name__ == '__main__':
    main()
