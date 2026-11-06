#!/usr/bin/env python3
"""Retain exact source/build/input provenance alongside RNG scenario evidence."""
import hashlib
import json
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[4]
build = pathlib.Path(sys.argv[1])
upstream = 'fc8bcf94de15e4824ccacbdad4042b654c7d50ab'
subprocess.run(['git','-C',str(root),'merge-base','--is-ancestor',upstream,'HEAD'],check=True)
cache = (build/'CMakeCache.txt').read_text(encoding='utf-8')
compiler = next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('CMAKE_CXX_COMPILER:FILEPATH='))
paths = ['games/pikmin1/pc_port/scenarios/scenario_throw_release.cpp',
         'games/pikmin1/pc_port/netplay/pc_netplay_det.cpp',
         'games/pikmin1/tools/scenarios/run_standin_scenario.cmake']
result = {
    'revision': subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
    'upstream_revision': upstream,
    'harness_base_revision': upstream,
    'rng_order': 'GameCube draw order from upstream #256',
    'compiler': subprocess.check_output([compiler,'--version'],text=True),
    'session_seed': 0, 'rates': [30,60],
    # Git permits platform line endings; compare canonical text, retain raw bytes too.
    'input_source_sha256': {p:hashlib.sha256((root/p).read_bytes().replace(b'\r\n',b'\n')).hexdigest() for p in paths},
    'input_worktree_sha256': {p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},
    'limitations': 'Scenario builds enable deterministic floats and generated stand-in assets; visual and timing fidelity remains bounded by those stand-ins.',
}
output = build/'scenario-evidence'
output.mkdir(exist_ok=True)
(output/'metadata.json').write_text(json.dumps(result,indent=2)+'\n')
for filename in ('CMakeCache.txt','CTestTestfile.cmake'):
    (output/filename).write_bytes((build/filename).read_bytes())
