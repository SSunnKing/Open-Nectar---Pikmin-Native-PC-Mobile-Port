"""Exercise production isPlaying and real native header layouts without a game build.

Run: py -3.12 tests/pikmin2_sequence_stop_ack.py --compiler C:/msys64/mingw64/bin/g++.exe
The negative control reinstates the original erroneous SeqSound offset walk.
"""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', default='g++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    game = root / 'games/pikmin2'
    source = (game / 'pikmin2-decomp/src/plugProjectHikinoU/PSSeq.cpp').read_text('utf-8')
    signature = 'bool SeqBase::isPlaying()'
    at = source.index(signature)
    start = source.index('{', at)
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    body = source[start:end]
    out = root / 'output/sequence-stop-ack'
    out.mkdir(parents=True, exist_ok=True)
    prefix = r'''
#include <type_traits>
#include <cstdio>
#include "PSSystem/PSSeq.h"
#include "PSSystem/PSSystemIF.h"
#include "JSystem/JAudio/JAS/JASMutexLock.h"
static bool irq;
BOOL OSDisableInterrupts() { bool enabled = !irq; irq = true; return enabled; }
BOOL OSRestoreInterrupts(BOOL enabled) { bool previous = !irq; irq = !enabled; return previous; }
using TrackPointer = decltype(PSSystem::SeqBase::mSeqSound);
static_assert(std::is_same<TrackPointer, JASTrack*>::value, "retained owner must be a track");
bool observed(TrackPointer mSeqSound)
'''
    suffix = r'''
int main() {
    // This is an aligned memory-layout fixture, not a constructed/live track.
    // Extra zeroed storage keeps the old erroneous double-offset read within
    // allocated storage instead of depending on a random adjacent allocation.
    alignas(JASTrack) unsigned char memory[sizeof(PSSystem::SeqSound) + 2 * sizeof(JASTrack)] = {};
    auto* track = reinterpret_cast<JASTrack*>(memory);
    if (observed(nullptr)) return 10;
    for (int savedWait : {0, -1, 0x01020304}) {
        track->mSeqCtrl.mState.w = savedWait;
        for (unsigned state : {0u, 1u, 2u, 3u}) {
            track->_35B = state;
            bool expected = state == 1 || state == 3;
            if (observed(track) != expected || irq) {
                std::printf("FAIL savedWait=%d trackState=%u expectedPlaying=%d\n", savedWait, state, expected);
                return 1;
            }
        }
    }
    std::puts("sequence stop acknowledgement regression passed");
}
'''
    flags = ['-w', '-std=c++17', '-fpermissive', '-DPIKI_PC_PORT', '-DVERSION_GPVE01',
             '-include', str(game / 'pikmin2-decomp-adapter/pikmin2_pc_types.h')]
    flags += ['-I' + str(game / path) for path in ('pikmin2-decomp-adapter', 'pc_port', 'pikmin2-decomp/include')]
    env = dict(os.environ)
    compiler_dir = str(Path(args.compiler).resolve().parent)
    env['PATH'] = compiler_dir + os.pathsep + env.get('PATH', '')
    results = []
    for name, actual in [('fixed', body), ('old', body.replace('mSeqSound->_35B',
                       'reinterpret_cast<PSSystem::SeqSound*>(mSeqSound)->mSeqParameter.mTrack.mSeqCtrl.mState.b[3]'))]:
        cpp, exe = out / (name + '.cpp'), out / (name + '.exe')
        cpp.write_text(prefix + actual + suffix, encoding='utf-8')
        subprocess.run([args.compiler, *flags, str(cpp), '-o', str(exe)], check=True, env=env)
        result = subprocess.run([str(exe)], capture_output=True, text=True, env=env)
        print(name, 'exit', result.returncode, result.stdout.strip())
        results.append(result.returncode)
    if results[0] != 0 or results[1] != 1:
        raise SystemExit('Expected fixed pass and original offset regression failure')


if __name__ == '__main__':
    main()
