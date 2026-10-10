"""Run actual native register producers/consumer against the cave-BGM regression."""
import argparse
import os
from pathlib import Path
import subprocess


def extract(source, signature):
    start = source.index(signature)
    at = source.index('{', start)
    end, depth = at + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', default='g++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    game = root / 'games/pikmin2'
    jas = game / 'pikmin2-decomp/src/JSystem/JAudio/JAS'
    source = (jas / 'JASTrack.cpp').read_text('utf-8')
    actual = '\n'.join(extract(source, sig) for sig in (
        'void JASTrack::writeRegDirect(', 'u16 JASTrack::readReg16(', 'u32 JASTrack::readReg32('))
    prefix = r'''
#include <cstdio>
#include <initializer_list>
#include <cstring>
#include "JSystem/JAudio/JAS/JASTrack.h"
// Other register classes are not exercised; this dependency is the original
// signed-byte conversion used only by registers 0..2.
namespace JASPlayer { s16 extend8to16(u8 value) { return static_cast<s8>(value); } }
'''
    suffix = r'''
int main() {
    alignas(JASTrack) unsigned char memory[sizeof(JASTrack)] = {};
    auto* track = reinterpret_cast<JASTrack*>(memory);
    track->mRegisterParam.init();
    // cavesoil.bms reads ports 6/7 into regs 0x10/0x11, then B1 C1 40 00 28
    // uses register 0x28 as its child sequence offset (actual crash: 0x20160000).
    for (unsigned pair = 0; pair < 4; ++pair) {
        const unsigned reg = 0x10 + pair * 2;
        track->writeRegDirect(reg, 0);
        track->writeRegDirect(reg + 1, 0x2016);
        if (track->readReg32(0x28 + pair) != 0x2016) {
            std::printf("FAIL pair=%u childOffset=%08x expected=00002016\n", pair, track->readReg32(0x28 + pair));
            return 1;
        }
        for (unsigned value : {0u, 0x12345678u, 0xffff0001u, 0xffffffffu}) {
            // This is the unchanged canonical u32 storage assignment used by
            // writeRegParam's 0x28..0x2b branch.
            track->mRegisterParam._20[pair] = value;
            if (track->readReg16(reg) != (value >> 16)
                || track->readReg16(reg + 1) != (value & 0xffff)
                || track->readReg32(0x28 + pair) != value) return 2;
        }
    }
    std::puts("sequence register alias regression passed");
}
'''
    out = root / 'output/sequence-register-alias'
    out.mkdir(parents=True, exist_ok=True)
    flags = ['-w', '-std=c++17', '-fpermissive', '-DPIKI_PC_PORT', '-DVERSION_GPVE01',
             '-include', str(game / 'pikmin2-decomp-adapter/pikmin2_pc_types.h')]
    flags += ['-I' + str(game / path) for path in ('pikmin2-decomp/include', 'pikmin2-decomp-adapter', 'pc_port')]
    env = dict(os.environ)
    env['PATH'] = str(Path(args.compiler).resolve().parent) + os.pathsep + env.get('PATH', '')
    # Pin the pre-fix native source so the negative control remains meaningful
    # after this regression and the repair are committed.
    old_source = subprocess.run(['git', 'show', '135225a6bb32ceb923602fb292e34884d8a30eca:games/pikmin2/pikmin2-decomp/src/JSystem/JAudio/JAS/JASTrack.cpp'],
                                cwd=root, capture_output=True, text=True, encoding='utf-8', check=True).stdout
    old = '\n'.join(extract(old_source, sig) for sig in (
        'void JASTrack::writeRegDirect(', 'u16 JASTrack::readReg16(', 'u32 JASTrack::readReg32('))
    if 'read16Alias' not in actual or 'write16Alias' not in actual:
        raise SystemExit('Production 16-bit alias consumers not found')
    results = []
    for name, body in [('fixed', actual), ('old', old)]:
        cpp, exe = out / (name + '.cpp'), out / (name + '.exe')
        cpp.write_text(prefix + body + suffix, encoding='utf-8')
        subprocess.run([args.compiler, *flags, str(cpp), str(jas / 'JASRegisterParam.cpp'), '-o', str(exe)], check=True, env=env)
        result = subprocess.run([str(exe)], capture_output=True, text=True, env=env)
        print(name, 'exit', result.returncode, result.stdout.strip())
        results.append(result.returncode)
    if results != [0, 1]:
        raise SystemExit('Expected fixed pass and original endian regression failure')


if __name__ == '__main__':
    main()
