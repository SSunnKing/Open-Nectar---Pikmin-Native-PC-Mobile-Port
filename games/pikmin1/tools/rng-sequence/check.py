#!/usr/bin/env python3
"""Compile the actual PC expression blocks from #224 in a synthetic context."""
import argparse
import hashlib
import json
import pathlib
import re
import shlex
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
GAME = ROOT / 'games/pikmin1'
# Each anchor is unique. Extract the PC arm, not a hand-maintained copy.
SITES = [
    ('spawn', 'src/plugPikiKando/viewPiki.cpp', 'void ViewPiki::init(', 'mSRT.t', [1, 1], 'sim', ['(u[0]-.5f)*300', '0', '(u[1]-.5f)*300']),
    ('territory', 'src/plugPikiYamashita/TAIAmove.cpp', 'void TAIAflyingInTerritory::setTargetPosition(', 'offset', [2, 2], 'sim', ['u[0]*2-1', '0', 'u[1]*2-1']),
    ('direction', 'src/plugPikiYamashita/TAIAmove.cpp', 'void TAIAlookAround::setTargetDirection(', 'teki.mTargetAngle', [0.75, 10], 'sim', ['(.75f+u[0]*.75f)*(u[1]*10>5?1.f:-1.f)+.25f']),
    ('attack', 'src/plugPikiYamashita/TAImar.cpp', '// - weight = 101', 'res', ['100.f/weight', 30], 'sim', ['(u[0]*(100.f/weight))*(u[1]*30)<=1']),
    ('cloth', 'include/Omake.h', 'if (i != 0 && j != 0 && i != mGridWidth', 'vec', [1, 1], 'sim', ['u[0]-2', 'u[1]-2', '0']),
    ('balloon', 'src/plugPikiYamashita/drawCommon.cpp', 'void zen::BalloonPane::setGoalPos(', 'randOffset', [2, 2], 'cosmetic', ['12*(u[0]*2-1)', '12*(u[1]*2-1)', '0']),
    ('star', 'src/plugPikiYamashita/drawWorldMap.cpp', 'if (pc_cosmetic_randf(100.0f) < mStarFallChance)', 'starResult', [100, 720, 50, 150], 'cosmetic', ['u[1]*720-40', '500-u[2]*50', '-u[3]*150']),
    ('panel', 'include/zen/MenuPanelMgr.h', 'MenuPanel(P2DPane* pane,', 'mRotationSpeed', [1, 10, 0.1, 10], 'cosmetic', ['(u[0]+1)*(u[1]*10>5?1.f:-1.f)']),
]


def extract(site):
    name, path, anchor, *_ = site
    source = (GAME / path).read_text(encoding="utf-8")
    if source.count(anchor) != 1:
        raise ValueError(f'{name}: source anchor missing or ambiguous')
    start = source.index(anchor)
    if name == 'star':
        start = source.rfind('#if defined(PIKI_PC_PORT)', 0, start)
    else:
        start = source.index('#if defined(PIKI_PC_PORT)', start)
    end = source.index('#else', start)
    block = source[start + len('#if defined(PIKI_PC_PORT)'):end]
    if '#' in block:
        raise ValueError(f'{name}: nested preprocessor arm requires review')
    if name == 'attack':
        # The branch shares its body with the original non-PC expression.
        tail = source[source.index('#endif', end) + len('#endif'):]
        match = re.match(r'\s*res = true;\s*} else {\s*res = false;\s*}', tail)
        if not match:
            raise ValueError('attack: shared branch body changed')
        block += match.group()
    if name == 'star':
        macro = re.findall(r'^#define PC_WM_STAR_X (.+)$', source, re.M)
        if len(macro) != 1:
            raise ValueError('star: macro missing or ambiguous')
        block = '#define PC_WM_STAR_X ' + macro[0] + '\n' + block + '\n#undef PC_WM_STAR_X\n'
    return block


PREAMBLE = r'''
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using f32 = float;
struct Vector3f {
    float x=0,y=0,z=0;
    Vector3f() = default;
    Vector3f(float a,float b,float c):x(a),y(b),z(c){}
    void set(float a,float b,float c) {x=a;y=b;z=c;}
};
struct Draw { float range; bool cosmetic; };
std::vector<Draw> draws;
float u[8];
float sample(float range, bool cosmetic) {
    if(draws.size()>=8) std::abort();
    draws.push_back({range,cosmetic});
    return u[draws.size()-1]*range;
}
struct System { float getRand(float n) {return sample(n,false);} } systemStub;
System* gsys=&systemStub;
namespace zen { float Rand(float n) {return sample(n,false);} }
float pc_cosmetic_randf(float n) {return sample(n,true);}
int pc_gfx_menu_shift_center() {return 40;}
Vector3f starResult;
struct Effects {void create(int, Vector3f v, void*, void*) {starResult=v;}} effects;
Effects* WMeffMgr=&effects;
constexpr int EFF2D_MapShootingStar=1;
void check(bool ok, const char* site) {
    if(!ok) {std::fprintf(stderr,"FAIL %s\n",site);std::exit(17);}
}
bool equal(float a,float b) {return std::fabs(a-b)<=0.00001f;}
'''


