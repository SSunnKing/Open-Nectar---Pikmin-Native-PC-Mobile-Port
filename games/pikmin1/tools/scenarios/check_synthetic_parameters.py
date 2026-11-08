#!/usr/bin/env python3
"""Check the fixture contract: tagged files delegate values to source constructors.

Parameters::read consumes one 0xffffffff terminator per list without changing
constructor defaults (covered independently by nectar_parms_writer_test).

Enemy parameters are not written at all: TekiMgr constructs every *Parameters
type with its defaults before CoreNode::load optionally reads tekipara/<type>.bin,
and a missing file is a no-op. A stub file would be unsafe (TekiParameters::read
is positional), so the tree must not contain any.
"""
import pathlib
import sys

# path under assets/dataDir: number of empty tagged lists
LISTS = {
    'parms/gamePrms.bin': 1,
    'parms/aiconst.bin': 1,
    'parms/demo.bin': 1,
    'parms/cunit.bin': 1,
    'parms/naviMgr.bin': 2,
    'parms/pikiMgr.bin': 2,
    # SnakeProp/MizuProp::read: CreatureProp, BossProp and the subclass list.
    'bosses/snake/parms.bin': 3,
    'bosses/mizu/parms.bin': 3,
}


def check(root):
    problems = []
    data = pathlib.Path(root) / 'assets/dataDir'
    for name, count in LISTS.items():
        path = data / name
        expected = b'\xff\xff\xff\xff' * count
        if not path.is_file() or path.read_bytes() != expected:
            problems.append(f'{name}: expected {count} empty tagged list(s), using engine defaults')
    for path in sorted((data / 'tekipara').glob('*.bin')):
        problems.append(f'tekipara/{path.name}: enemy parameters must stay unwritten (source constructor defaults)')
    return problems


if __name__ == '__main__':
    failures = check(sys.argv[1])
    for failure in failures:
        print(f'FAIL {failure}')
    if not failures:
        print(f'Synthetic parameter contract: all {len(LISTS)} files use source constructor defaults, no tekipara overrides')
    sys.exit(bool(failures))