def program(blocks):
    out = [PREAMBLE]
    for site in SITES:
        name, _, _, result, ranges, stream, expected = site
        setup = 'Vector3f offset,vec,randOffset; struct {Vector3f t;} mSRT; struct {float mTargetAngle=0,mFaceDirection=.25f;} teki; float angle=.75f,mGoalRadius=12,mRotationSpeed=0,mScaleSpeedFactor=0,mMoveSpeed=0,mStarFallChance=101; bool res=false;'
        checks = []
        if len(expected) == 3:
            checks += [f'equal({result}.{axis}, {value})' for axis, value in zip('xyz', expected)]
        else:
            checks.append(f'equal({result}, {expected[0]})')
        checks.append(f'draws.size()=={len(ranges)}')
        # Size check must precede indexed reads.
        checks = [checks[-1]] + checks[:-1]
        for i, value in enumerate(ranges):
            checks.append(f'equal(draws[{i}].range, {value}) && draws[{i}].cosmetic=={str(stream == "cosmetic").lower()}')
        if name == 'panel':
            checks += ['equal(mScaleSpeedFactor,u[2]*.1f+.05f)', 'equal(mMoveSpeed,u[3]*10+5)']
        out.append(f'void test_{name}(int weight) {{ draws.clear(); {setup}\n{blocks[name]}\ncheck({" && ".join(checks)},"{name}");\n}}')
        if name == 'star':
            out.append(f'void test_star_skip() {{ draws.clear(); {setup} mStarFallChance=0;\n{blocks[name]}\ncheck(draws.size()==1 && draws[0].cosmetic && equal(draws[0].range,100),"star_skip"); }}')
    out.append('void run_cases() { for(int weight: {1,51,100,101}) {')
    out.extend(f'test_{s[0]}(weight);' for s in SITES)
    out.append('test_star_skip(); } }')
    out.append('int main() { for(unsigned seed: {1u,7u,42u,0xc05e77u}) { unsigned state=seed; for(float& v:u) {state=state*1103515245u+12345u; v=float((state>>16)&32767u)/32768.f;} run_cases(); } for(float v: {0.f,.01f,.999f}) {for(float& x:u) x=v; run_cases();} for(float second: {std::nextafter(1.f/15,0.f),1.f/15,std::nextafter(1.f/15,1.f)}) {for(float& x:u) x=.5f; u[1]=second; run_cases();} std::puts("PASS all eight RNG sites including attack hit/miss/boundary vectors"); }')
    return '\n'.join(out)


def mutate(block, name):
    # Swap actual adjacent draw declarations; a compile error is never a pass.
    declarations = list(re.finditer(r'^[ \t]*const f32 [^\n;]+;[^\n]*$', block, re.M))
    if len(declarations) < 2:
        raise ValueError(f'{name}: no two draw declarations to mutate')
    a, b = declarations[:2]
    return block[:a.start()] + b.group() + block[a.end():b.start()] + a.group() + block[b.end():]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='c++')
    parser.add_argument('--output', type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    blocks = {s[0]: extract(s) for s in SITES}
    metadata = {'revision': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
                'compiler': subprocess.check_output([args.cxx, '--version'], text=True),
                'seeds': [1,7,42,0xc05e77], 'weights': [1,51,100,101], 'explicit_vectors': 'all-zero, .01, .999; attack product boundary at weight=100 with nextafter neighbors', 'results': [],
                'sources': {s[1]: hashlib.sha256((GAME/s[1]).read_bytes()).hexdigest() for s in SITES}}
    try:
        for opt in ['-O0', '-O2']:
            for mutation in [None] + list(blocks) + ['attack-decision']:
                variant = dict(blocks)
                if mutation == 'attack-decision':
                    variant['attack'], count = re.subn(r'if \(chanceA \* chanceB <= 1\.0f\)', 'if (false)', variant['attack'])
                    if count != 1:
                        raise ValueError('attack decision anchor changed')
                elif mutation:
                    variant[mutation] = mutate(variant[mutation], mutation)
                label = f'{opt[1:]}-{mutation or "baseline"}'
                cpp = args.output / f'{label}.cpp'
                exe = args.output / f'{label}.exe'
                cpp.write_text(program(variant))
                command = [args.cxx, '-std=c++17', opt, '-ffp-contract=off', str(cpp), '-o', str(exe)]
                compiled = subprocess.run(command, text=True, capture_output=True)
                (args.output/f'{label}.compile.log').write_text(compiled.stdout+compiled.stderr)
                if compiled.returncode:
                    raise RuntimeError(f'{label}: compilation failed (not mutation detection)')
                run = subprocess.run([str(exe.resolve())], text=True, capture_output=True)
                (args.output/f'{label}.run.log').write_text(run.stdout+run.stderr)
                ok = run.returncode == 0 if mutation is None else run.returncode == 17 and run.stderr.strip() == f'FAIL {"attack" if mutation == "attack-decision" else mutation}'
                metadata['results'].append({'variant': label, 'command': shlex.join(command), 'returncode': run.returncode, 'passed': ok})
                print(f'{label}: {"PASS" if ok else "FAIL"}', flush=True)
                if not ok:
                    raise RuntimeError(f'{label}: unexpected runtime result: {run.stdout} {run.stderr}')
    finally:
        (args.output/'metadata.json').write_text(json.dumps(metadata, indent=2)+'\n')


if __name__ == '__main__':
    main()
